// What BasaltMeasure has transcribed of each model's NOTES (src/dsp/models/<m>/NOTES.md, Gate
// table): the ModelSpec every gate reads, and the rows a model adds of its own: the VCA's and the
// FET's threshold coupling, VCA Auto and RMS times, FET All's lag, the Opto's history and depth,
// Vari-mu's rising ratio and Auto tiers, Upward's lift ballistics, Auto, Floor and shape. Every
// expectation comes from the model's own transcription, tests/models/<m>/Reference.h, written from
// its NOTES and never from the engine.

#include "support/GateKit.h"

#include "common/dsp/FloatCompare.h"

#include "models/fet/Reference.h"
#include "models/opto/Reference.h"
#include "models/upward/Reference.h"
#include "models/varimu/Reference.h"
#include "models/vca/Reference.h"
#include "support/Reference.h"

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>

namespace basalt::measure
{

using namespace kit;

namespace
{
    namespace vcaRef = ::basalt::reference::vca;
    namespace fetRef = ::basalt::reference::fet;
    namespace optoRef = reference::opto;
    namespace muRef = reference::varimu;
    namespace upRef = reference::upward;

    constexpr double inf = infiniteRatio;
    constexpr double stepThresholdDb = -30.0;
    const double e = std::exp (1.0);

    double inverseOf (const Settings& s) { return reference::inverseRatioOf (s.ratio); }

    /** The carrier reads 3.01 dB over its amplitude on a mean-square detector, a sine its peak on every detector. */
    double meanSquareReading (Signal signal, double amplitudeDb) { return signal == Signal::carrier ? amplitudeDb + carrierRmsOffsetDb : amplitudeDb; }

    upRef::Curve upwardCurve (const Settings& s) { return { s.thresholdDb, inverseOf (s), s.kneeDb, s.rangeDb, s.floorDb }; }

    /** The core's step grid (dynamics/NOTES.md, Gate table), the ∞ step as `top`. */
    std::vector<StepCase> coreSteps (bool quick, double top)
    {
        if (quick)
            return { { 0.02, 50.0, 4.0 }, { 0.1, 150.0, 20.0 }, { 1.0, 150.0, 2.0 }, { 10.0, 150.0, 4.0 },
                     { 10.0, 5.0, 4.0 }, { 100.0, 500.0, top }, { 300.0, 50.0, 4.0 } };
        std::vector<StepCase> c;
        for (const auto ratio : { 2.0, 4.0, 20.0, top })
            for (const auto attack : { 0.02, 0.1, 1.0, 10.0, 100.0, 300.0 })
                for (const auto release : { 5.0, 50.0, 150.0, 500.0, 5000.0 })
                    c.push_back ({ attack, release, ratio });
        return c;
    }

    StaticGrid coreGrid() { return { { -60.0, -30.0, -10.0 }, { 1.5, 2.0, 4.0, 10.0, 20.0, inf }, { 0.0, 6.0, 24.0 }, { 40.0, 10.0 } }; }

    /**
        A feedback loop settles its attack in Attack·R·G' (dynamics/NOTES.md, Results: Knee region):
        slower inside the knee. The staircase holds each level 0.1 s plus six of those, at most 3.5 s.
    */
    std::function<double (const Settings&, Signal, double, double)> feedbackSettle (const std::function<double (const Settings&, Signal, double)>& curve,
                                                                                     double cap)
    {
        return [curve, cap] (const Settings& s, Signal signal, double, double to)
        {
            const auto base = std::max (0.1, 12.0 * s.attackMs / 1000.0);
            if (! (curve (s, signal, to) > 0.0))
                return base;
            const auto slope = 1.0 - (curve (s, signal, to + 0.01) - curve (s, signal, to - 0.01)) / 0.02;
            const auto ratio = std::min (std::isinf (s.ratio) ? cap : s.ratio, cap);
            return std::min (base + 6.0 * std::max (s.attackMs, 0.1) / 1000.0 * ratio * std::max (slope, 0.0), 3.5);
        };
    }

    //==============================================================================
    /** The Opto's τ_on per setting (opto/NOTES.md, Attack), solved once on the transcription and kept. */
    double optoAttackConstantMs (double attackMs, double inverseRatio, double kneeDb)
    {
        static std::mutex lock;
        static std::map<std::tuple<double, double, double>, double> solved;
        const auto key = std::make_tuple (attackMs, inverseRatio, kneeDb);
        {
            const std::scoped_lock l (lock);
            if (const auto it = solved.find (key); it != solved.end())
                return it->second;
        }
        const auto tau = optoRef::solveAttackConstantMs (attackMs, inverseRatio, kneeDb, 48000.0);
        const std::scoped_lock l (lock);
        return solved[key] = tau;
    }

    /** The Opto's two cell constants for a static setting: τ_on rising, τ_ff falling (Release 1 s). */
    std::pair<double, double> optoConstants (const Settings& s)
    {
        const auto q = std::min (inverseOf (s), 1.0 - optoRef::slopeShareFloor);
        return { optoAttackConstantMs (std::max (s.attackMs, optoRef::attackFloorMs), q, s.kneeDb),
                 optoRef::releaseConstantMs (s.releaseMs, s.attackMs, inverseOf (s), s.kneeDb) };
    }

