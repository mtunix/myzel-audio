#include "dsp/feed/DisplayFeed.h"

#include "common/dsp/FloatCompare.h"

#include "dsp/shapes/spectral/SpectralShape.h"

#include <algorithm>
#include <cmath>

namespace halite::dsp
{

namespace
{
    std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
    {
        const auto q = a / b;
        return q * b > a ? q - 1 : q;
    }

    // HaliteChain::frameSize () reaches 16384 points only above 768 kHz: the stage holds a frame of hops and a chunk.
    constexpr int largestFrame = 16384;
}

DisplayFeed::DisplayFeed()
{
    auto size = 1;
    while (size < largestFrame / Detector::hopSize + 64)
        size <<= 1;
    stage.assign ((size_t) size, Staged {});
    stageMask = size - 1;

    for (auto* v : { &essCut, &harshCut, &essShown, &harshShown, &target, &reference })
        v->assign ((size_t) SpectralSnapshot::maxBins, 1.0f);
    scratch.assign ((size_t) SpectralSnapshot::maxBins + 1, 0.0);
}

void DisplayFeed::prepare (double sampleRate, int frameSize)
{
    std::fill (stage.begin(), stage.end(), Staged {});
    held = {};

    bins = std::clamp (frameSize / 2 + 1, 1, SpectralSnapshot::maxBins);
    binHz = sampleRate / std::max (frameSize, 1);
    publishInterval = std::max (1, (int) std::lround (sampleRate / publishHz));
    sincePublish = carried = 0;
    std::fill (essCut.begin(), essCut.end(), 1.0f);
    std::fill (harshCut.begin(), harshCut.end(), 1.0f);

    // At the same rate everything a reader holds stays valid: the history and the clock run on.
    if (myzel::dsp::isExactly (sampleRate, preparedRate))
        return;

    preparedRate = sampleRate;
    recordHops = std::max (1, (int) std::ceil (sampleRate / 50000.0));
    elapsedClock = 0;
    recordOpen = false;
    currentRecord = 0;
    inputPeak = outputPeak = removedPeak = 0.0f;
    outputSquares = {};
    recordFill = 0;
    records.restart();
    spectralShown = false;
    snapshot.publish (false, bins, binHz, essCut.data(), harshCut.data(), nullptr);

    rate.store (sampleRate, std::memory_order_relaxed);
    recordLength.store (recordHops * Detector::hopSize, std::memory_order_relaxed);
    restarts.fetch_add (1, std::memory_order_release);
}

std::int64_t DisplayFeed::recordOf (std::int64_t elapsed) const noexcept
{
    return floorDiv (elapsed, recordSamples());
}

void DisplayFeed::hop (const DetectorFrame& f, std::int64_t offset) noexcept
{
    // The reductions belong to the input hop ending at reductionPosition; its centre picks the
    // feed's hop, so consecutive detector hops fill consecutive feed hops whatever the offset.
    const auto h = floorDiv (f.reductionPosition - Detector::hopSize / 2 + offset, Detector::hopSize);

    Staged s;
    s.hop = h;
    for (int r = 0; r < std::clamp (f.numReductions, 1, 2); ++r)
    {
        const auto& red = f.reductions[(size_t) r];
        s.sibilanceDb = std::max (s.sibilanceDb, red.sibilanceDb);
        s.essDb = std::max (s.essDb, red.essDb);
        s.harshDb = std::max (s.harshDb, red.harshDb);
    }
    stage[(size_t) (h & stageMask)] = s;
}

void DisplayFeed::stamp (EssEvent& e, std::int64_t offset, bool playing) const noexcept
{
    e.elapsed = e.start + offset;
    e.songPosition = playing;
    e.seconds = preparedRate > 0.0 ? (double) (playing ? e.start : e.elapsed) / preparedRate : 0.0;
}

HistoryRecord DisplayFeed::recordFor (std::int64_t record) noexcept
{
    // The detector's values for the record's hops, the largest of each; a record none of whose hops
    // was staged (Live, at a chunk's end; a restart) holds the last values seen.
    auto any = false;
    Staged values;
    for (auto h = record * recordHops; h < (record + 1) * recordHops; ++h)
    {
        const auto& s = stage[(size_t) (h & stageMask)];
        if (s.hop != h)
            continue;
        values.sibilanceDb = std::max (values.sibilanceDb, s.sibilanceDb);
        values.essDb = std::max (values.essDb, s.essDb);
        values.harshDb = std::max (values.harshDb, s.harshDb);
        any = true;
    }
    if (any)
        held = values;

    HistoryRecord r;
    r.sibilanceDb = held.sibilanceDb;
    r.essDb = held.essDb;
    r.harshDb = held.harshDb;
    return r;
}

void DisplayFeed::close() noexcept
{
    auto r = recordFor (currentRecord);
    r.inputPeak = inputPeak;
    r.outputPeak = outputPeak;
    r.removedPeak = removedPeak;
    r.outputRms = recordFill > 0 ? (float) std::sqrt (std::max (outputSquares[0], outputSquares[1]) / recordFill) : 0.0f;
    records.push (r);
    inputPeak = outputPeak = removedPeak = 0.0f;
    outputSquares = {};
    recordFill = 0;
}

void DisplayFeed::audio (const float* const* dry, const float* const* removed, const float* const* out, int numChannels, int n,
                         int latency) noexcept
{
    if (n <= 0 || numChannels <= 0)
        return;

    const auto channels = std::min (numChannels, 2);

    fifo.begin (n);
    for (int c = 0; c < 2; ++c)
    {
        const auto source = std::min (c, channels - 1);
        fifo.write (SampleFifo::inputStream (c), dry[source], n);
        fifo.write (SampleFifo::outputStream (c), out[source], n);
    }
    fifo.end (n);

    // The audio leaving now went in `latency` samples ago; before the first input there is none.
    const auto length = (std::int64_t) recordHops * Detector::hopSize;
    const auto contentStart = elapsedClock - latency;
    auto i = contentStart < 0 ? (int) std::min<std::int64_t> (n, -contentStart) : 0;

    while (i < n)
    {
        const auto t = contentStart + i;
        const auto record = t / length;
        if (! recordOpen)
        {
            recordOpen = true;
            currentRecord = record;
        }
        else if (record > currentRecord)
        {
            // A shorter latency skips input: the records it skips hold no audio.
            close();
            for (auto skipped = std::max (currentRecord + 1, record - HopHistory::capacity / 2); skipped < record; ++skipped)
            {
                currentRecord = skipped;
                close();
            }
            currentRecord = record;
        }

        // To the end of the open record; after a longer latency the input repeats, and joins it until it passes.
        const auto end = (int) std::min<std::int64_t> (n, i + (currentRecord + 1) * length - t);
        for (int c = 0; c < channels; ++c)
        {
            const auto* d = dry[c];
            const auto* r = removed[c];
            auto squares = 0.0;
            for (int j = i; j < end; ++j)
            {
                const auto o = d[j] - r[j];
                inputPeak = std::max (inputPeak, std::abs (d[j]));
                outputPeak = std::max (outputPeak, std::abs (o));
                removedPeak = std::max (removedPeak, std::abs (r[j]));
                squares += (double) o * o;
            }
            outputSquares[(size_t) c] += squares;
        }
        recordFill += end - i;
        i = end;
    }

    elapsedClock += n;
}

void DisplayFeed::spectral (const SpectralShape* running, Detection detection, int n) noexcept
{
    if (running != nullptr)
    {
        // The deepest gain of each bin since the last publication; outside a band every gain is 1.
        for (int a = 0; a < running->numAnalyses(); ++a)
        {
            const auto& analysis = running->analysis (a);
            if (analysis.numBins() != bins)
                continue;
            const auto fold = [] (const SpectralAnalysis::Range& range, const float* gains, std::vector<float>& cut)
            {
                for (int k = range.first; k <= range.last; ++k)
                    cut[(size_t) k] = std::min (cut[(size_t) k], gains[k]);
            };
            if (analysis.essActive())
                fold (analysis.essBins(), analysis.essGains(), essCut);
            if (analysis.harshActive())
                fold (analysis.harshBins(), analysis.harshGains(), harshCut);
        }
    }

    sincePublish += n;
    if (sincePublish < publishInterval)
        return;
    sincePublish = 0;

    if (running == nullptr)
    {
        if (spectralShown)
        {
            std::fill (essCut.begin(), essCut.end(), 1.0f);
            std::fill (harshCut.begin(), harshCut.end(), 1.0f);
            snapshot.publish (false, bins, binHz, essCut.data(), harshCut.data(), nullptr);
            spectralShown = false;
        }
        return;
    }

    // What the reader has not seen yet joins this publication, for a while.
    const auto carry = spectralShown && ! snapshot.acknowledged (snapshot.lastSequence()) && carried < carryLimit;
    carried = carry ? carried + 1 : 0;
    for (int k = 0; k < bins; ++k)
    {
        essShown[(size_t) k] = carry ? std::min (essCut[(size_t) k], essShown[(size_t) k]) : essCut[(size_t) k];
        harshShown[(size_t) k] = carry ? std::min (harshCut[(size_t) k], harshShown[(size_t) k]) : harshCut[(size_t) k];
    }

    // The target per channel: each analysis's signals summed, the analyses averaged.
    std::fill (target.begin(), target.begin() + bins, 0.0f);
    auto analyses = 0;
    for (int a = 0; a < running->numAnalyses(); ++a)
    {
        const auto& analysis = running->analysis (a);
        if (analysis.numBins() != bins)
            continue;
        analysis.referenceLevels (detection, scratch.data(), reference.data());
        for (int k = 0; k < bins; ++k)
            target[(size_t) k] += reference[(size_t) k];
        ++analyses;
    }
    const auto scale = analyses > 0 ? 1.0f / (float) (analyses * running->signalsPerAnalysis()) : 0.0f;
    for (int k = 0; k < bins; ++k)
        target[(size_t) k] *= scale;

    snapshot.publish (true, bins, binHz, essShown.data(), harshShown.data(), analyses > 0 ? target.data() : nullptr);
    spectralShown = true;
    std::fill (essCut.begin(), essCut.begin() + bins, 1.0f);
    std::fill (harshCut.begin(), harshCut.begin() + bins, 1.0f);
}

} // namespace halite::dsp
