#include "dsp/feed/CutCurves.h"

#include "dsp/shapes/Shapes.h"
#include "dsp/shapes/split/SplitFilter.h"

#include <algorithm>
#include <cmath>

namespace halite::dsp
{

CutSettings CutSettings::from (const DetectorSettings& s, Shape running, double sampleRate) noexcept
{
    CutSettings c;
    c.shape = running;
    c.bands = Detector::bandsFor (s, sampleRate);
    c.essKind = SplitFilter::kindFor (std::max (s.detectLowHz, s.detectHighHz), sampleRate);
    return c;
}

namespace cutCurves
{
    namespace
    {
        constexpr float smallestGain = 1.0e-6f;    // −120 dB

        float dbOfGain (float g) noexcept { return 20.0f * std::log10 (std::clamp (g, smallestGain, 1.0f)); }

        void split (const splitDesign::Prototype& p, SplitKind kind, double low, double high, float reductionDb, const float* hz, int count,
                    float* outDb) noexcept
        {
            const auto depth = std::clamp ((double) reductionDb, 0.0, p.deepestDb);
            if (depth <= 0.0)
            {
                std::fill (outDb, outDb + count, 0.0f);
                return;
            }
            const auto gain = std::pow (10.0, -depth / 20.0);
            for (int i = 0; i < count; ++i)
            {
                const auto power = splitDesign::analogPower (p, kind, low, high, std::max ((double) hz[i], 1.0), gain);
                outDb[i] = std::min (0.0f, (float) (10.0 * std::log10 (std::max (power, 1.0e-12))));
            }
        }

        bool spectralShown (const CutSettings& s, const SpectralView* v) noexcept
        {
            return s.shape == Shape::spectral && v != nullptr && v->active && v->bins > 0;
        }
    }

    void fromBins (const float* gains, int bins, double binHz, const float* hz, int count, float* outDb) noexcept
    {
        if (bins <= 0 || ! (binHz > 0.0))
        {
            std::fill (outDb, outDb + count, 0.0f);
            return;
        }

        const auto gainAt = [gains, bins] (int k) { return k >= 0 && k < bins ? gains[k] : 1.0f; };
        for (int i = 0; i < count; ++i)
        {
            // The point stands for the frequencies halfway (on the log axis) to its neighbours.
            const auto f = (double) hz[i];
            const auto below = i > 0 ? std::sqrt (f * hz[i - 1]) : f;
            const auto above = i + 1 < count ? std::sqrt (f * hz[i + 1]) : f;
            const auto first = (int) std::ceil (below / binHz), last = (int) std::floor (above / binHz);

            const auto x = f / binHz;
            const auto k = (int) std::floor (x);
            const auto u = (float) (x - k);
            auto db = (1.0f - u) * dbOfGain (gainAt (k)) + u * dbOfGain (gainAt (k + 1));
            for (int b = std::max (first, 0); b <= std::min (last, bins - 1); ++b)
                db = std::min (db, dbOfGain (gains[b]));
            outDb[i] = std::min (db, 0.0f);
        }
    }

    void ess (const CutSettings& s, float reductionDb, const SpectralView* spectral, const float* hz, int count, float* outDb) noexcept
    {
        switch (s.shape)
        {
            case Shape::spectral:
                if (spectralShown (s, spectral))
                    fromBins (spectral->essGain.data(), spectral->bins, spectral->binHz, hz, count, outDb);
                else
                    std::fill (outDb, outDb + count, 0.0f);
                return;
            case Shape::wide:
                std::fill (outDb, outDb + count, -std::clamp (reductionDb, 0.0f, (float) essDeepestCutDb));
                return;
            case Shape::split:
                break;
        }
        split (splitDesign::ess(), s.essKind, s.bands.essLow, s.bands.essHigh, reductionDb, hz, count, outDb);
    }

    void harsh (const CutSettings& s, float reductionDb, const SpectralView* spectral, const float* hz, int count, float* outDb) noexcept
    {
        if (s.shape == Shape::spectral)
        {
            if (spectralShown (s, spectral))
                fromBins (spectral->harshGain.data(), spectral->bins, spectral->binHz, hz, count, outDb);
            else
                std::fill (outDb, outDb + count, 0.0f);
            return;
        }
        split (splitDesign::harsh(), SplitKind::bell, s.bands.harshLow, s.bands.harshHigh, reductionDb, hz, count, outDb);
    }
}

} // namespace halite::dsp