    //==============================================================================
    std::array<ModelSpec, numOfferedModels> makeSpecs()
    {
        std::array<ModelSpec, numOfferedModels> specs {};
        for (int i = 0; i < numOfferedModels; ++i)
            specs[(size_t) i].model = static_cast<Model> (i);

        // Clean (clean/NOTES.md): eq. 4 on the blend of the peak and RMS reductions.
        auto& clean = specs[(size_t) Model::clean];
        clean.staticAttacksMs = { 10.0, 0.1 };  // the blend at half RMS, and pure peak
        clean.staticFull = coreGrid();
        clean.staticQuick = { { -60.0, -10.0 }, { 1.5, 4.0, inf }, { 0.0, 6.0, 24.0 }, { 40.0, 10.0 } };
        clean.steadyReduction = [] (const Settings& st, Signal signal, double amplitudeDb)
        {
            const auto inverse = inverseOf (st);
            const auto peak = reference::reduction (amplitudeDb, st.thresholdDb, inverse, st.kneeDb);
            const auto b = reference::cleanBlend (st.attackMs);
            const auto r = signal == Signal::sine ? peak
                                                  : (1.0 - b) * peak + b * reference::reduction (amplitudeDb + carrierRmsOffsetDb, st.thresholdDb, inverse, st.kneeDb);
            return std::min (r, st.rangeDb);
        };
        clean.steps = [] (bool quick) { return coreSteps (quick, inf); };
        clean.expectedReleaseMs = [] (const Settings& st) { return std::max (st.releaseMs, reference::cleanReleaseFloor (st.attackMs, inverseOf (st))); };
        clean.distortionGated = [] (const Settings& st) { return st.attackMs >= 10.0 && st.releaseMs >= 50.0; };

        // VCA (vca/NOTES.md, Gates): eq. 4 at T − 6/R on each detector's own reading; Peak's times are the core's.
        auto& vca = specs[(size_t) Model::vca];
        vca.detectorSwitch = true;
        vca.staticFull = coreGrid();
        vca.staticQuick = { { -60.0, -10.0 }, { 1.5, 4.0, inf }, { 0.0, 24.0 }, { 40.0, 10.0 } };
        vca.steadyReduction = [] (const Settings& st, Signal signal, double amplitudeDb)
        {
            const auto reading = st.vcaDetector == VcaDetector::rms ? meanSquareReading (signal, amplitudeDb) : amplitudeDb;
            return vcaRef::staticReductionDb (reading, st.thresholdDb, inverseOf (st), st.kneeDb, st.rangeDb);
        };
        vca.steps = [] (bool quick) { return coreSteps (quick, inf); };
        vca.expectedReleaseMs = [] (const Settings& st)
        {
            return st.vcaDetector == VcaDetector::rms ? vcaRef::rmsReleasePer10DbMs (st.attackMs, st.releaseMs, inverseOf (st))
                                                      : vcaRef::peakReleaseMs (st.attackMs, st.releaseMs);
        };
        vca.distortionGated = [] (const Settings& st)
        { return st.vcaDetector == VcaDetector::rms ? st.attackMs >= 100.0 : st.attackMs >= 10.0 && st.releaseMs >= 2.0 * st.attackMs; };

        // FET (fet/NOTES.md, Gate table): the loop on eq. 4 at T + ΔT(R), capped at 20, All at the 4:1 threshold with a key lag.
        auto& fet = specs[(size_t) Model::fet];
        fet.feedback = fet.lowFrequencyGated = true;
        fet.budgetPercent = 0.5;
        fet.staticFull = { { -60.0, -30.0, -10.0 }, { 1.5, 2.0, 4.0, 8.0, 12.0, 20.0, inf }, { 0.0, 6.0, 24.0 }, { 40.0, 10.0 } };
        fet.staticQuick = { { -30.0 }, { 1.5, 8.0, inf }, { 0.0, 24.0 }, { 40.0, 10.0 } };
        fet.steadyReduction = [] (const Settings& st, Signal, double amplitudeDb)
        { return fetRef::staticReductionDb (amplitudeDb, st.thresholdDb, st.ratio, st.kneeDb, st.rangeDb); };
        fet.stepSettleSeconds = feedbackSettle (fet.steadyReduction, fetRef::ratioCap);
        fet.steps = [] (bool quick)
        {
            if (quick)
                return std::vector<StepCase> { { 0.02, 50.0, 4.0 }, { 0.8, 50.0, 20.0 }, { 10.0, 300.0, inf }, { 100.0, 1100.0, 4.0 }, { 0.2, 5.0, 20.0 } };
            std::vector<StepCase> c;
            for (const auto ratio : fetRef::ballisticsRatios)
                for (const auto attack : fetRef::ballisticsAttacksMs)
                    for (const auto release : fetRef::ballisticsReleasesMs)
                        c.push_back ({ attack, release, ratio });
            return c;
        };
        fet.lagMs = [] (const Settings& st) { return fetRef::lagMs (st.ratio); };

        // Opto (opto/NOTES.md, Gate table): the cell's curve on the mean-square reading; Release is the time to 50 %.
        auto& opto = specs[(size_t) Model::opto];
        opto.budgetPercent = 0.5;
        opto.identityQuiet = 0.004;     // −48 dBFS: under T − 6w = −39.3 dB at Knee 24
        opto.staticAttacksMs = { 50.0 };
        opto.staticFull = coreGrid();
        opto.staticQuick = { { -30.0 }, { 2.0, 4.0, inf }, { 0.0, 6.0 }, { 40.0, 10.0 } };
        opto.steadyReduction = [] (const Settings& st, Signal signal, double amplitudeDb)
        { return optoRef::staticReduction (meanSquareReading (signal, amplitudeDb), st.thresholdDb, inverseOf (st), st.kneeDb, st.rangeDb); };
        opto.staticDescending = [] (const Settings& st)
        {
            const auto [rise, fall] = optoConstants (st);
            return fall < rise;
        };
        opto.stepSettleSeconds = [] (const Settings& st, Signal, double from, double to)
        {
            // optoRef::stepSettleS: 1 dB to within 0.005 dB on the constant that moves the cell; the
            // first step from silence takes five rising constants.
            const auto [rise, fall] = optoConstants (st);
            if (from < -150.0)
                return 5.0 * rise / 1000.0 + 0.1;
            const auto tau = to > from ? rise : fall;
            return 1.2 * optoRef::stepSettleS (tau, st.attackMs, inverseOf (st), st.kneeDb) + 0.05;
        };
        opto.steps = [] (bool quick)
        {
            std::vector<StepCase> c;
            const auto attacks = quick ? std::vector<double> { 0.5, 2.0, 10.0, 100.0 } : std::vector<double> { 0.02, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 100.0, 300.0 };
            for (const auto ratio : quick ? std::vector<double> { 4.0 } : std::vector<double> { 2.0, 4.0, 20.0 })
                for (const auto knee : quick ? std::vector<double> { 6.0 } : std::vector<double> { 0.0, 6.0 })
                    for (const auto attack : attacks)
                        c.push_back ({ attack, 150.0, ratio, knee, true, false });
            const auto releases = quick ? std::vector<double> { 60.0, 500.0 } : std::vector<double> { 30.0, 60.0, 150.0, 500.0, 2000.0, 5000.0 };
            for (const auto ratio : quick ? std::vector<double> { 4.0 } : std::vector<double> { 2.0, 4.0, 20.0 })
                for (const auto knee : quick ? std::vector<double> { 0.0, 6.0 } : std::vector<double> { 0.0, 6.0, 24.0 })
                    for (const auto release : releases)
                        c.push_back ({ 10.0, release, ratio, knee, false, true });
            return c;
        };
        opto.expectedAttackMs = [] (const Settings& st) { return optoRef::expectedAttackMs (st.attackMs); };
        opto.expectedReleaseMs = [] (const Settings& st) { return optoRef::expectedReleaseMs (st.releaseMs, st.attackMs, st.kneeDb); };
        opto.releaseFraction = 0.5;
        // D_final, which the attack is read against, settles on the cell's τ_on, far over Attack at
        // low ratios with a hard knee: eight of them leave it 10⁻³ of the way.
        opto.stepHoldSeconds = [] (const Settings& st) { return 8.0 * optoConstants (st).first / 1000.0 + 0.2; };
        opto.rangeAttackGated = false;
        opto.linksLevels = true;

        // Vari-mu (varimu/NOTES.md, Gate table): eq. 4 plus the rise to 20:1, the loop's attack calibrated.
        auto& mu = specs[(size_t) Model::variMu];
        mu.feedback = mu.lowFrequencyGated = mu.gateOvershoot = true;
        mu.budgetPercent = 0.5;
        mu.staticFull = { { -60.0, -30.0, -10.0 }, { 1.5, 2.0, 4.0, 6.0, 10.0, 20.0, inf }, { 0.0, 6.0, 12.0, 24.0 }, { 40.0, 10.0 } };
        mu.staticQuick = { { -30.0 }, { 1.5, 4.0, inf }, { 0.0, 12.0 }, { 40.0, 10.0 } };
        mu.staticLevels = [] (const Settings& st)
        {
            std::vector<double> l;
            for (auto x = st.thresholdDb - 20.0; x <= st.thresholdDb + 40.0 + 1.0e-9; x += 1.0)
                l.push_back (x);
            return l;
        };
        mu.steadyReduction = [] (const Settings& st, Signal, double amplitudeDb)
        { return muRef::staticReduction (amplitudeDb, st.thresholdDb, inverseOf (st), st.kneeDb, st.rangeDb); };
        mu.stepSettleSeconds = feedbackSettle (mu.steadyReduction, muRef::ratioCap);
        mu.steps = [] (bool quick)
        {
            std::vector<StepCase> c;
            if (quick)
            {
                for (const auto attack : { 0.1, 1.0, 50.0 })
                    c.push_back ({ attack, 150.0, 4.0, 6.0, true, false });
                c.push_back ({ 0.4, 150.0, 20.0, 0.0, true, false });
                c.push_back ({ 0.1, 150.0, 4.0, 6.0, false, false, 30.0 });
                for (const auto release : { 150.0, 2000.0 })
                    c.push_back ({ 1.0, release, 4.0, 0.0, false, true });
                return c;
            }
            for (const auto ratio : { 1.5, 4.0, 8.0, 20.0 })
                for (const auto knee : { 0.0, 6.0, 24.0 })
                    for (const auto attack : { 0.02, 0.1, 0.2, 0.4, 1.0, 10.0, 25.0, 50.0, 70.0, 300.0 })
                        c.push_back ({ attack, 150.0, ratio, knee, true, false });
            for (const auto depth : { 20.0, 30.0 })
                for (const auto attack : { 0.02, 0.1, 1.0, 10.0 })
                    c.push_back ({ attack, 150.0, 4.0, 6.0, false, false, depth });
            for (const auto ratio : { 4.0, 20.0 })
                for (const auto release : { 5.0, 50.0, 150.0, 500.0, 5000.0 })
                    c.push_back ({ 1.0, release, ratio, 0.0, false, true });
            return c;
        };
        mu.expectedAttackMs = [] (const Settings& st) { return muRef::expectedAttackMs (st.attackMs); };
        mu.attackTolerance = [] (const Settings& st) { return muRef::attackTolerance (st.attackMs); };

        // Upward (upward/NOTES.md, Gates): the lift on the mean-square reading, faded in over the 12 dB above Floor.
        auto& up = specs[(size_t) Model::upward];
        up.lifts = true;
        up.identityUnderOnset = false;
        up.lookaheadRows = false;
        up.distortionDepthDb = -6.0;
        up.staticFull = { { -50.0, -30.0, -10.0 }, { 1.5, 2.0, 4.0, 10.0, inf }, { 0.0, 6.0, 24.0 }, { 40.0, 10.0 }, { -90.0, -70.0, -50.0 } };
        up.staticQuick = { { -30.0 }, { 2.0, inf }, { 0.0, 24.0 }, { 40.0 }, { -70.0 } };
        up.staticLevels = [] (const Settings& st)
        {
            std::vector<double> l;
            // Amplitudes: from 14 dB under Floor, so the carrier too reads from more than 10 dB under it.
            for (auto x = st.floorDb - 14.0; x <= st.thresholdDb + 10.0 + 1.0e-9; x += 1.0)
                l.push_back (x);
            return l;
        };
        up.steadyReduction = [] (const Settings& st, Signal signal, double amplitudeDb)
        {
            // −0 never: no lift is exactly 0 (staticReduction's convention).
            const auto lift = upRef::lift (upwardCurve (st), meanSquareReading (signal, amplitudeDb));
            return lift > 0.0 ? -lift : 0.0;
        };
        up.stepSettleSeconds = [] (const Settings& st, Signal signal, double from, double to)
        {
            // A falling level brings the lift in at 10 dB per Release (or the detector's floor); a rising one takes it away on Attack.
            const auto curve = upwardCurve (st);
            const auto liftFrom = from < -150.0 ? 0.0 : upRef::lift (curve, meanSquareReading (signal, from));
            const auto liftTo = upRef::lift (curve, meanSquareReading (signal, to));
            const auto per10 = upRef::releasePer10DbMs (st.releaseMs, inverseOf (st)) / 1000.0;
            return std::max (0.1, 12.0 * st.attackMs / 1000.0) + 0.05 + 1.2 * std::max (0.0, liftTo - liftFrom) / 10.0 * per10;
        };

        return specs;
    }
}

const ModelSpec& specOf (Model m)
{
    static const auto specs = makeSpecs();
    return specs[(size_t) runningModel (m)];
}

double kit::vcaRmsThirdHarmonicDbc (double attackMs, double inverseRatio, double hz)
{
    return vcaRef::rmsThirdHarmonicDbc (attackMs, inverseRatio, hz);
}

//==============================================================================
namespace
{
    /** Silence, then `levels` (amplitudes in dBFS) for their durations (s), as one carrier key; returns where each segment starts. */
    Samples carrierSteps (const Options& o, const std::vector<std::pair<double, double>>& segments, std::vector<int>* starts = nullptr)
    {
        Samples key;
        for (const auto& [db, seconds] : segments)
        {
            if (starts != nullptr)
                starts->push_back ((int) key.size());
            const auto n = 2 * (samplesFor (seconds, o.rate) / 2);
            append (key, db < -150.0 ? silence (n) : carrier (n, gainOf (db)));
        }
        return key;
    }

