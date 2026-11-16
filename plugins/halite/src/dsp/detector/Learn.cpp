#include "dsp/detector/Learn.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace halite::dsp
{

static_assert (std::atomic<double>::is_always_lock_free, "the audio thread writes a capture's hop length");

int LevelHistogram::binOf (float db) noexcept
{
    if (! (db > lowDb))
        return 0;
    return std::min (numBins - 1, (int) ((db - lowDb) / binDb));
}

void LevelHistogram::add (int bin) noexcept
{
    auto& b = bins[(size_t) bin];
    b.store (b.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    count.store (count.load (std::memory_order_relaxed) + 1, std::memory_order_release);
}

void LevelHistogram::remove (int bin) noexcept
{
    auto& b = bins[(size_t) bin];
    if (const auto n = b.load (std::memory_order_relaxed); n > 0)
    {
        b.store (n - 1, std::memory_order_relaxed);
        count.store (count.load (std::memory_order_relaxed) - 1, std::memory_order_release);
    }
}

void LevelHistogram::clear() noexcept
{
    for (auto& b : bins)
        b.store (0, std::memory_order_relaxed);
    count.store (0, std::memory_order_release);
}

void LevelHistogram::read (Counts& out) const noexcept
{
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = bins[i].load (std::memory_order_relaxed);
}

float histogramLevelDb (const LevelHistogram::Counts& counts, std::uint32_t total, double share) noexcept
{
    std::uint64_t sum = 0;
    for (const auto c : counts)
        sum += c;
    // A reader may catch the writer between a bin and the total: the bins decide.
    if (sum == 0 || total == 0)
        return std::numeric_limits<float>::quiet_NaN();

    const auto target = std::clamp (share, 0.0, 1.0) * (double) sum;
    auto below = 0.0;
    for (int i = 0; i < LevelHistogram::numBins; ++i)
    {
        const auto c = (double) counts[(size_t) i];
        if (c > 0.0 && below + c >= target)
            return LevelHistogram::lowEdgeOf (i) + LevelHistogram::binDb * (float) ((target - below) / c);
        below += c;
    }
    return LevelHistogram::lowEdgeOf (LevelHistogram::numBins);
}

LearnResult learnThresholds (const LevelHistogram::Counts& esses, std::uint32_t numEsses, const LevelHistogram::Counts& voiced,
                             std::uint32_t numVoiced, bool harshOn, double hopSeconds, const LearnTuning& t) noexcept
{
    LearnResult r;
    r.esses = (int) numEsses;
    if (r.esses >= t.minEsses)
    {
        const auto median = histogramLevelDb (esses, numEsses, 0.5);
        const auto quiet = histogramLevelDb (esses, numEsses, t.quietShare);
        r.essDb = std::max (median - t.medianReductionDb, quiet);
        r.essSet = std::isfinite (r.essDb);
    }

    r.voicedSeconds = (double) numVoiced * hopSeconds;
    if (harshOn && numVoiced > 0 && r.voicedSeconds >= t.minVoicedSeconds)
    {
        r.harshDb = histogramLevelDb (voiced, numVoiced, 1.0 - t.harshShare);
        r.harshSet = std::isfinite (r.harshDb);
    }
    return r;
}

void EssCandidates::prepare (double hopSeconds, const LearnTuning& t) noexcept
{
    hangoverHops = std::max (1, (int) std::lround (t.hangoverMs * 0.001 / hopSeconds));
    minLengthHops = std::max (1, (int) std::lround (t.minLengthMs * 0.001 / hopSeconds));
    reset();
}

void EssCandidates::reset() noexcept
{
    active = false;
    peak = 0.0f;
    lowest = silentDb;
    length = below = 0;
}

bool EssCandidates::push (float levelDb, float floorDb, float splitDb, float& peakDb) noexcept
{
    const auto split = splitDb > 0.0f;
    const auto over = levelDb > floorDb && (active ? ! split || levelDb > peak - splitDb : ! split || levelDb >= lowest + splitDb);
    if (! active)
        lowest = std::min (lowest, levelDb);

    if (over)
    {
        if (! active)
        {
            active = true;
            peak = levelDb;
            length = 0;
        }
        // The length runs from the first hop over to the last, the dips under the floor included.
        length += below + 1;
        below = 0;
        peak = std::max (peak, levelDb);
        return false;
    }

    if (! active || ++below <= hangoverHops)
        return false;

    active = false;
    below = 0;
    lowest = levelDb;
    peakDb = peak;
    return length >= minLengthHops;
}

bool EssCandidates::flush (float& peakDb) noexcept
{
    const auto counts = active && length >= minLengthHops;
    peakDb = peak;
    reset();
    return counts;
}

void Learn::prepare (double sampleRate, const LearnTuning& t)
{
    const auto newRate = std::abs (sampleRate - preparedRate) > 1.0e-3;
    preparedRate = sampleRate;
    tuning = t;
    hopLength = Detector::hopSize / sampleRate;
    captureHopsTotal = std::max (1, (int) std::lround (t.captureSeconds / hopLength));
    liveHops = std::max<std::int64_t> (1, std::llround (t.liveSeconds / hopLength));
    candidates.prepare (hopLength, t);
    // More candidates than this in 60 s (68 a second) push the oldest out early.
    if (liveQueue.empty())
        liveQueue.assign (4096, {});
    reset();
    if (! newRate)
        return;

    // The request stays unserved, so the next hop starts its capture again.
    if (phase() == Phase::capturing)
    {
        state.store ((int) Phase::idle, std::memory_order_release);
        served.fetch_sub (1, std::memory_order_acq_rel);
    }
    liveEsses.clear();
    liveHead = liveSize = 0;
    hopCount = 0;
    detection = -1;
}

void Learn::reset() noexcept
{
    candidates.reset();
}

float Learn::progress() const noexcept
{
    switch (phase())
    {
        case Phase::idle:       return 0.0f;
        case Phase::captured:   return 1.0f;
        case Phase::capturing:  break;
    }
    return std::min (1.0f, (float) captureHops.load (std::memory_order_relaxed) / (float) captureHopsTotal);
}

float Learn::essLevelDb (const DetectorFrame& f) noexcept
{
    auto level = f.decisions[0].sibilanceDb;
    for (int r = 1; r < f.numReductions; ++r)
        level = std::max (level, f.decisions[(size_t) r].sibilanceDb);
    return level;
}

float Learn::voicedHarshDb (const DetectorFrame& f, const DetectorSettings& s, const LearnTuning& t) noexcept
{
    if (! s.harshOn)
        return std::numeric_limits<float>::quiet_NaN();

    // The path the Harsh band follows when linked: the louder.
    const auto p = f.numPaths == 2 && f.paths[1].harshDb > f.paths[0].harshDb ? 1 : 0;
    const auto& path = f.paths[(size_t) p];
    // Not the sibilance candidate flag: a belted vowel's band reads about 10 dB under the voice and
    // holds the body, and it is the frame Harsh is for. S over the floor marks an ess instead.
    const auto voice = s.detection == Detection::voice;
    const auto voiced = path.noise < t.voicedNoise && path.harshNoise < t.voicedNoise && ! path.bodyFloored && path.harshDb > silentDb
                        && (! voice || essLevelDb (f) <= t.floorDb);
    if (! voiced)
        return std::numeric_limits<float>::quiet_NaN();
    return voice ? path.harshRelativeDb : path.harshDb;
}

void Learn::startCapture (const DetectorSettings& s) noexcept
{
    captureEsses.clear();
    captureVoiced.clear();
    captureHops.store (0, std::memory_order_relaxed);
    captureDetection.store ((int) s.detection, std::memory_order_relaxed);
    captureHarsh.store (s.harshOn, std::memory_order_relaxed);
    captureHop.store (hopLength, std::memory_order_relaxed);
    openInCapture = false;
    state.store ((int) Phase::capturing, std::memory_order_release);
}

void Learn::addLive (int bin) noexcept
{
    const auto capacity = liveQueue.size();
    if (liveSize == capacity)
    {
        liveEsses.remove (liveQueue[liveHead].bin);
        liveHead = (liveHead + 1) % capacity;
        --liveSize;
    }
    liveQueue[(liveHead + liveSize) % capacity] = { hopCount, bin };
    ++liveSize;
    liveEsses.add (bin);
}

void Learn::expireLive() noexcept
{
    while (liveSize > 0 && hopCount - liveQueue[liveHead].hop >= liveHops)
    {
        liveEsses.remove (liveQueue[liveHead].bin);
        liveHead = (liveHead + 1) % liveQueue.size();
        --liveSize;
    }
}

void Learn::hop (const DetectorFrame& f, const DetectorSettings& s) noexcept
{
    // The capture starts before its id is published: a reader that sees the id then never sees the
    // previous capture's phase.
    if (const auto r = requests.load (std::memory_order_acquire); r != served.load (std::memory_order_relaxed))
    {
        startCapture (s);
        served.store (r, std::memory_order_release);
    }

    // A level in another unit: the live histogram starts again, and so does a capture.
    if ((int) s.detection != detection)
    {
        detection = (int) s.detection;
        candidates.reset();
        liveEsses.clear();
        liveHead = liveSize = 0;
        if (phase() == Phase::capturing)
            startCapture (s);
    }

    ++hopCount;
    expireLive();

    const auto capturing = phase() == Phase::capturing;
    const auto voice = s.detection == Detection::voice;
    const auto wasOpen = candidates.open();
    auto peak = 0.0f;
    const auto ended = candidates.push (essLevelDb (f), voice ? tuning.floorDb : tuning.floorAbsDb, voice ? 0.0f : tuning.splitAbsDb, peak);
    if (! wasOpen && candidates.open())
        openInCapture = capturing;
    if (ended)
    {
        const auto bin = LevelHistogram::binOf (peak);
        addLive (bin);
        if (capturing && openInCapture)
            captureEsses.add (bin);
    }

    if (! capturing)
        return;

    if (const auto harsh = voicedHarshDb (f, s, tuning); std::isfinite (harsh))
        captureVoiced.add (LevelHistogram::binOf (harsh));

    const auto done = captureHops.load (std::memory_order_relaxed) + 1;
    captureHops.store (done, std::memory_order_relaxed);
    if (done < captureHopsTotal)
        return;

    // An ess begun in the capture and still open when it stops counts with its peak so far; the
    // live histogram takes it when it ends.
    if (auto open = candidates; openInCapture && open.flush (peak))
        captureEsses.add (LevelHistogram::binOf (peak));
    captureHarsh.store (s.harshOn, std::memory_order_relaxed);
    state.store ((int) Phase::captured, std::memory_order_release);
}

} // namespace halite::dsp
