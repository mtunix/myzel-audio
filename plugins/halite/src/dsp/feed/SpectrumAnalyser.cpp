#include "dsp/feed/SpectrumAnalyser.h"

#include "dsp/feed/SpectralSnapshot.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace halite::dsp
{

namespace
{
    float dbOf (double power, float floorDb) noexcept { return power > 0.0 ? std::max ((float) (10.0 * std::log10 (power)), floorDb) : floorDb; }
}

int SpectrumAnalyser::fftSizeFor (double sampleRate) noexcept
{
    const auto octaves = sampleRate > 0.0 ? std::clamp ((int) std::lround (std::log2 (sampleRate / 48000.0)), -2, 3) : 0;
    return octaves >= 0 ? 4096 << octaves : 4096 >> -octaves;
}

double SpectrumAnalyser::proportionOf (double hz) noexcept
{
    return std::log (std::max (hz, 1.0) / lowestHz) / std::log (highestHz / lowestHz);
}

double SpectrumAnalyser::frequencyAt (double proportion) noexcept
{
    return lowestHz * std::pow (highestHz / lowestHz, proportion);
}

void SpectrumAnalyser::prepare (double sampleRate, int numPoints, const Settings& s)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    settings = s;

    const auto n = fftSizeFor (rate);
    auto order = 0;
    while ((1 << order) < n)
        ++order;
    fft.prepare (order);

    // Periodic Hann: its squares sum to 3n/8, the scale of every bin's power below.
    window.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n));
    frame.assign ((size_t) n, 0.0f);
    re.assign ((size_t) n / 2 + 1, 0.0f);
    im.assign ((size_t) n / 2 + 1, 0.0f);
    streams.assign ((size_t) SampleFifo::numStreams, std::vector<float> ((size_t) n, 0.0f));
    power.assign ((size_t) n / 2 + 1, 0.0);
    cumulative.assign ((size_t) n / 2 + 2, 0.0);
    bandScratch.assign ((size_t) SpectralSnapshot::maxBins + 1, 0.0);

    numPoints = std::max (numPoints, 2);
    pointHz.resize ((size_t) numPoints);
    for (int p = 0; p < numPoints; ++p)
        pointHz[(size_t) p] = (float) frequencyAt ((double) p / (numPoints - 1));
    bands.assign ((size_t) numPoints, 0.0);
    input.assign ((size_t) numPoints, settings.floorDb);
    output.assign ((size_t) numPoints, settings.floorDb);
    inputPower.assign ((size_t) numPoints, 0.0);
    outputPower.assign ((size_t) numPoints, 0.0);
    started = false;
}

void SpectrumAnalyser::clear() noexcept
{
    std::fill (input.begin(), input.end(), settings.floorDb);
    std::fill (output.begin(), output.end(), settings.floorDb);
    std::fill (inputPower.begin(), inputPower.end(), 0.0);
    std::fill (outputPower.begin(), outputPower.end(), 0.0);
    started = false;
}

void SpectrumAnalyser::bandPowers (const double* cum, int bins, double binHz, double* out) const noexcept
{
    // Bin k holds its power evenly over [k − ½, k + ½) bins: the band's power is the integral of
    // that density over its edges, so a band narrower than a bin takes its share of one.
    const auto at = [cum, bins] (double x)
    {
        const auto u = std::clamp (x + 0.5, 0.0, (double) bins);
        const auto j = std::min ((int) u, bins - 1);
        return cum[j] + (u - j) * (cum[j + 1] - cum[j]);
    };

    const auto half = std::exp2 (0.5 * settings.bandOctaves);
    for (size_t p = 0; p < pointHz.size(); ++p)
    {
        const auto f = (double) pointHz[p];
        out[p] = std::max (0.0, at (f * half / binHz) - at (f / half / binHz));
    }
}

void SpectrumAnalyser::bandLevels (const float* meanSquarePerBin, int bins, double binHz, float* outDb) noexcept
{
    bins = std::clamp (bins, 1, (int) bandScratch.size() - 1);
    bandScratch[0] = 0.0;
    for (int k = 0; k < bins; ++k)
        bandScratch[(size_t) k + 1] = bandScratch[(size_t) k] + std::max (0.0f, meanSquarePerBin[k]);
    bandPowers (bandScratch.data(), bins, binHz, bands.data());
    for (size_t p = 0; p < bands.size(); ++p)
        outDb[p] = dbOf (bands[p], settings.floorDb);
}