    /** The time (s) from input sample n0 until r first crosses `target` going the given way, the core's convention. */
    double timeTo (const std::vector<double>& r, int n0, double target, bool rising, double rate)
    {
        return crossingSeconds (r, n0, target, rising, rate);
    }

    Row timeRow (const std::string& gate, const std::string& group, const std::string& item, double measuredS, double expectedS, double rate,
                 const std::string& note = {})
    {
        const auto tol = timeTolerance (expectedS, rate);
        return gated (gate, group, item, 1000.0 * measuredS, 1000.0 * expectedS, 1000.0 * (expectedS - tol), 1000.0 * (expectedS + tol), "ms",
                      format ("%.4f x expected", measuredS / expectedS) + (note.empty() ? "" : "; " + note));
    }

    /**
        The threshold coupling (vca/NOTES.md and fet/NOTES.md, Gates): the carrier on a 0.01 dB
        level sweep, hard knee, Attack 0.2 ms; the knee found by extrapolating the reduction between
        1 and 8 dB linearly to 0, ±0.05 dB of where the NOTES put it.
    */
    Row couplingRow (const Options& o, Model model, VcaDetector detector, double ratio, double expectedKneeDb, const std::string& note)
    {
        const auto& spec = specOf (model);
        auto s = measurementBase (model);
        s.thresholdDb = stepThresholdDb;
        s.ratio = ratio;
        s.attackMs = 0.2;
        s.releaseMs = 150.0;
        s.vcaDetector = detector;

        // The slope above the knee: 1 − 1/R, the feedback models' ∞ step at their cap of 20.
        const auto loopRatio = spec.feedback ? std::min (ratio, 20.0) : ratio;
        const auto top = std::isinf (loopRatio) ? 1.0 : 1.0 - 1.0 / loopRatio;
        const auto from = expectedKneeDb - 1.5, to = expectedKneeDb + 9.0 / top + 0.5;
        const auto per = std::max (2, samplesFor (0.002, o.rate) / 2 * 2);
        const auto rms = spec.detectorSwitch && detector == VcaDetector::rms;
        std::vector<double> levels;
        Samples key;
        for (auto x = from; x <= to + 1.0e-9; x += 0.01)
        {
            levels.push_back (x);
            append (key, carrier (per, gainOf (rms ? x - carrierRmsOffsetDb : x)));
        }
        const auto r = reductionTrace (o, s, key);

        // The settled reduction at each level, read at its last sample; the line through those between 1 and 8 dB.
        auto n = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (size_t i = 0; i < levels.size(); ++i)
        {
            const auto y = r[(i + 1) * (size_t) per - 1];
            if (y < 1.0 || y > 8.0)
                continue;
            n += 1.0;
            sx += levels[i];
            sy += y;
            sxx += levels[i] * levels[i];
            sxy += levels[i] * y;
        }
        const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), intercept = (sy - slope * sx) / n;
        const auto knee = -intercept / slope;
        return gated ("static", modelLabel (model, detector) + ", threshold coupling",
                      format ("Ratio %s: the hard knee, extrapolated from 1 - 8 dB of reduction, against T", ratioText (ratio).c_str()), knee - stepThresholdDb,
                      expectedKneeDb - stepThresholdDb, expectedKneeDb - stepThresholdDb - 0.05, expectedKneeDb - stepThresholdDb + 0.05, "dB",
                      format ("slope %.5f (1 - 1/R %.5f); %s", slope, top, note.c_str()));
    }

