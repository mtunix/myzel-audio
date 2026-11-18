// The static curve and the ballistics on the core's definition (PLAN.md, Measurement and quality
// gates; dynamics/NOTES.md, Ballistics and Gate table; each model's NOTES, Gate table). What each
// model specifies comes from its ModelSpec; the rows a model adds of its own are in Models.cpp.

#include "support/GateKit.h"

#include "support/Reference.h"

#include "common/dsp/FloatCompare.h"

#include <cmath>
#include <limits>
#include <numbers>

namespace basalt::measure
{

using namespace kit;

namespace
{
    constexpr double staticTolerance = 0.25;
    constexpr double stepDepthDb = 10.0;        // D, the settled reduction of a step
    constexpr double stepThresholdDb = -30.0;
    const double e = std::exp (1.0);

    std::string ratioLabel (double ratio) { return "R " + ratioText (ratio); }

    std::string signalLabel (Signal s, double hz)
    {
        return s == Signal::carrier ? "carrier" : (hz >= 1000.0 ? format ("%g kHz sine", hz / 1000.0) : format ("%g Hz sine", hz));
    }

    struct Curve
    {
        double threshold, ratio, knee, range, floor, attackMs;
        Signal signal;
        double hz;  // sines
        bool gate;
        VcaDetector detector;
    };

    Settings settingsOf (Model model, const Curve& c)
    {
        auto s = measurementBase (model);
        s.thresholdDb = c.threshold;
        s.ratio = c.ratio;
        s.kneeDb = c.knee;
        s.rangeDb = c.range;
        s.floorDb = c.floor;
        s.attackMs = c.attackMs;
        s.vcaDetector = c.detector;
        return s;
    }

    /** One curve's row: the error furthest from the specification over the model's levels. */
    std::string curveItem (const ModelSpec& spec, const Curve& c)
    {
        auto item = format ("T %g, %s, Knee %g, Range %g", c.threshold, ratioLabel (c.ratio).c_str(), c.knee, c.range);
        if (spec.lifts)
            item += format (", Floor %g", c.floor);
        return item;
    }

    std::string curveGroup (Model model, const Curve& c)
    {
        return format ("%s, %s, Attack %g ms", modelLabel (model, c.detector).c_str(), signalLabel (c.signal, c.hz).c_str(), c.attackMs);
    }

    Row curveRow (Model model, const Curve& c, const Staircase& st)
    {
        const auto& spec = specOf (model);

        auto worst = 0.0, worstLevel = st.levels.front(), worstMeasured = 0.0, worstSpecified = 0.0;
        for (size_t i = 0; i < st.levels.size(); ++i)
        {
            const auto error = st.measured[i] - st.specified[i];
            if (! std::isfinite (error) || std::abs (error) > std::abs (worst))
            {
                worst = std::isfinite (error) ? error : std::numeric_limits<double>::quiet_NaN();
                worstLevel = st.levels[i];
                worstMeasured = st.measured[i];
                worstSpecified = st.specified[i];
                if (! std::isfinite (error))
                    break;
            }
        }

        const auto group = curveGroup (model, c);
        const auto item = curveItem (spec, c);
        const auto note = format ("largest error at T %+g dB: measured %.3f, specified %.3f dB", worstLevel - c.threshold, worstMeasured, worstSpecified);
        if (c.gate)
            return gated ("static", group, item, worst, 0.0, -staticTolerance, staticTolerance, "dB", note);
        return reported ("static", group, item, worst, 0.0, "dB", note);
    }

    /** The steady mean reduction on pink noise against BasaltMeasure's transcription run on the same noise (clean/NOTES.md, Gates). */
    Row pinkRow (const Options& o, Model model, double attackMs, double ratio)
    {
        auto s = measurementBase (model);
        s.thresholdDb = stepThresholdDb;
        s.ratio = ratio;
        s.kneeDb = 6.0;
        s.attackMs = attackMs;

        const auto n = samplesFor (10.0, o.rate), from = samplesFor (2.0, o.rate);
        const Channels key { pink (n, o.rate, 7u, -24.0) };
        const auto device = renderKey (o.device, s, key, o.probe, { o.rate, { 512 }, {}, {} }).traces[0].reductionDb;
        const auto reference = renderKey (DeviceKind::reference, s, key, Probe::external, { o.rate, { 512 }, {}, {} }).traces[0].reductionDb;
        const auto measured = mean (device, from, n), expected = mean (reference, from, n);
        return gated ("static", "pink noise, 20 Hz - 20 kHz, -24 dBFS RMS, against the transcription",
                      format ("T -30, %s, Knee 6, Attack %g ms", ratioLabel (ratio).c_str(), attackMs), measured, expected,
                      expected - staticTolerance, expected + staticTolerance, "dB", "mean reduction over 2 - 10 s");
    }