bool SpectrumAnalyser::update (const SampleFifo& fifo) noexcept
{
    const auto end = fifo.count();
    if (started && end == lastEnd)
        return false;

    const auto n = fft.size();
    float* out[SampleFifo::numStreams];
    for (int s = 0; s < SampleFifo::numStreams; ++s)
        out[s] = streams[(size_t) s].data();
    if (! fifo.read (end, n, out))
        return false;

    const auto seconds = started ? (double) (end - lastEnd) / rate : 1.0e9;
    lastEnd = end;
    started = true;

    const auto bins = n / 2 + 1;
    const auto binHz = rate / n;
    // One-sided power over the mean square: 2 |X|² / (n Σw²) with Σw² = 3n/8, a half for DC and
    // Nyquist; and a half again for the mean of the two channels.
    const auto scale = 16.0 / (3.0 * (double) n * (double) n) * 0.5;

    const auto attack = 1.0 - std::exp (-seconds / (settings.attackMs * 0.001));
    const auto release = 1.0 - std::exp (-seconds / (settings.releaseMs * 0.001));

    for (int which = 0; which < 2; ++which)
    {
        std::fill (power.begin(), power.end(), 0.0);
        for (int c = 0; c < 2; ++c)
        {
            const auto& x = streams[(size_t) (which == 0 ? SampleFifo::inputStream (c) : SampleFifo::outputStream (c))];
            for (int i = 0; i < n; ++i)
                frame[(size_t) i] = x[(size_t) i] * window[(size_t) i];
            fft.forward (frame.data(), re.data(), im.data());
            for (int k = 0; k < bins; ++k)
                power[(size_t) k] += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
        }

        cumulative[0] = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            const auto edge = k == 0 || k == bins - 1 ? 0.5 : 1.0;
            cumulative[(size_t) k + 1] = cumulative[(size_t) k] + power[(size_t) k] * scale * edge;
        }
        bandPowers (cumulative.data(), bins, binHz, bands.data());

        auto& smoothed = which == 0 ? inputPower : outputPower;
        auto& db = which == 0 ? input : output;
        for (size_t p = 0; p < bands.size(); ++p)
        {
            const auto target = bands[p];
            smoothed[p] += (target > smoothed[p] ? attack : release) * (target - smoothed[p]);
            db[p] = dbOf (smoothed[p], settings.floorDb);
        }
    }
    return true;
}

//==============================================================================
void LevelMeters::prepare (double sampleRate) noexcept
{
    keep = std::exp (-1.0 / (std::max (sampleRate, 1.0) * rmsMs * 0.001));
    started = false;
    meanSquare = {};
    peak.fill (floorDb);
    rms.fill (floorDb);
}

bool LevelMeters::update (const SampleFifo& fifo) noexcept
{
    const auto end = fifo.count();
    cursor = std::max (cursor, fifo.oldestSafe());
    if (cursor >= end)
        return false;

    std::array<float, SampleFifo::numStreams> largest {};
    float* out[SampleFifo::numStreams];
    for (size_t s = 0; s < chunk.size(); ++s)
        out[s] = chunk[s].data();

    while (cursor < end)
    {
        const auto n = (int) std::min<std::uint64_t> (end - cursor, chunk[0].size());
        const auto ok = fifo.read (cursor + (std::uint64_t) n, n, out);
        cursor += (std::uint64_t) n;
        if (! ok)
            continue;
        for (size_t s = 0; s < chunk.size(); ++s)
        {
            // The first samples ever read seed the RMS, so it does not rise from silence.
            if (! started)
            {
                auto sum = 0.0;
                for (int i = 0; i < n; ++i)
                    sum += (double) chunk[s][(size_t) i] * chunk[s][(size_t) i];
                meanSquare[s] = sum / n;
            }
            auto ms = meanSquare[s];
            auto p = largest[s];
            for (int i = 0; i < n; ++i)
            {
                const auto x = chunk[s][(size_t) i];
                p = std::max (p, std::abs (x));
                ms = keep * ms + (1.0 - keep) * (double) x * x;
            }
            meanSquare[s] = ms;
            largest[s] = p;
        }
        started = true;
    }

    for (size_t s = 0; s < chunk.size(); ++s)
    {
        peak[s] = largest[s] > 0.0f ? std::max (20.0f * std::log10 (largest[s]), floorDb) : floorDb;
        rms[s] = dbOf (meanSquare[s], floorDb);
    }
    return true;
}

} // namespace halite::dsp