    /** Auto release dimmed (FET, VCA RMS: Controls): the switch changes nothing, bit for bit. */
    Row autoDimmedRow (const Options& o, Model model, VcaDetector detector)
    {
        auto s = measurementBase (model);
        s.thresholdDb = stepThresholdDb;
        s.ratio = 4.0;
        s.releaseMs = 150.0;
        s.vcaDetector = detector;
        const auto key = carrierSteps (o, { { -200.0, 0.05 }, { -10.0, 0.3 }, { -200.0, 0.3 }, { -20.0, 1.0 }, { -26.0, 0.2 }, { -200.0, 1.0 } });
        const auto off = reductionTrace (o, s, key);
        s.autoRelease = true;
        const auto on = reductionTrace (o, s, key);
        long differ = 0;
        for (size_t i = 0; i < std::min (off.size(), on.size()); ++i)
            differ += std::memcmp (&off[i], &on[i], sizeof (double)) != 0 ? 1 : 0;
        return gated ("ballistics", modelLabel (model, detector) + ", Auto release", "Auto release is dimmed: on and off give the same gain, bit for bit",
                      (double) differ, 0.0, 0.0, 0.0, "samples");
    }

    //==============================================================================
    // VCA (vca/NOTES.md, Gates).

    /** Peak Auto: the two-capacitor release against the transcription, at +0.1, +1, +10 s and to 36.8 %. */
    std::vector<Row> vcaAutoRows (const Options& o, const vcaRef::AutoRow& row)
    {
        auto s = measurementBase (Model::vca);
        s.thresholdDb = stepThresholdDb;
        s.ratio = 4.0;
        s.attackMs = row.attackMs;
        s.releaseMs = row.releaseMs;
        s.autoRelease = true;
        const auto level = vcaRef::levelForReductionDb (10.0, s.thresholdDb, 0.25);

        std::vector<int> starts;
        const auto key = carrierSteps (o, { { -200.0, 0.05 }, { level, row.heldS }, { -200.0, 31.0 } }, &starts);
        const auto r = reductionTrace (o, s, key);
        const auto step = starts[2];
        const auto expected = vcaRef::autoReleaseAfter (row.attackMs, row.releaseMs, (step - starts[1]) / o.rate, o.rate);

        const auto group = format ("vca peak, Auto release, Attack %g ms, Release %g ms", row.attackMs, row.releaseMs);
        const auto held = format ("held %g s", row.heldS);
        std::vector<Row> rows;
        const auto applied = r[(size_t) step - 1];
        rows.push_back (gated ("ballistics", group, held + ": the reduction when the signal stops", applied, expected.applied, expected.applied - 0.05,
                               expected.applied + 0.05, "dB"));
        // +0.1, +1 and +10 s: autoReadAtS's first, third and fifth, the NOTES' table's columns.
        for (const auto k : { 0, 2, 4 })
        {
            const auto at = vcaRef::autoReadAtS[(size_t) k];
            const auto m0 = k == 0 ? row.after01 : (k == 2 ? row.after1 : row.after10);
            const auto measured = r[(size_t) step - 1 + (size_t) samplesFor (at, o.rate)];
            rows.push_back (gated ("ballistics", group, held + format (": the reduction %g s after the signal stops", at), measured, expected.after[(size_t) k],
                                   expected.after[(size_t) k] - 0.3, expected.after[(size_t) k] + 0.3, "dB",
                                   std::isnan (m0) ? std::string ("against the transcription") : format ("against the transcription; M0's table %.2f dB", m0)));
        }
        const auto release = timeTo (r, step, applied / e, false, o.rate);
        rows.push_back (timeRow ("ballistics", group, held + ": release to 36.8 % of the applied reduction", release, expected.releaseS, o.rate,
                                 format ("M0's table %.3g s", row.releaseS)));
        return rows;
    }

    /** RMS attack (vca/NOTES.md, Gates): from Threshold on the RMS reading, Ratio ∞, the single pole's time for the step. */
    std::vector<Row> vcaRmsAttackRows (const Options& o, double attackMs)
    {
        std::vector<Row> rows;
        auto s = measurementBase (Model::vca);
        s.vcaDetector = VcaDetector::rms;
        s.thresholdDb = stepThresholdDb;
        s.ratio = inf;
        s.attackMs = attackMs;
        s.releaseMs = 1000.0;
        const auto settle = std::max (0.3, 12.0 * vcaRef::rmsTimeConstantMs (attackMs) / 1000.0);
        for (const auto depth : { 10.0, 20.0, 30.0 })
        {
            std::vector<int> starts;
            const auto key = carrierSteps (o, { { stepThresholdDb - carrierRmsOffsetDb, settle },
                                                { stepThresholdDb + depth - carrierRmsOffsetDb, std::max (0.2, 12.0 * vcaRef::rmsTimeConstantMs (attackMs) / 1000.0) } },
                                           &starts);
            const auto r = reductionTrace (o, s, key);
            const auto settled = r.back();
            const auto measured = timeTo (r, starts[1], (1.0 - 1.0 / e) * settled, true, o.rate);
            rows.push_back (timeRow ("ballistics", format ("vca rms, attack from Threshold, R inf, Attack %g ms", attackMs),
                                     format ("step of %g dB: 63.2 %% time", depth), measured, vcaRef::rmsAttackMs (s.attackMs, depth) / 1000.0, o.rate,
                                     format ("settled %.3f dB; the sheet's %g ms at the mark", settled, vcaRef::datasheetAttackMs (depth))));
        }
        return rows;
    }

    /** RMS release (vca/NOTES.md, Gates): a constant rate, the slope between 90 % and 10 % of the depth per 10 dB. */
    Row vcaRmsReleaseRow (const Options& o, double attackMs, double releaseMs, double depth)
    {
        auto s = measurementBase (Model::vca);
        s.vcaDetector = VcaDetector::rms;
        s.thresholdDb = stepThresholdDb;
        s.ratio = inf;
        s.attackMs = attackMs;
        s.releaseMs = releaseMs;
        const auto per10 = vcaRef::rmsReleasePer10DbMs (attackMs, releaseMs, 0.0) / 1000.0;
        std::vector<int> starts;
        const auto key = carrierSteps (o, { { stepThresholdDb + depth - carrierRmsOffsetDb, std::max (0.3, 15.0 * vcaRef::rmsTimeConstantMs (attackMs) / 1000.0) },
                                            { -200.0, 1.3 * per10 * depth / 10.0 + 0.1 } },
                                       &starts);
        const auto r = reductionTrace (o, s, key);
        const auto held = r[(size_t) starts[1] - 1];
        const auto t90 = timeTo (r, starts[1], 0.9 * held, false, o.rate), t10 = timeTo (r, starts[1], 0.1 * held, false, o.rate);
        const auto measured = (t10 - t90) * 10.0 / (0.8 * held);
        return timeRow ("ballistics", format ("vca rms, release rate, R inf, Attack %g ms", attackMs),
                        format ("Release %g ms, %g dB deep: time per 10 dB between 90 and 10 %%", releaseMs, depth), measured, per10, o.rate,
                        releaseMs < 1000.0 * per10 - 1.0e-9 ? format ("the detector's floor %.2f ms", 1000.0 * per10) : std::string ("the knob"));
    }

