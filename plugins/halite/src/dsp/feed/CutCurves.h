#pragma once

#include "dsp/Modes.h"
#include "dsp/detector/Detector.h"
#include "dsp/detector/DetectorSettings.h"
#include "dsp/feed/SpectralSnapshot.h"
#include "dsp/shapes/split/SplitDesign.h"

namespace halite::dsp
{

/** What the spectrum's cut curves are drawn from, besides the reductions (src/dsp/feed/NOTES.md). */
struct CutSettings
{
    Shape shape = Shape::split;             // the shape that runs (ChainStatus::shape: Live runs Split for Spectral)
    Detector::Bands bands;                  // as the detectors and the shapes use them: sorted, widened, under 0.45 fs
    SplitKind essKind = SplitKind::bell;    // Split's Ess cut: a high shelf once Detect High reaches 0.4 fs

    /** From the parameters as the audio thread reads them (HaliteProcessor::readSettings) and the running shape. */
    static CutSettings from (const DetectorSettings&, Shape running, double sampleRate) noexcept;
};

/**
    Each band's cut in dB over frequency, for the spectrum (PLAN.md, Parts, Spectrum), on the
    message thread. Split: the analog prototype of the steep cut at the given reduction (the digital
    cut is within 0.1 dB of it to 16 dB, 0.46 dB near 0.45 fs at worst; split/NOTES.md). Wide: the
    Ess band flat over every frequency, the Harsh band Split's bell (as the shapes do). Spectral:
    the published per-bin gains, as they are, whatever `reductionDb` says; where a display point
    spans several bins, the deepest of them. Every value ≤ 0 dB (0: no cut). No allocation.
*/
namespace cutCurves
{
    /** `reductionDb`: the Ess reduction to draw (≥ 0, the history's records); `spectral`: the snapshot read, or null. */
    void ess (const CutSettings&, float reductionDb, const SpectralView* spectral, const float* hz, int count, float* outDb) noexcept;

    void harsh (const CutSettings&, float reductionDb, const SpectralView* spectral, const float* hz, int count, float* outDb) noexcept;

    /** Spectral's per-bin gains at display points (`gains`: one band's from a SpectralView), in dB. */
    void fromBins (const float* gains, int bins, double binHz, const float* hz, int count, float* outDb) noexcept;
}

} // namespace halite::dsp