    /**
        Range (dynamics/NOTES.md, Range): R ∞ (feedback: the cap), 30 dB over, Range 10: steady
        reduction 10 dB ±0.05. Upward's Range caps the lift: 30 dB under Threshold.
    */
    Row rangeRow (const Options& o, Model model, VcaDetector detector)
    {
        const auto& spec = specOf (model);
        auto s = measurementBase (model);
        s.thresholdDb = stepThresholdDb;
        s.ratio = infiniteRatio;
        s.rangeDb = 10.0;
        s.releaseMs = 150.0;
        s.floorDb = -90.0;
        s.vcaDetector = detector;
        const auto level = spec.lifts ? stepThresholdDb - 30.0 : stepThresholdDb + 30.0;
        const auto n = samplesFor (1.0, o.rate);
        const auto r = reductionTrace (o, s, carrier (n, gainOf (level)));
        const auto measured = mean (r, n - samplesFor (0.05, o.rate), n);
        Settings applied = s;
        const auto expected = spec.steadyReduction (applied, Signal::carrier, level);
        return gated ("static", modelLabel (model, detector) + ", Range",
                      spec.lifts ? "R inf, carrier 30 dB under, Range 10: steady lift" : "R inf, carrier 30 dB over, Range 10: steady reduction", measured,
                      expected, expected - 0.05, expected + 0.05, "dB");
    }
}

std::vector<VcaDetector> kit::detectorsFor (const Options& o, Model model)
{
    if (! specOf (model).detectorSwitch || o.detectors.empty())
        return { VcaDetector::peak };
    return o.detectors;
}

std::string kit::modelLabel (Model model, VcaDetector detector)
{
    if (specOf (model).detectorSwitch)
        return std::string (modelKey (model)) + (detector == VcaDetector::rms ? " rms" : " peak");
    return modelKey (model);
}

double kit::levelFor (const ModelSpec& spec, const Settings& s, Signal signal, double depthDb)
{
    // The reduction rises with level (a lift falls towards 0): bisection between far below and far above Threshold.
    auto lo = s.thresholdDb - (spec.lifts ? 60.0 : 30.0), hi = s.thresholdDb + 200.0;
    for (int i = 0; i < 200; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (spec.steadyReduction (s, signal, mid) < depthDb ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

double kit::timeTolerance (double targetSeconds, double rate)
{
    return std::max (0.10 * targetSeconds, 0.25 / rate);
}

std::vector<double> kit::reductionTrace (const Options& o, const Settings& s, const Samples& key)
{
    return renderKey (o.device, s, { key }, o.probe, { o.rate, { 512 }, {}, {} }).traces[0].reductionDb;
}

Staircase kit::staircase (const Options& o, const Settings& s, Signal signal, double hz)
{
    const auto& spec = specOf (s.model);
    const auto rate = o.rate;
    const auto sine = signal == Signal::sine;
    const auto window = ! sine || hz >= 1000.0 ? 0.05 : (hz >= 100.0 ? 0.1 : 0.2);
    const auto windowSamples = samplesFor (window, rate);

    Staircase st;
    if (spec.staticLevels)
        st.levels = spec.staticLevels (s);
    else
        for (auto l = s.thresholdDb - 20.0; l <= s.thresholdDb + 30.0 + 1.0e-9; l += 1.0)
            st.levels.push_back (l);
    if (spec.staticDescending && spec.staticDescending (s))
        std::reverse (st.levels.begin(), st.levels.end());

    // The levels are on the detector's reading: the carrier's amplitude sits 3.01 dB under it on a
    // mean-square detector, which steadyReduction knows; the staircase plays amplitudes.
    Samples key;
    auto from = -200.0;
    for (size_t i = 0; i < st.levels.size(); ++i)
    {
        const auto to = st.levels[i];
        auto settle = spec.stepSettleSeconds ? spec.stepSettleSeconds (s, signal, from, to) : std::max (0.1, 12.0 * s.attackMs / 1000.0);
        settle += sine && hz < 1000.0 ? 0.1 : 0.0;
        const auto segment = 2 * (samplesFor (settle + window, rate) / 2);
        append (key, sine ? measure::sine (hz, segment, rate, gainOf (to), 0.0, (int64_t) key.size()) : carrier (segment, gainOf (to)));
        st.ends.push_back ((int) key.size());
        from = to;
    }

    const auto r = renderKey (o.device, s, { key }, o.probe, { rate, { 512 }, {}, {} });
    st.applied = r.applied;
    st.trace = r.traces[0].reductionDb;
    st.gain = r.traces[0].gain;
    st.window = windowSamples;
    for (size_t i = 0; i < st.levels.size(); ++i)
    {
        st.measured.push_back (mean (st.trace, st.ends[i] - windowSamples, st.ends[i]));
        st.specified.push_back (spec.steadyReduction (st.applied, signal, st.levels[i]));
    }
    return st;
}

Row kit::staticCurveRow (const Options& o, Model model, double thresholdDb, double ratio, double kneeDb, double rangeDb, double attackMs, Signal signal,
                        double hz, bool gate)
{
    const Curve c { thresholdDb, ratio, kneeDb, rangeDb, -70.0, attackMs, signal, hz, gate, o.base.vcaDetector };
    return curveRow (model, c, staircase (o, settingsOf (model, c), signal, hz));
}

std::vector<Row> staticCurve (const Options& o)
{
    const auto model = o.modelOrClean();
    const auto& spec = specOf (model);
    if (! spec.steadyReduction)
        return { unspecified ("static", modelKey (model), "every curve", "BasaltMeasure has no transcription of this model's curve") };

    const auto& grid = o.quick ? spec.staticQuick : spec.staticFull;
    std::vector<Curve> curves;
    for (const auto detector : detectorsFor (o, model))
    {
        for (const auto attack : spec.staticAttacksMs)
            for (const auto signal : { Signal::sine, Signal::carrier })
                for (const auto t : grid.thresholds)
                    for (const auto r : grid.ratios)
                        for (const auto w : grid.knees)
                            for (const auto range : grid.ranges)
                                for (const auto floor : grid.floors)
                                    curves.push_back ({ t, r, w, range, floor, attack, signal, 1000.0, true, detector });

        // 100 Hz is gated for the feedback models (PLAN.md, Static curve), 50 Hz reported for every model.
        if (! o.quick)
            for (const auto hz : { 100.0, 50.0 })
                for (const auto attack : spec.staticAttacksMs)
                {
                    if (spec.lowFrequencyGated && hz >= 100.0)
                    {
                        for (const auto t : grid.thresholds)
                            for (const auto r : grid.ratios)
                                for (const auto w : grid.knees)
                                    for (const auto range : grid.ranges)
                                        curves.push_back ({ t, r, w, range, grid.floors.front(), attack, Signal::sine, hz, true, detector });
                        continue;
                    }
                    for (const auto r : { 2.0, 4.0, 20.0, infiniteRatio })
                        for (const auto w : { 0.0, 6.0 })
                            curves.push_back ({ -30.0, r, w, 40.0, grid.floors.front(), attack, Signal::sine, hz, false, detector });
                }
    }

    std::vector<Job> jobs;
    for (const auto& c : curves)
        jobs.push_back ([&o, model, c]
        {
            const auto st = staircase (o, settingsOf (model, c), c.signal, c.hz);
            std::vector<Row> rows { curveRow (model, c, st) };
            // Upward also: output monotonic in input, the lift exactly 0 at and under Floor (upward/NOTES.md, Gates).
            if (specOf (model).lifts && c.signal == Signal::carrier)
                for (auto& r : liftShapeRows (st, modelLabel (model, c.detector) + ", shape, carrier", curveItem (specOf (model), c)))
                    rows.push_back (std::move (r));
            return rows;
        });
    for (const auto detector : detectorsFor (o, model))
        jobs.push_back ([&o, model, detector] { return std::vector<Row> { rangeRow (o, model, detector) }; });
    if (runningModel (model) == Model::clean)
        for (const auto attack : o.quick ? std::vector<double> { 10.0 } : std::vector<double> { 10.0, 100.0 })
            for (const auto ratio : o.quick ? std::vector<double> { 4.0 } : std::vector<double> { 2.0, 4.0, infiniteRatio })
                jobs.push_back ([&o, model, attack, ratio] { return std::vector<Row> { pinkRow (o, model, attack, ratio) }; });
    for (auto& j : modelStaticJobs (o, model))
        jobs.push_back (std::move (j));
    return runJobs (jobs, o.jobs);
}

//==============================================================================
StepTimes kit::stepResponse (const Options& o, const Settings& s, double levelDb, double holdSeconds, double tailSeconds, const Samples* burst,
                             const std::string& dump, double releaseFraction)
{
    const auto pre = samplesFor (0.05, o.rate);
    Samples key = silence (pre);
    append (key, burst != nullptr ? *burst : carrier (2 * (samplesFor (holdSeconds, o.rate) / 2), gainOf (levelDb)));
    const auto n1 = (int) key.size();
    append (key, silence (samplesFor (tailSeconds, o.rate)));

    RenderOptions ro { o.rate, { 512 }, {}, {} };
    ro.dump = dump;
    const auto r = renderKey (o.device, s, { key }, o.probe, ro);
    StepTimes t;
    t.applied = r.applied;
    t.trace = r.traces[0].reductionDb;
    t.gain = r.traces[0].gain;
    t.n0 = pre;
    t.n1 = n1;
    t.settledDb = t.trace[(size_t) n1 - 1];
    t.peakDb = *std::max_element (t.trace.begin() + pre, t.trace.begin() + n1);
    if (std::isfinite (t.settledDb) && t.settledDb > 0.0)
    {
        t.attack = crossingSeconds (t.trace, pre, (1.0 - 1.0 / e) * t.settledDb, true, o.rate);
        t.release = crossingSeconds (t.trace, n1, t.settledDb * releaseFraction, false, o.rate);
    }
    return t;
}

namespace
{
    // The release needs 5 × (Attack + Release) to settle; D_final, which the attack is read
    // against, needs 10 × Attack when Release is short (5.4 attack constants read it 0.5 % low).
    double holdFor (double attackMs, double releaseMs) { return std::max ({ 5.0 * (attackMs + releaseMs) / 1000.0, 10.0 * attackMs / 1000.0, 0.25 }); }

    std::vector<Row> stepRows (const Options& o, Model model, VcaDetector detector, const StepCase& c)
    {
        const auto& spec = specOf (model);
        auto s = measurementBase (model);
        s.thresholdDb = stepThresholdDb;
        s.ratio = c.ratio;
        s.kneeDb = c.kneeDb;
        s.attackMs = c.attackMs;
        s.releaseMs = c.releaseMs;
        s.vcaDetector = detector;

        const auto expectedRelease = [&spec] (const Settings& x) { return spec.expectedReleaseMs ? spec.expectedReleaseMs (x) : x.releaseMs; };
        const auto expectedAttack = [&spec] (const Settings& x) { return spec.expectedAttackMs ? spec.expectedAttackMs (x) : x.attackMs; };
        const auto lag = spec.lagMs ? spec.lagMs (s) / 1000.0 : 0.0;
        const auto label = modelLabel (model, detector);
        const auto group = c.kneeDb > 0.0 ? format ("%s, %s, Knee %g", label.c_str(), ratioLabel (c.ratio).c_str(), c.kneeDb)
                                          : format ("%s, %s", label.c_str(), ratioLabel (c.ratio).c_str());
        const auto item = format ("Attack %g ms, Release %g ms", c.attackMs, c.releaseMs);
        const auto hold = std::max (holdFor (c.attackMs, c.releaseMs), spec.stepHoldSeconds ? spec.stepHoldSeconds (s) : 0.0);
        const auto t = stepResponse (o, s, levelFor (spec, s, Signal::carrier, c.depthDb), hold + lag,
                                     4.0 * expectedRelease (s) / 1000.0 + 0.05 + lag, nullptr, dumpPath (o, "ballistics", group, item), spec.releaseFraction);

        const auto attack = expectedAttack (t.applied) / 1000.0, release = expectedRelease (t.applied) / 1000.0;
        const auto attackFraction = spec.attackTolerance ? spec.attackTolerance (t.applied) : 0.10;
        const auto ta = std::max (attackFraction * attack, 0.25 / o.rate), tr = timeTolerance (release, o.rate);
        const auto lagNote = lag > 0.0 ? format ("; measured from the end of the %.3g ms lag", 1000.0 * lag) : std::string();
        const auto knob = [] (double measured, double expected, double knobMs, double expectedMs)
        { return std::abs (knobMs - expectedMs) > 1.0e-9 ? format ("%.4f x the model's %.4g ms (knob %g)", measured / expected, expectedMs, knobMs)
                                                         : format ("%.4f x the knob", measured / expected); };

        std::vector<Row> rows;
        if (! myzel::dsp::isExactly (c.depthDb, stepDepthDb))
        {
            rows.push_back (gated ("ballistics", group, item + format (": overshoot of a step to %g dB", c.depthDb), t.peakDb - t.settledDb, 0.0,
                                   -std::numeric_limits<double>::infinity(), 0.05, "dB", format ("settled %.3f dB", t.settledDb)));
            return rows;
        }
        if (c.attackRow)
            rows.push_back (gated ("ballistics", group, item + ": attack", 1000.0 * (t.attack - lag), 1000.0 * attack, 1000.0 * (attack - ta),
                                   1000.0 * (attack + ta), "ms",
                                   knob (t.attack - lag, attack, t.applied.attackMs, 1000.0 * attack) + format ("; settled %.3f dB", t.settledDb) + lagNote));
        if (c.releaseRow)
            rows.push_back (gated ("ballistics", group, item + (spec.releaseFraction > 0.4 ? ": release to 50 %" : ": release"),
                                   1000.0 * (t.release - lag), 1000.0 * release, 1000.0 * (release - tr), 1000.0 * (release + tr), "ms",
                                   knob (t.release - lag, release, t.applied.releaseMs, 1000.0 * release) + lagNote));
        if (spec.gateOvershoot)
            rows.push_back (gated ("ballistics", group, item + ": overshoot of the step to 10 dB", t.peakDb - t.settledDb, 0.0,
                                   -std::numeric_limits<double>::infinity(), 0.05, "dB"));

        // Clean's floor is 1.01·Attack/κ + δ (clean/NOTES.md, Release with the RMS half; PLAN.md's
        // Ballistics gate). Shown against the cascade's floor alone, what δ makes up for; not gated.
        if (runningModel (model) == Model::clean && c.releaseMs < 1000.0 * release)
        {
            const auto cascadeFloor = 1.01 * t.applied.attackMs / reference::cleanKappa (reference::cleanBlend (t.applied.attackMs)) / 1000.0;
            if (c.releaseMs / 1000.0 < cascadeFloor * 1.1)
                rows.push_back (reported ("ballistics", group, item + ": release against the floor without delta, 1.01 Attack/kappa", 1000.0 * t.release,
                                          1000.0 * cascadeFloor, "ms", format ("%.4f x that floor: the RMS half's drag delta", t.release / cascadeFloor)));
        }
        return rows;
    }

    /**
        Look-ahead (dynamics/NOTES.md, Gate table): the attack side equals the Look-ahead 0 trace
        shifted by the look-ahead, the release side equals it in audio time, within 10⁻⁶ dB (and
        one float step of the gain); in Live, Look-ahead is dimmed.
    */
    std::vector<Row> lookaheadRows (const Options& o, Model model, VcaDetector detector)
    {
        const auto& spec = specOf (model);
        std::vector<Row> rows;
        auto s = measurementBase (model);
        s.thresholdDb = stepThresholdDb;
        s.ratio = 4.0;
        s.attackMs = 10.0;
        s.releaseMs = 150.0;
        s.vcaDetector = detector;
        const auto level = levelFor (spec, s, Signal::carrier, stepDepthDb);
        const auto hold = holdFor (10.0, 150.0), tail = 0.65;

        for (const auto latency : o.latencies)
        {
            s.latency = latency;
            s.lookaheadMs = 0.0;
            const auto plain = stepResponse (o, s, level, hold, tail);
            for (const auto ms : latency == LatencyMode::normal ? std::vector<double> { 3.0, 10.0 } : std::vector<double> { 10.0 })
            {
                s.lookaheadMs = ms;
                const auto ahead = stepResponse (o, s, level, hold, tail);
                const auto shift = latency == LatencyMode::normal ? (int) std::lround (ms / 1000.0 * o.rate) : 0;
                auto attackSide = 0.0, releaseSide = 0.0;
                for (int n = std::max (1, plain.n0 - shift - 64); n < plain.n1 - shift; ++n)
                    attackSide = std::max (attackSide, gainDifferenceDb (ahead.gain[(size_t) n], plain.gain[(size_t) (n + shift)]));
                for (int n = plain.n1; n < (int) plain.gain.size(); ++n)
                    releaseSide = std::max (releaseSide, gainDifferenceDb (ahead.gain[(size_t) n], plain.gain[(size_t) n]));

                const auto group = format ("%s, look-ahead, %s", modelLabel (model, detector).c_str(), latencyName (latency));
                if (latency == LatencyMode::live)
                {
                    rows.push_back (gated ("ballistics", group, format ("Look-ahead %g ms is dimmed in Live: the trace equals Look-ahead 0", ms),
                                           std::max (attackSide, releaseSide), 0.0, 0.0, 1.0e-6, "dB"));
                    continue;
                }
                // The tap settles on whole samples (dynamics/NOTES.md, Look-ahead): the shift is the rounded look-ahead.
                rows.push_back (gated ("ballistics", group, format ("Look-ahead %g ms: attack side = the Look-ahead 0 trace %d samples later", ms, shift),
                                       attackSide, 0.0, 0.0, 1.0e-6, "dB", "largest difference beyond one float step of the gain"));
                rows.push_back (gated ("ballistics", group, format ("Look-ahead %g ms: release side = the Look-ahead 0 trace in audio time", ms),
                                       releaseSide, 0.0, 0.0, 1.0e-6, "dB", "largest difference beyond one float step of the gain"));
            }
        }
        return rows;
    }

    /**
        Range (dynamics/NOTES.md, Gate table): the attack with Range engaged as without, ±10 %:
        Range on the static reduction before feed-forward ballistics, on the state of a feedback loop.
    */
    std::vector<Row> rangeAttackRows (const Options& o, Model model, VcaDetector detector)
    {
        const auto& spec = specOf (model);
        auto s = measurementBase (model);
        s.thresholdDb = stepThresholdDb;
        s.ratio = infiniteRatio;
        s.attackMs = 10.0;
        s.releaseMs = 150.0;
        s.vcaDetector = detector;
        const auto level = stepThresholdDb + 30.0;
        const auto open = stepResponse (o, s, level, holdFor (10.0, 150.0), 0.65);
        s.rangeDb = 10.0;
        const auto clamped = stepResponse (o, s, level, holdFor (10.0, 150.0), 0.65);
        const auto group = format ("%s, Range", modelLabel (model, detector).c_str());
        const auto item = std::string ("R inf, 30 dB over: attack with Range 10 / attack with Range 40");
        const auto note = format ("%.3f ms / %.3f ms", 1000.0 * clamped.attack, 1000.0 * open.attack);

        // A feedback loop keeps Range on its state (dynamics/NOTES.md, Range): its trajectory is the
        // unclamped one until it meets Range, so a step past Range reads early on 63 % of Range. What
        // the clamp must not do is slow the attack: R 20 to 9 dB under Range 10, the loop's demand
        // (R − 1)(L − T) far past it, attacks at the knob.
        if (spec.feedback)
        {
            auto under = measurementBase (model);
            under.thresholdDb = stepThresholdDb;
            under.ratio = 20.0;
            under.attackMs = 10.0;
            under.releaseMs = 150.0;
            under.rangeDb = 10.0;
            const auto t = stepResponse (o, under, levelFor (spec, under, Signal::carrier, 9.0), holdFor (10.0, 150.0), 0.65);
            const auto expected = 10.0 / 1000.0, tol = timeTolerance (expected, o.rate);
            return { gated ("ballistics", group, "R 20, a step to 9 dB under Range 10, its demand past Range: attack at the knob", 1000.0 * t.attack,
                            1000.0 * expected, 1000.0 * (expected - tol), 1000.0 * (expected + tol), "ms", format ("settled %.3f dB", t.settledDb)),
                     reported ("ballistics", group, item, clamped.attack / open.attack, 1.0, "x",
                               note + "; reported: the state clamp cuts the unclamped trajectory at Range, so 63 % of Range comes early") };
        }
        if (! spec.rangeAttackGated || (spec.detectorSwitch && detector == VcaDetector::rms))
            return { reported ("ballistics", group, item, clamped.attack / open.attack, 1.0, "x",
                               note + "; reported: Range acts where the attack is the detector's own rise or the light (the model's NOTES)") };
        return { gated ("ballistics", group, item, clamped.attack / open.attack, 1.0, 0.9, 1.1, "x", note) };
    }

    /** The slope of ln r over [from, to): the local time constant of an exponential tail, s. */
    double tailTimeConstant (const std::vector<double>& r, int from, int to, double rate)
    {
        auto st = 0.0, sy = 0.0, stt = 0.0, sty = 0.0;
        auto count = 0;
        for (int i = from; i < std::min (to, (int) r.size()); ++i)
        {
            if (! (r[(size_t) i] > 0.0))
                return std::numeric_limits<double>::quiet_NaN();
            const auto t = i / rate, y = std::log (r[(size_t) i]);
            st += t;
            sy += y;
            stt += t * t;
            sty += t * y;
            ++count;
        }
        const auto slope = (count * sty - st * sy) / (count * stt - st * st);
        return -1.0 / slope;
    }
    /** Clean's Auto release (clean/NOTES.md, Gates, Auto release). */
    std::vector<Row> autoReleaseRows (const Options& o, Model model)
    {
        const auto& spec = specOf (model);
        std::vector<Row> rows;
        const auto group = format ("%s, Auto release", modelKey (model));
        const auto rate = o.rate;

        auto base = measurementBase (model);
        base.thresholdDb = stepThresholdDb;
        base.ratio = 4.0;
        base.attackMs = 1.0;
        base.autoRelease = true;

        const auto releases = o.quick ? std::vector<double> { 150.0 } : std::vector<double> { 50.0, 150.0, 500.0 };
        for (const auto releaseMs : releases)
        {
            auto s = base;
            s.releaseMs = releaseMs;
            const auto holdMs = 10.0 * std::ceil (std::max (5.0 * (1.0 + releaseMs), 1000.0) / 10.0);
            const auto hold = samplesFor (holdMs / 1000.0, rate);

            struct Burst
            {
                const char* name;
                Samples signal;
                const char* expectation;
            };
            const Burst bursts[] {
                { "1 kHz sine", sine (1000.0, hold, rate, gainOf (levelFor (spec, s, Signal::sine, stepDepthDb))), "1.00" },
                { "carrier", carrier (hold, gainOf (levelFor (spec, s, Signal::carrier, stepDepthDb))), "1.41" },
                { "pink noise", pink (hold, rate, 11u, -22.0), "sqrt(2/C2)" },
            };
            for (const auto& b : bursts)
            {
                const auto t = stepResponse (o, s, 0.0, 0.0, 6.0 * releaseMs / 1000.0 + 0.1, &b.signal);
                Samples key = silence (t.n0);
                append (key, b.signal);
                const auto c2 = reference::crestSquared ({ key }, rate, true, t.n1 - 1);
                const auto expected = std::max (releaseMs / 1000.0 * reference::cleanAutoFactor (c2),
                                             reference::cleanReleaseFloor (t.applied.attackMs, reference::inverseRatioOf (t.applied.ratio)) / 1000.0);
                const auto tol = timeTolerance (expected, rate);
                rows.push_back (gated ("ballistics", group, format ("Release %g ms, %s burst: release (%s x Release)", releaseMs, b.name, b.expectation),
                                       1000.0 * t.release, 1000.0 * expected, 1000.0 * (expected - tol), 1000.0 * (expected + tol), "ms",
                                       format ("%.3f x Release; C2 at the step %.3f", t.release / (releaseMs / 1000.0), c2)));
            }

            // An impulse train: C² far above 128, so the release sits at its clamp, Release/8, and never below.
            {
                const auto n = samplesFor (2.0, rate), period = samplesFor (0.1, rate);
                Samples train = silence (n);
                int last = 0;
                for (int i = 0; i < n; i += period)
                {
                    train[(size_t) i] = 1.0f;
                    last = i;
                }
                const auto t = stepResponse (o, s, 0.0, 0.0, 1.0, &train);
                const auto from = t.n0 + last, to = std::min ((int) t.trace.size(), from + samplesFor (0.05, rate));
                auto peak = from;
                for (int i = from; i < to; ++i)
                    if (t.trace[(size_t) i] > t.trace[(size_t) peak])
                        peak = i;
                const auto release = crossingSeconds (t.trace, peak + 1, t.trace[(size_t) peak] / e, false, rate);
                const auto floor = releaseMs / 8000.0;
                rows.push_back (gated ("ballistics", group, format ("Release %g ms, 10 Hz impulse train: release never below Release/8", releaseMs),
                                       1000.0 * release, 1000.0 * floor, 1000.0 * 0.9 * floor, std::numeric_limits<double>::infinity(), "ms",
                                       format ("%.3f x Release, from the reduction's peak after the last impulse", release / (releaseMs / 1000.0))));
            }
        }

        // Release 5 s on the carrier: √2 × Release, and the guard holds C² through 20 s of silence.
        {
            auto s = base;
            s.releaseMs = 5000.0;
            const auto t = stepResponse (o, s, levelFor (spec, s, Signal::carrier, stepDepthDb), 25.0, 25.0);
            const auto expected = 5.0 * std::sqrt (2.0), tol = timeTolerance (expected, rate);
            rows.push_back (gated ("ballistics", group, "Release 5 s, carrier: release (1.41 x Release)", 1000.0 * t.release, 1000.0 * expected,
                                   1000.0 * (expected - tol), 1000.0 * (expected + tol), "ms", format ("%.3f x Release", t.release / 5.0)));
            const auto early = tailTimeConstant (t.trace, t.n1 + samplesFor (4.0, rate), t.n1 + samplesFor (6.0, rate), rate);
            const auto late = tailTimeConstant (t.trace, t.n1 + samplesFor (19.0, rate), t.n1 + samplesFor (21.0, rate), rate);
            rows.push_back (gated ("ballistics", group, "Release 5 s, carrier: the tail's rate 20 s after the step / at 5 s (the guard)", late / early, 1.0,
                                   0.9, 1.1, "x", format ("time constants %.3f s and %.3f s", early, late)));
        }

        // The first release after a reset starts from C² = 2: a 30 ms burst straight after it, against the transcription.
        {
            auto s = base;
            s.releaseMs = 150.0;
            const auto burst = carrier (samplesFor (0.03, rate), gainOf (levelFor (spec, s, Signal::carrier, stepDepthDb)));
            const auto t = stepResponse (o, s, 0.0, 0.0, 1.0, &burst);
            auto ref = o;
            ref.device = DeviceKind::reference;
            ref.probe = Probe::external;
            const auto expected = stepResponse (ref, s, 0.0, 0.0, 1.0, &burst).release, tol = timeTolerance (expected, rate);
            rows.push_back (gated ("ballistics", group, "Release 150 ms, a 30 ms carrier burst after a reset: release as the transcription's",
                                   1000.0 * t.release, 1000.0 * expected, 1000.0 * (expected - tol), 1000.0 * (expected + tol), "ms"));
        }
        return rows;
    }

}

std::vector<Row> ballistics (const Options& o)
{
    const auto model = o.modelOrClean();
    const auto& spec = specOf (model);

    std::vector<Job> jobs;
    for (const auto detector : detectorsFor (o, model))
    {
        // VCA RMS's times are its own (vca/NOTES.md, Gates): rows in Models.cpp.
        const auto coreTimes = spec.steps && ! (spec.detectorSwitch && detector == VcaDetector::rms);
        if (coreTimes)
            for (const auto& c : spec.steps (o.quick))
                jobs.push_back ([&o, model, detector, c] { return stepRows (o, model, detector, c); });
        if (spec.lookaheadRows)
            jobs.push_back ([&o, model, detector] { return lookaheadRows (o, model, detector); });
        if (! spec.lifts)
            jobs.push_back ([&o, model, detector] { return rangeAttackRows (o, model, detector); });
    }
    if (runningModel (model) == Model::clean)
        jobs.push_back ([&o, model] { return autoReleaseRows (o, model); });
    for (auto& j : modelBallisticsJobs (o, model))
        jobs.push_back (std::move (j));
    if (jobs.empty())
        return { unspecified ("ballistics", modelKey (model), "every step", "BasaltMeasure has no transcription of this model's ballistics") };
    return runJobs (jobs, o.jobs);
}

//==============================================================================
// Distortion (PLAN.md: THD+N of a 1 kHz sine at 6 dB of reduction, Character off, reported for
// every Attack; gated where the model's NOTES gate it, Clean at Attack ≥ 10 ms and Release ≥ 50 ms,
// ≤ −80 dBc; models/clean/NOTES.md, Gates).
namespace
{
    constexpr double distortionHz = 1000.0;
    constexpr double distortionLimitDbc = -80.0;
    constexpr double distortionThresholdDb = -18.0;
    constexpr double distortionRatio = 4.0;

    struct DistortionRun
    {
        Distortion d;
        Settings s;
        bool ok = false;
    };

    DistortionRun distortionRun (const Options& o, Model model, VcaDetector detector, double attackMs, double releaseMs)
    {
        const auto& spec = specOf (model);
        DistortionRun run;
        auto& s = run.s;
        s = measurementBase (model);
        s.thresholdDb = distortionThresholdDb;
        s.ratio = distortionRatio;
        s.attackMs = attackMs;
        s.releaseMs = releaseMs;
        s.vcaDetector = detector;

        // Whole periods of 1 kHz: rate/100 samples hold ten (441 at 44.1 kHz, 480 at 48 kHz).
        const auto tenPeriods = o.rate / 100.0;
        if (std::abs (tenPeriods - std::round (tenPeriods)) > 1.0e-9)
            return run;

        const auto amplitude = gainOf (levelFor (spec, s, Signal::sine, spec.distortionDepthDb));
        // 25 attack constants: a reduction still drifting by 10⁻⁴ dB over the window would read
        // as −100 dBc of THD+N on its own. A lift comes in at 10 dB per Release.
        const auto settleSeconds = std::max ({ 25.0 * attackMs / 1000.0, 0.5, spec.lifts ? 3.0 * releaseMs / 1000.0 + 0.2 : 0.0 });
        const auto settle = samplesFor (settleSeconds, o.rate);
        const auto length = 50 * (int) std::round (tenPeriods);
        const auto n = settle + length + samplesFor (0.02, o.rate);

        Input in;
        in.main = { sine (distortionHz, n, o.rate, amplitude), sine (distortionHz, n, o.rate, amplitude) };
        auto device = makeDevice (o.device);
        const auto out = render (*device, s, in, { o.rate, { 512 }, {}, {} });
        run.d = distortionOf (out.main[0], settle + out.latency, length, o.rate, distortionHz, amplitude);
        run.s = out.applied;
        run.ok = true;
        return run;
    }

    Row distortionRow (const Options& o, Model model, VcaDetector detector, double attackMs, double releaseMs)
    {
        const auto& spec = specOf (model);
        const auto group = format ("%s, R %g, hard knee, 1 kHz at %g dB of static %s", modelLabel (model, detector).c_str(), distortionRatio,
                                   std::abs (spec.distortionDepthDb), spec.lifts ? "lift" : "reduction");
        const auto item = format ("Attack %g ms, Release %g ms", attackMs, releaseMs);
        if (! spec.steadyReduction)
            return unspecified ("distortion", group, item, "BasaltMeasure has no transcription of this model's curve");

        const auto run = distortionRun (o, model, detector, attackMs, releaseMs);
        if (! run.ok)
            return unspecified ("distortion", group, item, "the rate holds no whole number of 1 kHz periods in 10 ms");
        const auto& d = run.d;
        const auto& s = run.s;

        auto note = format ("THD (harmonics 2-10) %.1f dBc; fundamental %s by %.2f dB", d.thd, spec.lifts ? "lifted" : "reduced", std::abs (d.reductionDb));
        if (spec.expectedReleaseMs && spec.expectedReleaseMs (s) > releaseMs * 1.0001)
            note += format ("; Release runs at the model's floor, %.1f ms", spec.expectedReleaseMs (s));
        if (spec.distortionGated && spec.distortionGated (s))
            return gated ("distortion", group, item + ": THD+N", d.thdN, distortionLimitDbc, -std::numeric_limits<double>::infinity(), distortionLimitDbc,
                          "dBc", note);
        return reported ("distortion", group, item + ": THD+N", d.thdN, distortionLimitDbc, "dBc",
                         note + (releaseMs < attackMs ? "; Release under Attack: the hold no longer spans a period" : "; not gated here (the model's NOTES)"));
    }
}

Distortion distortionOf (const Samples& x, int from, int length, double rate, double hz, double inputAmplitude)
{
    const auto project = [&] (double f, double& a, double& b)
    {
        a = b = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const auto w = 2.0 * std::numbers::pi * f * (from + i) / rate;
            a += x[(size_t) (from + i)] * std::sin (w);
            b += x[(size_t) (from + i)] * std::cos (w);
        }
        a *= 2.0 / length;
        b *= 2.0 / length;
    };

    double a, b;
    project (hz, a, b);

    auto residual = 0.0, fundamental = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const auto w = 2.0 * std::numbers::pi * hz * (from + i) / rate;
        const auto fit = a * std::sin (w) + b * std::cos (w);
        residual += std::pow (x[(size_t) (from + i)] - fit, 2.0);
        fundamental += fit * fit;
    }

    auto harmonics = 0.0, third = 0.0;
    for (int h = 2; h <= 10 && h * hz < 0.5 * rate; ++h)
    {
        double ha, hb;
        project (h * hz, ha, hb);
        harmonics += 0.5 * (ha * ha + hb * hb);
        if (h == 3)
            third = 0.5 * (ha * ha + hb * hb);
    }

    Distortion d;
    const auto amplitude = std::hypot (a, b);
    d.thdN = 10.0 * std::log10 (std::max (residual, 1.0e-300) / fundamental);
    d.thd = 10.0 * std::log10 (std::max (harmonics, 1.0e-300) / (0.5 * amplitude * amplitude));
    d.third = 10.0 * std::log10 (std::max (third, 1.0e-300) / (0.5 * amplitude * amplitude));
    d.reductionDb = -20.0 * std::log10 (amplitude / inputAmplitude);
    return d;
}

std::vector<Row> distortion (const Options& o)
{
    const auto model = o.modelOrClean();
    const auto attacks = o.quick ? std::vector<double> { 1.0, 10.0, 30.0 } : std::vector<double> { 0.02, 0.1, 1.0, 10.0, 30.0, 100.0, 300.0 };
    const auto releases = o.quick ? std::vector<double> { 5.0, 50.0, 1000.0 } : std::vector<double> { 5.0, 50.0, 150.0, 500.0, 1000.0, 5000.0 };

    std::vector<Job> jobs;
    for (const auto detector : detectorsFor (o, model))
        for (const auto attack : attacks)
            for (const auto release : releases)
                jobs.push_back ([&o, model, detector, attack, release] { return std::vector<Row> { distortionRow (o, model, detector, attack, release) }; });

    // VCA RMS: a one-pole RMS ripples at 2f; its third harmonic is the formula's within ±1 dB at
    // Attack 15.79 and 50 ms, Release at the detector's floor (vca/NOTES.md, Gates, Distortion).
    if (specOf (model).detectorSwitch)
        for (const auto detector : detectorsFor (o, model))
            if (detector == VcaDetector::rms)
                for (const auto attack : { 15.79, 50.0 })
                    jobs.push_back ([&o, model, attack]
                    {
                        const auto run = distortionRun (o, model, VcaDetector::rms, attack, 5.0);
                        const auto expected = vcaRmsThirdHarmonicDbc (run.s.attackMs, reference::inverseRatioOf (run.s.ratio), distortionHz);
                        return std::vector<Row> { gated ("distortion", "vca rms, the single pole's ripple, Release at the floor",
                                                         format ("Attack %g ms: third harmonic against the formula", attack), run.d.third, expected,
                                                         expected - 1.0, expected + 1.0, "dBc",
                                                         format ("THD+N %.1f dBc, THD %.1f dBc", run.d.thdN, run.d.thd)) };
                    });
    return runJobs (jobs, o.jobs);
}

} // namespace basalt::measure