    /** RMS on pink noise band-limited to 20 Hz … 20 kHz (vca/NOTES.md, Gates): the mean reduction is the curve at the noise's RMS reading. */
    Row vcaPinkRow (const Options& o, double attackMs)
    {
        constexpr double depth = 6.0;
        auto s = measurementBase (Model::vca);
        s.vcaDetector = VcaDetector::rms;
        s.thresholdDb = stepThresholdDb;
        s.ratio = 4.0;
        s.attackMs = attackMs;
        s.releaseMs = 5.0667 * attackMs;    // under the detector's floor: the model is its single pole
        const auto reading = vcaRef::levelForReductionDb (depth, s.thresholdDb, 0.25);
        const auto n = samplesFor (10.0, o.rate), skip = samplesFor (1.0, o.rate);
        const auto r = reductionTrace (o, s, pink (n, o.rate, 11u, reading - carrierRmsOffsetDb));
        const auto measured = mean (r, skip, n);
        return gated ("static", "vca rms, pink noise 20 Hz - 20 kHz", format ("Attack %g ms, Release at the floor: mean reduction for a 6 dB curve", attackMs),
                      measured, depth, depth - 0.25, depth + 0.25, "dB", "the curve at the noise's RMS reading; 1 - 10 s");
    }

    //==============================================================================
    // FET (fet/NOTES.md, Gate table).

    /**
        All's lag (fet/NOTES.md, Gate table): no reduction of 0.1 dB before L − 1 sample, and the
        first reduction by L + 1 sample (L = 1 ms); the attack after it is the step grid's All rows.
    */
    std::vector<Row> fetLagRows (const Options& o, double attackMs)
    {
        const auto& spec = specOf (Model::fet);
        auto s = measurementBase (Model::fet);
        s.thresholdDb = stepThresholdDb;
        s.ratio = inf;
        s.attackMs = attackMs;
        s.releaseMs = 150.0;
        std::vector<int> starts;
        const auto key = carrierSteps (o, { { -200.0, 0.05 }, { levelFor (spec, s, Signal::carrier, 10.0), 0.3 + 12.0 * attackMs / 1000.0 } }, &starts);
        const auto r = reductionTrace (o, s, key);
        const auto firstAbove = [&] (double db)
        {
            for (int i = starts[1]; i < (int) r.size(); ++i)
                if (r[(size_t) i] > db)
                    return (double) (i - starts[1]);
            return std::numeric_limits<double>::quiet_NaN();
        };
        const auto lag = fetRef::allLagMs / 1000.0 * o.rate;
        const auto group = format ("fet, All's lag, Attack %g ms", attackMs);
        return { gated ("ballistics", group, "the first sample after the step with 0.1 dB of reduction", firstAbove (fetRef::lagQuietDb), lag,
                        lag - fetRef::lagToleranceSamples, std::numeric_limits<double>::infinity(), "samples", "L = 1 ms: quiet for L"),
                 gated ("ballistics", group, "the first sample after the step with any reduction", firstAbove (0.0), lag, -std::numeric_limits<double>::infinity(),
                        lag + fetRef::lagToleranceSamples, "samples", "then attacking from L") };
    }

    /** The knee region (reported, fet/NOTES.md): Knee 12, R 4, the attack of steps ending inside the knee against Attack·R·G'. */
    Row fetKneeRow (const Options& o, double overDb)
    {
        auto s = measurementBase (Model::fet);
        s.thresholdDb = stepThresholdDb;
        s.ratio = 4.0;
        s.kneeDb = 12.0;
        s.attackMs = 1.0;
        s.releaseMs = 150.0;
        const auto level = stepThresholdDb + overDb;
        const auto t = stepResponse (o, s, level, 0.5, 0.2);
        const auto expected = fetRef::kneeRegionAttackMs (1.0, level, stepThresholdDb, 4.0, 12.0);
        return reported ("ballistics", "fet, knee region, Knee 12, R 4, Attack 1 ms", format ("step to T %+g dB: attack", overDb), 1000.0 * t.attack, expected,
                         "ms", format ("%.3f x the knob; target: Attack R G' at the settled level, linearised", 1000.0 * t.attack));
    }

    //==============================================================================
    // Opto (opto/NOTES.md, Gate table).

    optoRef::Settings optoSettings (const Settings& s, double rate)
    {
        optoRef::Settings r;
        r.thresholdDb = s.thresholdDb;
        r.inverseRatio = inverseOf (s);
        r.kneeDb = s.kneeDb;
        r.rangeDb = s.rangeDb;
        r.attackMs = s.attackMs;
        r.releaseMs = s.releaseMs;
        r.autoRelease = s.autoRelease;
        r.rate = rate;
        r.attackConstantMs = optoAttackConstantMs (std::max (s.attackMs, optoRef::attackFloorMs), std::min (r.inverseRatio, 1.0 - optoRef::slopeShareFloor),
                                                   s.kneeDb);
        return r;
    }

    /** What the engine reads after `holdS` at `depth` dB on the carrier, then silence: the reduction held and the time to `fraction` of it. */
    std::pair<double, double> optoRelease (const Options& o, const Settings& s, double depth, double holdS, double fraction, double tailS)
    {
        const auto reading = optoRef::levelForReduction (depth, s.thresholdDb, inverseOf (s), s.kneeDb);
        std::vector<int> starts;
        const auto key = carrierSteps (o, { { -200.0, 0.05 }, { reading - carrierRmsOffsetDb, holdS }, { -200.0, tailS } }, &starts);
        const auto r = reductionTrace (o, s, key);
        const auto held = r[(size_t) starts[2] - 1];
        return { held, timeTo (r, starts[2], fraction * held, false, o.rate) };
    }

    /** The history row: full release (to 10 %) after 0.5 s and after 10 s at 10 dB, Auto on, against the transcription; their ratio ≥ 2. */
    std::vector<Row> optoHistoryRows (const Options& o, double ratio, double releaseMs)
    {
        auto s = measurementBase (Model::opto);
        s.thresholdDb = stepThresholdDb;
        s.ratio = ratio;
        s.kneeDb = 6.0;
        s.attackMs = 10.0;
        s.releaseMs = releaseMs;
        s.autoRelease = true;
        const auto ref = optoSettings (s, o.rate);
        const auto group = format ("opto, history, Auto on, R %s, Knee 6, Release %g ms", ratioText (ratio).c_str(), releaseMs);

        std::vector<Row> rows;
        std::array<double, 2> measured {};
        const std::array<double, 2> holds { 0.5, 10.0 };
        for (size_t k = 0; k < holds.size(); ++k)
        {
            const auto expected = optoRef::fullReleaseS (ref, holds[k]);
            const auto [held, release] = optoRelease (o, s, 10.0, holds[k], 0.1, 1.5 * expected + 1.0);
            measured[k] = release;
            const auto m0 = myzel::dsp::isExactly (ratio, 4.0) && myzel::dsp::isExactly (releaseMs, 60.0) ? format ("; M0 expected %.2f s", k == 0 ? optoRef::m0FullReleaseAfterHalfSecondS
                                                                                                      : optoRef::m0FullReleaseAfterTenSecondsS)
                                                             : std::string();
            rows.push_back (timeRow ("ballistics", group, format ("after %g s at 10 dB: full release (to 10 %%)", holds[k]), release, expected, o.rate,
                                     format ("held %.3f dB", held) + m0));
        }
        rows.push_back (gated ("ballistics", group, "full release after 10 s / after 0.5 s (\"depending upon the amount of previous reduction\")",
                               measured[1] / measured[0], optoRef::historyRatioFloor, optoRef::historyRatioFloor, std::numeric_limits<double>::infinity(), "x",
                               format ("the transcription's %.3f x", optoRef::fullReleaseS (ref, 10.0) / optoRef::fullReleaseS (ref, 0.5))));
        return rows;
    }

    /** The depth row: t50 after 20 dB over t50 after 10 dB, Auto off, Release 60 ms, R 4, Knee 6, against the transcription ±10 %. */
    Row optoDepthRow (const Options& o)
    {
        auto s = measurementBase (Model::opto);
        s.thresholdDb = stepThresholdDb;
        s.ratio = 4.0;
        s.kneeDb = 6.0;
        s.attackMs = 10.0;
        s.releaseMs = 60.0;
        const auto deep = optoRelease (o, s, 20.0, 1.0, 0.5, 1.0).second, shallow = optoRelease (o, s, 10.0, 1.0, 0.5, 1.0).second;
        const auto expected = optoRef::depthRatio (optoSettings (s, o.rate));
        return gated ("ballistics", "opto, depth, Auto off, R 4, Knee 6, Release 60 ms", "t50 after 20 dB / t50 after 10 dB", deep / shallow, expected,
                      0.9 * expected, 1.1 * expected, "x", format ("%.2f ms / %.2f ms; M0 expected about 1.84", 1000.0 * deep, 1000.0 * shallow));
    }

    //==============================================================================
    // Vari-mu (varimu/NOTES.md, Gate table).

    /** The rising ratio: secants from the settled 1 dB staircase at Ratio 4, Knee 6, and 20 ±1 above the knee at Ratio 20. */
    std::vector<Row> variMuRatioRows (const Options& o, double ratio)
    {
        auto s = measurementBase (Model::variMu);
        s.thresholdDb = stepThresholdDb;
        s.ratio = ratio;
        const auto rising = myzel::dsp::isExactly (ratio, 4.0);
        s.kneeDb = rising ? 6.0 : 0.0;
        s.attackMs = 10.0;
        const auto st = staircase (o, s, Signal::carrier, 1000.0);

        // 1 / (G(x_n) − G(x_n−1)) from the measured reductions, the reading the reference's secant describes.
        std::vector<double> secant (st.levels.size(), std::numeric_limits<double>::quiet_NaN());
        for (size_t i = 1; i < st.levels.size(); ++i)
            secant[i] = 1.0 / ((st.levels[i] - st.measured[i]) - (st.levels[i - 1] - st.measured[i - 1]));

        std::vector<Row> rows;
        const auto group = format ("vari-mu, rising ratio, R %s, Knee %g, carrier", ratioText (ratio).c_str(), s.kneeDb);
        if (rising)
        {
            for (const auto& limit : muRef::risingRatioLimits)
            {
                auto measured = std::numeric_limits<double>::quiet_NaN();
                for (size_t i = 2; i < st.levels.size(); ++i)
                    if (st.measured[i - 1] <= limit.reductionDb && st.measured[i] >= limit.reductionDb)
                    {
                        measured = secant[i - 1] + (limit.reductionDb - st.measured[i - 1]) / (st.measured[i] - st.measured[i - 1]) * (secant[i] - secant[i - 1]);
                        break;
                    }
                rows.push_back (gated ("static", group, format ("local ratio (1 dB secant) at %g dB of reduction", limit.reductionDb), measured,
                                       muRef::secantRatioAtReduction (limit.reductionDb, 0.25, 6.0), limit.atLeast, limit.atMost, ":1",
                                       "target: the transcription's secant"));
            }
            return rows;
        }

        auto worst = 20.0;
        auto worstAt = std::numeric_limits<double>::quiet_NaN();
        for (size_t i = 1; i < st.levels.size(); ++i)
            if (st.levels[i - 1] > s.thresholdDb + 1.0 && st.measured[i] < s.rangeDb - 0.5
                && (! std::isfinite (secant[i]) || std::abs (secant[i] - 20.0) >= std::abs (worst - 20.0)))
            {
                worst = secant[i];
                worstAt = st.levels[i] - s.thresholdDb;
            }
        rows.push_back (gated ("static", group, "local ratio above the knee, the furthest from 20", worst, 20.0, 20.0 - muRef::capRatioTolerance,
                               20.0 + muRef::capRatioTolerance, ":1", format ("at T %+g dB", worstAt)));
        return rows;
    }

    /** Auto (varimu/NOTES.md, Gate table): the release after each history against the transcription; one peak = Release; growing with the peaks. */
    std::vector<Row> variMuAutoRows (const Options& o, double releaseMs, bool quick)
    {
        auto s = measurementBase (Model::variMu);
        s.thresholdDb = stepThresholdDb;
        s.ratio = 4.0;
        s.kneeDb = 6.0;
        s.attackMs = 0.4;
        s.releaseMs = releaseMs;
        s.autoRelease = true;
        muRef::Settings ref;
        ref.thresholdDb = s.thresholdDb;
        ref.inverseRatio = 0.25;
        ref.kneeDb = s.kneeDb;
        ref.rangeDb = s.rangeDb;
        ref.attackMs = s.attackMs;
        ref.releaseMs = releaseMs;
        ref.autoRelease = true;

        const auto amplitude = gainOf (muRef::levelForReduction (10.0, s.thresholdDb, 0.25, s.kneeDb));
        const auto group = format ("vari-mu, Auto, R 4, Knee 6, Attack 0.4 ms, Release %g s", releaseMs / 1000.0);
        std::vector<Row> rows;
        std::vector<double> peaks;
        for (size_t k = 0; k < muRef::autoRows().size(); ++k)
        {
            const auto& row = muRef::autoRows()[k];
            if (quick && k != 0 && k != 1 && k != 4)
                continue;
            constexpr double preS = 0.05;
            const auto tailS = std::min (muRef::slowTier * releaseMs / 1000.0, muRef::tierCapS) * 1.5 + 0.5;
            const auto magnitude = muRef::magnitudes (row.history, amplitude, preS, tailS, o.rate);
            Samples key (magnitude.size());
            for (size_t i = 0; i < key.size(); ++i)
                key[i] = (float) (i % 2 == 0 ? magnitude[i] : -magnitude[i]);
            const auto r = reductionTrace (o, s, key);
            const auto step = (size_t) std::lround ((preS + row.history.endS()) * o.rate);
            const auto measured = muRef::releaseSeconds (r, step, o.rate);
            const auto expected = muRef::expectedAutoReleaseS (row.history, ref, 10.0, o.rate);
            const auto note = format ("%.2f x Release; M0's variant %.3g s", measured / (releaseMs / 1000.0), muRef::m0ExpectedReleaseS (row, releaseMs));
            rows.push_back (timeRow ("ballistics", group, std::string ("after ") + row.history.name + ": release against the transcription", measured, expected,
                                     o.rate, note));
            if (k == 0)
                rows.push_back (timeRow ("ballistics", group, "after one 50 ms peak: release = Release", measured, releaseMs / 1000.0, o.rate));
            if (k < 4)
                peaks.push_back (measured);
        }
        if (peaks.size() == 4)
        {
            auto least = std::numeric_limits<double>::infinity();
            for (size_t k = 1; k < peaks.size(); ++k)
                least = std::min (least, peaks[k] / peaks[k - 1]);
            rows.push_back (gated ("ballistics", group, "the release grows with the number of peaks (1, 3, 5, 8): smallest ratio of successive releases",
                                   least, 1.0, 1.0, std::numeric_limits<double>::infinity(), "x",
                                   format ("%.3f, %.3f, %.3f, %.3f s", peaks[0], peaks[1], peaks[2], peaks[3])));
        }
        return rows;
    }

    //==============================================================================
    // Upward (upward/NOTES.md, Gates). The lift is the negative reduction.

    std::vector<double> liftOf (std::vector<double> r)
    {
        for (auto& v : r)
            v = -v;
        return r;
    }

    /** The attack gate: the carrier from T − 20 to T + 10 on the RMS reading, the time for the lift to fall to 36.8 %. */
    Row upwardAttackRow (const Options& o, double attackMs)
    {
        using G = upRef::AttackGate;
        auto s = measurementBase (Model::upward);
        s.thresholdDb = G::thresholdDb;
        s.floorDb = G::floorDb;
        s.rangeDb = G::rangeDb;
        s.kneeDb = G::kneeDb;
        s.ratio = 4.0;
        s.attackMs = attackMs;
        s.releaseMs = G::releaseMs;
        std::vector<int> starts;
        const auto key = carrierSteps (o, { { G::thresholdDb - G::belowDb - carrierRmsOffsetDb, G::holdSecondsFor (attackMs) },
                                            { G::thresholdDb + G::aboveDb - carrierRmsOffsetDb, 10.0 * attackMs / 1000.0 + 0.1 } },
                                       &starts);
        const auto y = liftOf (reductionTrace (o, s, key));
        const auto start = y[(size_t) starts[1] - 1];
        const auto measured = timeTo (y, starts[1], start / e, false, o.rate);
        const auto expected = upRef::gatedAttackSeconds (s.attackMs, o.rate);
        return timeRow ("ballistics", "upward, attack, T -30, R 4, carrier T-20 -> T+10", format ("Attack %g ms: the lift to 36.8 %%", attackMs), measured,
                        expected, o.rate,
                        format ("lift at the step %.2f dB; %s", start,
                                attackMs >= 3.0 ? "against the knob" : format ("against the specified %.4f ms (the RMS's rise)", 1000.0 * expected).c_str()));
    }

    /** The lift-in gate: the carrier from T + 10 to T − 60, Range 20, Floor −90, the lift's slope from 2 to 12 dB per 10 dB. */
    Row upwardLiftInRow (const Options& o, double releaseMs, double ratio, double attackMs, bool autoRelease = false, double heldSeconds = 0.5)
    {
        using G = upRef::ReleaseGate;
        auto s = measurementBase (Model::upward);
        s.thresholdDb = G::thresholdDb;
        s.floorDb = G::floorDb;
        s.rangeDb = G::rangeDb;
        s.kneeDb = G::kneeDb;
        s.ratio = ratio;
        s.attackMs = attackMs;
        s.releaseMs = releaseMs;
        s.autoRelease = autoRelease;
        const auto slowest = (autoRelease ? upRef::autoFactor (1.0) : 1.0) * upRef::releasePer10DbMs (releaseMs, inverseOf (s));
        std::vector<int> starts;
        const auto key = carrierSteps (o, { { -200.0, 0.05 }, { G::thresholdDb + G::aboveDb - carrierRmsOffsetDb, heldSeconds },
                                            { G::thresholdDb - G::belowDb - carrierRmsOffsetDb, (1.3 * slowest + 250.0) / 1000.0 } },
                                       &starts);
        const auto y = liftOf (reductionTrace (o, s, key));
        const auto from = timeTo (y, starts[2], G::fromDb, true, o.rate), to = timeTo (y, starts[2], G::toDb, true, o.rate);
        const auto measured = (to - from) * upRef::liftPerReleaseDb / (G::toDb - G::fromDb);
        const auto expected = upRef::liftInSeconds (releaseMs, inverseOf (s), o.rate, autoRelease, heldSeconds, attackMs);
        if (autoRelease)
            return timeRow ("ballistics", "upward, Auto, Release 150 ms, Attack 5 ms, R 4", format ("after %g s above Threshold: lift-in per 10 dB", heldSeconds),
                            measured, expected, o.rate, format ("M0 read %g ms", heldSeconds < 1.0 ? 209.0 : 606.0));
        return timeRow ("ballistics", format ("upward, lift-in, R %s, Attack %g ms", ratioText (ratio).c_str(), attackMs),
                        format ("Release %g ms: time per 10 dB of lift (2 -> 12 dB)", releaseMs), measured, expected, o.rate,
                        releaseMs < upRef::detectorReleaseFloorMs (inverseOf (s)) ? format ("the detector's floor %.2f ms", upRef::detectorReleaseFloorMs (inverseOf (s)))
                                                                                  : std::string ("10 dB per Release"));
    }
}

std::vector<Row> kit::liftShapeRows (const Staircase& st, const std::string& group, const std::string& item)
{
    // Output level x + ℓ against x: never falling (no fold-back), within the carrier's read-out noise.
    auto worst = std::numeric_limits<double>::infinity();
    auto worstAt = 0.0;
    for (size_t i = 1; i < st.levels.size(); ++i)
    {
        const auto rise = (st.levels[i] - st.measured[i]) - (st.levels[i - 1] - st.measured[i - 1]);
        if (rise < worst)
        {
            worst = rise;
            worstAt = st.levels[i];
        }
    }

    // At and under Floor the gain is exactly 1, from the start: the staircase rises from under it, its
    // carrier reading 3.01 dB over its amplitude.
    long touched = 0;
    for (size_t i = 0; i < st.levels.size(); ++i)
        if (st.levels[i] + carrierRmsOffsetDb <= st.applied.floorDb + 1.0e-9)
            for (int n = i == 0 ? 0 : st.ends[i - 1]; n < st.ends[i]; ++n)
                touched += myzel::dsp::isExactly (st.gain[(size_t) n], 1.0) ? 0 : 1;

    return { gated ("static", group, item + ": output level rises with the input (smallest rise per 1 dB step)", worst, 0.0, -0.01,
                    std::numeric_limits<double>::infinity(), "dB", format ("at %+g dB; -0.01 dB allows the read-out's noise", worstAt)),
             gated ("static", group, item + ": the gain at and under Floor, samples not exactly 1", (double) touched, 0.0, 0.0, 0.0, "samples") };
}

std::vector<Job> kit::modelStaticJobs (const Options& o, Model model)
{
    std::vector<Job> jobs;
    switch (runningModel (model))
    {
        case Model::vca:
            for (const auto detector : detectorsFor (o, model))
            {
                for (const auto ratio : o.quick ? std::vector<double> { 2.0, inf } : std::vector<double> { 2.0, 4.0, 10.0, 20.0, inf })
                    jobs.push_back ([&o, detector, ratio]
                    {
                        return std::vector<Row> { couplingRow (o, Model::vca, detector, ratio, vcaRef::couplingKneeDb (stepThresholdDb, reference::inverseRatioOf (ratio)),
                                                               "T_eff = T - 6/R") };
                    });
                if (detector == VcaDetector::rms)
                    for (const auto attack : o.quick ? std::vector<double> { vcaRef::markAttackMs } : std::vector<double> { vcaRef::markAttackMs, 100.0 })
                        jobs.push_back ([&o, attack] { return std::vector<Row> { vcaPinkRow (o, attack) }; });
            }
            break;

        case Model::fet:
            for (const auto& [ratio, offset] : fetRef::couplingRows)
            {
                if (o.quick && ! myzel::dsp::isExactly (ratio, 8.0) && ! std::isinf (ratio))
                    continue;
                jobs.push_back ([&o, ratio, offset]
                { return std::vector<Row> { couplingRow (o, Model::fet, VcaDetector::peak, ratio, stepThresholdDb + offset, "T + dT(R), linear in log2 R between the reference's points") }; });
            }
            break;

        case Model::variMu:
            for (const auto ratio : o.quick ? std::vector<double> { 4.0 } : std::vector<double> { 4.0, 20.0 })
                jobs.push_back ([&o, ratio] { return variMuRatioRows (o, ratio); });
            break;

        case Model::clean:
        case Model::opto:
        case Model::upward:
        case Model::diodeBridge:
        case Model::pwm:
        case Model::pumping:
            break;
    }
    return jobs;
}

std::vector<Job> kit::modelBallisticsJobs (const Options& o, Model model)
{
    std::vector<Job> jobs;
    switch (runningModel (model))
    {
        case Model::vca:
            for (const auto detector : detectorsFor (o, model))
            {
                if (detector == VcaDetector::peak)
                {
                    for (size_t k = 0; k < vcaRef::autoRows.size(); ++k)
                        if (! o.quick || k < 3)
                            jobs.push_back ([&o, k] { return vcaAutoRows (o, vcaRef::autoRows[k]); });
                    continue;
                }
                for (const auto attack : o.quick ? std::vector<double> { vcaRef::markAttackMs } : std::vector<double> { 1.0, vcaRef::markAttackMs, 100.0 })
                    jobs.push_back ([&o, attack] { return vcaRmsAttackRows (o, attack); });
                const auto releases = o.quick ? std::vector<double> { 80.0, 1000.0 } : std::vector<double> { 5.0, 20.0, 80.0, 150.0, 500.0, 1000.0, 5000.0 };
                for (const auto release : releases)
                    for (const auto depth : o.quick ? std::vector<double> { 6.0, 24.0 } : std::vector<double> { 6.0, 12.0, 24.0 })
                        jobs.push_back ([&o, release, depth] { return std::vector<Row> { vcaRmsReleaseRow (o, 1.0, release, depth) }; });
                if (! o.quick)
                    jobs.push_back ([&o] { return std::vector<Row> { vcaRmsReleaseRow (o, 10.0, 20.0, 12.0) }; });
                jobs.push_back ([&o] { return std::vector<Row> { autoDimmedRow (o, Model::vca, VcaDetector::rms) }; });
            }
            break;

        case Model::fet:
            for (const auto attack : o.quick ? std::vector<double> { 0.2 } : std::vector<double> { 0.02, 0.2, 10.0 })
                jobs.push_back ([&o, attack] { return fetLagRows (o, attack); });
            if (! o.quick)
                for (const auto over : { -4.0, -2.0, 0.0, 2.0, 4.0, 6.0, 10.0 })
                    jobs.push_back ([&o, over] { return std::vector<Row> { fetKneeRow (o, over) }; });
            jobs.push_back ([&o] { return std::vector<Row> { autoDimmedRow (o, Model::fet, VcaDetector::peak) }; });
            break;

        case Model::opto:
            for (const auto ratio : o.quick ? std::vector<double> { 4.0 } : std::vector<double> { 2.0, 4.0, 10.0, 20.0 })
                for (const auto release : o.quick ? std::vector<double> { 60.0 } : std::vector<double> { 30.0, 60.0, 150.0, 500.0, 1000.0 })
                    jobs.push_back ([&o, ratio, release] { return optoHistoryRows (o, ratio, release); });
            jobs.push_back ([&o] { return std::vector<Row> { optoDepthRow (o) }; });
            break;

        case Model::variMu:
            for (const auto release : o.quick ? std::vector<double> { 300.0 } : std::vector<double> { 300.0, 2000.0 })
                jobs.push_back ([&o, release] { return variMuAutoRows (o, release, o.quick); });
            break;

        case Model::upward:
        {
            const auto attacks = o.quick ? std::vector<double> { 0.02, 1.0, 10.0, 100.0 }
                                         : std::vector<double> { 0.02, 0.05, 0.1, 0.3, 0.5, 1.0, 3.0, 10.0, 30.0, 100.0, 300.0 };
            for (const auto attack : attacks)
                jobs.push_back ([&o, attack] { return std::vector<Row> { upwardAttackRow (o, attack) }; });
            if (o.quick)
            {
                for (const auto& [release, ratio] : std::vector<std::pair<double, double>> { { 150.0, 4.0 }, { 1000.0, 4.0 }, { 5.0, inf } })
                    jobs.push_back ([&o, release, ratio] { return std::vector<Row> { upwardLiftInRow (o, release, ratio, 1.0) }; });
            }
            else
            {
                for (const auto ratio : { 1.5, 2.0, 4.0, 10.0, inf })
                    for (const auto release : { 5.0, 20.0, 50.0, 150.0, 500.0, 1000.0, 5000.0 })
                        for (const auto attack : { 1.0, 30.0 })
                            jobs.push_back ([&o, release, ratio, attack] { return std::vector<Row> { upwardLiftInRow (o, release, ratio, attack) }; });
            }
            for (const auto held : { 0.2, 6.0 })
                jobs.push_back ([&o, held] { return std::vector<Row> { upwardLiftInRow (o, 150.0, 4.0, 5.0, true, held) }; });
            break;
        }

        case Model::clean:
        case Model::diodeBridge:
        case Model::pwm:
        case Model::pumping:
            break;
    }
    return jobs;
}

//==============================================================================
std::vector<Row> kit::floorSweepRows (const Options& o, LatencyMode latency)
{
    // A level moving 1 dB/s through Floor − 2 … Floor + 14 dB at 20:1: the lift fades in on its
    // smooth curve, so the gain never steps (upward/NOTES.md, Gates, Floor; the click measure).
    Settings s;
    s.model = Model::upward;
    s.latency = latency;
    s.ratio = 20.0;
    s.key = KeySource::external;
    const auto seconds = 16.0;
    const auto n = samplesFor (seconds, o.rate);
    Input in;
    in.main = { sine (330.0, n, o.rate, 0.25), sine (330.0, n, o.rate, 0.2, 1.1) };
    Samples key ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto reading = s.floorDb - 2.0 + i / o.rate;
        key[(size_t) i] = (float) ((i % 2 == 0 ? 1.0 : -1.0) * gainOf (reading - carrierRmsOffsetDb));
    }
    in.key = { key, key };
    auto device = makeDevice (o.device);
    const auto out = render (*device, s, in, { o.rate, { 256 }, {}, {} });
    auto crest = -std::numeric_limits<double>::infinity();
    for (int ch = 0; ch < 2; ++ch)
        crest = std::max (crest, clickCrestDb (out.main[(size_t) ch], samplesFor (0.3, o.rate) + out.latency, n));
    return { gated ("clicks", format ("upward, %s, Attack 10 ms", latencyName (latency)), "Floor: the key sweeping 1 dB/s through Floor -2 ... +14 dB, R 20",
                    crest, -60.0, -std::numeric_limits<double>::infinity(), -60.0, "dB", "the click measure on the 330 Hz sines over the whole sweep") };
}

} // namespace basalt::measure
