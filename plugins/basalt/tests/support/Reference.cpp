#include "support/Reference.h"

#include "common/dsp/FloatCompare.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace basalt::measure::reference
{

double releaseRatio (double rho)
{
    rho = std::max (rho, 1.01);
    const auto target = std::exp (-1.0);

    // The cascade's remaining fraction at t = ρ·τ_A for τ_R = u·τ_A; it rises with u.
    const auto remaining = [rho] (double u)
    {
        if (std::abs (u - 1.0) < 1.0e-7)
            return (1.0 + rho) * std::exp (-rho);
        return (u * std::exp (-rho / u) - std::exp (-rho)) / (u - 1.0);
    };

    auto lo = 0.0, hi = rho;
    for (int i = 0; i < 200 && hi - lo > 1.0e-15 * hi; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (remaining (mid) < target ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

double reduction (double x, double t, double inverseRatio, double w)
{
    const auto d = x - t;
    const auto slope = 1.0 - inverseRatio;
    if (w > 0.0 && 2.0 * std::abs (d) <= w)
        return slope * (d + 0.5 * w) * (d + 0.5 * w) / (2.0 * w);
    return 2.0 * d > w ? slope * d : 0.0;
}

double cleanBlend (double attackMs)
{
    return 0.5 * std::clamp (std::log10 (attackMs / 1.0), 0.0, 1.0);
}

double cleanReleaseDrag (double attackMs, double inverseRatio)
{
    constexpr auto depth = 10.0;
    const auto b = cleanBlend (attackMs);
    const auto c = std::max (1.0 - inverseRatio, 0.5);
    const auto over = depth / c + 10.0 * std::log10 (2.0) * (1.0 - b);
    return b * c * over * over * (attackMs / 4.0) / (2.0 * (10.0 / std::log (10.0)) * depth);
}

double cleanReleaseFloor (double attackMs, double inverseRatio)
{
    return 1.01 * attackMs / cleanKappa (cleanBlend (attackMs)) + cleanReleaseDrag (attackMs, inverseRatio);
}

double cleanAutoFactor (double c2)
{
    return std::clamp (std::sqrt (2.0 / c2), 0.125, std::sqrt (2.0));
}

namespace
{
    constexpr double crestSeconds = 0.2;
    constexpr int crestUpdate = 16;
    constexpr double crestGuard = 1.0e-20;
    constexpr double flush = 1.0e-30;
    constexpr double snapDb = 1.0e-6;
    constexpr double fadeSeconds = 0.02;
    constexpr double floorDb = -200.0;

    /** Eq. 7 with Clean's guard: C² holds while q < 10⁻²⁰, p and q flush together, C² starts at 2. */
    struct Crest
    {
        double p = 0.0, q = 0.0, c2 = 2.0;

        void push (double k2, double alpha) noexcept
        {
            p = std::max (k2, alpha * p + (1.0 - alpha) * k2);
            q = alpha * q + (1.0 - alpha) * k2;
            if (q < flush)
                p = q = 0.0;
        }

        void update() noexcept
        {
            if (q >= crestGuard)
                c2 = p / q;
        }
    };

    double levelDb (double magnitude) { return magnitude > 0.0 ? std::max (20.0 * std::log10 (magnitude), floorDb) : floorDb; }
    double rmsLevelDb (double meanSquare) { return meanSquare > 0.0 ? std::max (10.0 * std::log10 (2.0 * meanSquare), floorDb) : floorDb; }

    /** Threshold, 1/Ratio, Knee, Range, and λ (Linked only; 0 otherwise), which fades with them. */
    struct CurveSet
    {
        double threshold = 0.0, inverseRatio = 1.0, knee = 0.0, range = 0.0, link = 0.0;

        bool operator== (const CurveSet&) const = default;
    };

    /**
        dynamics/NOTES.md, Clicks: a curve change fades over 20 ms on a raised cosine, the new
        curve's ballistics beside the old; a change during a fade waits for its end, the latest
        winning.
    */
    struct CurveFade
    {
        CurveSet from, to, waiting;
        int steps = 960, position = 960;
        bool pending = false;

        void jump (const CurveSet& c) noexcept
        {
            from = to = c;
            position = steps;
            pending = false;
        }

        void moveTo (const CurveSet& c) noexcept
        {
            if (position < steps || pending)
            {
                pending = ! (c == to);
                waiting = c;
            }
            else if (! (c == to))
            {
                from = to;
                to = c;
                position = 0;
            }
        }

        double next() noexcept
        {
            if (position >= steps)
            {
                if (! pending)
                    return 1.0;
                from = to;
                to = waiting;
                pending = false;
                position = 0;
            }
            ++position;
            return position >= steps ? 1.0 : 0.5 - 0.5 * std::cos (std::acos (-1.0) * position / steps);
        }

        bool fading() const noexcept { return position < steps; }
    };

    /**
        dynamics/NOTES.md, Clicks: Attack and Release glide in ln(ms) over 20 ms on a cubic that
        lands with zero slope and, on a new target, continues the slope it has.
    */
    struct Glide
    {
        double value = 0.0, start = 0.0, target = 0.0, slope = 0.0;
        int steps = 960, position = 960;

        void jump (double v) noexcept
        {
            value = start = target = v;
            slope = 0.0;
            position = steps;
        }

        void moveTo (double v) noexcept
        {
            if (myzel::dsp::isExactly (v, target))
                return;
            const auto u = (double) position / steps;
            slope = position < steps ? (target - start) * 6.0 * u * (1.0 - u) / steps + slope * (1.0 - u) * (1.0 - 3.0 * u) : 0.0;
            start = value;
            target = v;
            position = 0;
        }

        double next() noexcept
        {
            if (position < steps)
            {
                ++position;
                const auto u = (double) position / steps;
                value = position == steps ? target : start + (target - start) * (3.0 * u * u - 2.0 * u * u * u) + slope * steps * (u * u * u - 2.0 * u * u + u);
            }
            return value;
        }

        bool moving() const noexcept { return position < steps; }
    };

    /** The last `length` values, for their maximum (the look-ahead window); a plain scan, the reference needs no speed. */
    struct Window
    {
        std::vector<double> ring;
        size_t write = 0;

        void reset (size_t capacity)
        {
            ring.assign (capacity, 0.0);
            write = 0;
        }

        void push (double v) noexcept
        {
            ring[write] = v;
            write = (write + 1) % ring.size();
        }

        double maxOfLast (size_t length) const noexcept
        {
            auto m = 0.0;
            for (size_t i = 1; i <= std::min (length, ring.size()); ++i)
                m = std::max (m, ring[(write + ring.size() - i) % ring.size()]);
            return m;
        }
    };

    class CleanReference final : public Device
    {
    public:
        explicit CleanReference (bool onePoleOnly) : onePole (onePoleOnly) {}

        void prepare (const Settings& s, double r, int mainChannels, int keyChannels, int) override
        {
            rate = r;
            numMain = mainChannels;
            numKey = keyChannels;
            settings = s;
            delay = s.latency == LatencyMode::normal ? (int) std::lround (0.010 * rate) : 0;

            audio.assign ((size_t) numMain, std::vector<float> ((size_t) delay + 1, 0.0f));
            source.assign (2, std::vector<double> ((size_t) delay + 1, 0.0));
            write = 0;
            for (auto& d : detectors)
            {
                d = {};
                d.peak.reset ((size_t) delay + 2);
                d.meanSquare.reset ((size_t) delay + 2);
            }
            sharedCrest = {};
            sampleCount = 0;

            const auto steps = std::max (1, (int) std::lround (fadeSeconds * rate));
            curve.steps = attackGlide.steps = releaseGlide.steps = steps;
            curve.jump (curveOf (s));
            attackGlide.jump (std::log (s.attackMs));
            releaseGlide.jump (std::log (s.releaseMs));
            inFade = false;
            updateTimes();
        }

        void set (const Settings& s) override
        {
            curve.moveTo (curveOf (s));
            attackGlide.moveTo (std::log (s.attackMs));
            releaseGlide.moveTo (std::log (s.releaseMs));
            const auto latencyChanged = s.latency != settings.latency;
            settings = s;
            if (latencyChanged)
                prepare (s, rate, numMain, numKey, 0);
            updateTimes();
        }

        int latency() const override { return delay; }
        Settings applied() const override { return settings; }

        void process (float* const* main, int mainChannels, const float* const* key, int keyChannels, int n) override
        {
            for (int i = 0; i < n; ++i)
                processSample (main, mainChannels, key, keyChannels, i);
        }

    private:
        struct DetectorState
        {
            double m = 0.0, y1 = 0.0, yL = 0.0, alphaR = 0.0;
            double y1New = 0.0, yLNew = 0.0;    // the new curve's ballistics during a curve fade
            Window peak, meanSquare;
            Crest crest;
        };

        static CurveSet curveOf (const Settings& s)
        {
            return { s.thresholdDb, inverseRatioOf (s.ratio), s.kneeDb, s.rangeDb, s.stereo == StereoMode::linked ? s.linkPercent / 100.0 : 0.0 };
        }

        /** The time constants from the glided Attack and Release (the one-pole known answer reads the settings as they are). */
        void updateTimes()
        {
            if (onePole)
            {
                alphaA = std::exp (-1.0 / (settings.attackMs / 1000.0 * rate));
                baseAlphaR = std::exp (-1.0 / (settings.releaseMs / 1000.0 * rate));
            }
            else
            {
                attackMs = std::exp (attackGlide.value);
                releaseMs = std::exp (releaseGlide.value);
                blend = cleanBlend (attackMs);
                tauA = attackMs / 1000.0 / cleanKappa (blend);
                alphaA = std::exp (-1.0 / (tauA * rate));
                beta = std::exp (-1.0 / (attackMs / 4000.0 * rate));
                drag = cleanReleaseDrag (attackMs, curve.to.inverseRatio) / 1000.0;
                baseAlphaR = releaseAlpha (releaseMs / 1000.0);
            }
            alphaC = std::exp (-1.0 / (crestSeconds * rate));
            lookahead = settings.latency == LatencyMode::live ? 0
                                                              : std::clamp ((int) std::lround (settings.lookaheadMs / 1000.0 * rate), 0, delay);
            for (auto& d : detectors)
                d.alphaR = baseAlphaR;
            if (settings.autoRelease && ! onePole)
                updateAutoRelease();
        }

        /** clean/NOTES.md, Release with the RMS half: δ comes off the release before the h(ρ) solve. */
        double releaseAlpha (double releaseSeconds) const
        {
            const auto tauR = tauA * releaseRatio ((releaseSeconds - drag) / tauA);
            return std::exp (-1.0 / (tauR * rate));
        }

        void updateAutoRelease()
        {
            for (int c = 0; c < 2; ++c)
            {
                const auto& crest = linkedCrest() ? sharedCrest : detectors[(size_t) c].crest;
                detectors[(size_t) c].alphaR = releaseAlpha (releaseMs / 1000.0 * cleanAutoFactor (crest.c2));
            }
        }

        /** dynamics/NOTES.md, Implementation (M1): a linked pair steers its release from one crest detector while Link > 0. */
        bool linkedCrest() const { return settings.stereo == StereoMode::linked && numMain == 2 && curve.to.link > 0.0; }

        /** Which detector channels run: the stereo matrix (dynamics/NOTES.md, Stereo). */
        int numDetectors() const
        {
            if (numMain == 1)
                return 1;
            return settings.stereo == StereoMode::mid || settings.stereo == StereoMode::side ? 1 : 2;
        }

        void processSample (float* const* main, int mainChannels, const float* const* key, int keyChannels, int i)
        {
            const auto size = (int) audio[0].size();
            for (int c = 0; c < mainChannels; ++c)
                audio[(size_t) c][(size_t) write] = main[c][i];

            // The detector's source as L, R: the main input, or the side chain (mono fed to both, stereo summed for a mono main).
            std::array<double, 2> in {};
            const auto external = settings.key == KeySource::external;
            if (external && keyChannels == 0)
                in = { 0.0, 0.0 };
            else if (external && (keyChannels == 1 || mainChannels == 1))
            {
                const auto k = keyChannels == 1 ? (double) key[0][i] : 0.5 * ((double) key[0][i] + key[1][i]);
                in = { k, k };
            }
            else if (external)
                in = { (double) key[0][i], (double) key[1][i] };
            else
                in = { (double) main[0][i], (double) main[mainChannels - 1][i] };
            source[0][(size_t) write] = in[0];
            source[1][(size_t) write] = in[1];

            // The key is delayed by the latency less the look-ahead; the audio by the latency.
            const auto keyAt = (size_t) ((write - (delay - lookahead) + size) % size);
            const auto audioAt = (size_t) ((write + 1) % size);
            write = (write + 1) % size;

            const auto l = source[0][keyAt], r = source[1][keyAt];
            std::array<double, 2> k { l, r };
            if (numMain == 2 && settings.stereo == StereoMode::mid)
                k = { 0.5 * (l + r), 0.0 };
            else if (numMain == 2 && settings.stereo == StereoMode::side)
                k = { 0.5 * (l - r), 0.0 };
            else if (numMain == 2 && settings.stereo == StereoMode::midSide)
                k = { 0.5 * (l + r), 0.5 * (l - r) };

            if (! onePole && (attackGlide.moving() || releaseGlide.moving()))
            {
                attackGlide.next();
                releaseGlide.next();
                updateTimes();
            }

            const auto weight = curve.next();
            const auto fading = curve.fading();
            const auto& to = curve.to;
            const auto& from = curve.from;
            const auto channels = numDetectors();

            if (settings.autoRelease && ! onePole)
            {
                // Every crest detector runs all the time, so a change of Link or mode never starts one cold.
                if (numMain == 2)
                    sharedCrest.push (std::max (k[0] * k[0], k[1] * k[1]), alphaC);
                for (int c = 0; c < channels; ++c)
                    detectors[(size_t) c].crest.push (k[(size_t) c] * k[(size_t) c], alphaC);
                if (sampleCount % crestUpdate == 0)
                {
                    sharedCrest.update();
                    for (auto& d : detectors)
                        d.crest.update();
                    updateAutoRelease();
                }
            }
            ++sampleCount;

            const auto reductionOf = [this] (const CurveSet& cs, double peakDb, double rmsDb)
            {
                if (onePole)
                    return reduction (peakDb, cs.threshold, cs.inverseRatio, cs.knee);
                return (1.0 - blend) * reduction (peakDb, cs.threshold, cs.inverseRatio, cs.knee)
                       + blend * reduction (rmsDb, cs.threshold, cs.inverseRatio, cs.knee);
            };

            std::array<double, 2> target {}, before {};
            for (int c = 0; c < channels; ++c)
            {
                auto& d = detectors[(size_t) c];
                const auto kc = k[(size_t) c];
                d.m = beta * d.m + (1.0 - beta) * kc * kc;
                if (d.m < flush)
                    d.m = 0.0;
                d.peak.push (std::abs (kc));
                d.meanSquare.push (d.m);
                const auto window = (size_t) lookahead + 1;
                const auto peakDb = levelDb (d.peak.maxOfLast (window));
                const auto rmsDb = rmsLevelDb (d.meanSquare.maxOfLast (window));
                target[(size_t) c] = reductionOf (to, peakDb, rmsDb);
                before[(size_t) c] = reductionOf (from, peakDb, rmsDb);
            }

            if (channels == 2 && settings.stereo == StereoMode::linked && numMain == 2)
            {
                for (auto [v, lambda] : { std::pair { &target, to.link }, std::pair { &before, from.link } })
                {
                    const auto loudest = std::max ((*v)[0], (*v)[1]);
                    for (auto& x : *v)
                        x = (1.0 - lambda) * x + lambda * loudest;
                }
            }

            const auto step = [this] (double& y1, double& yL, double rc, double alphaR)
            {
                if (onePole)
                {
                    const auto a = rc > yL ? alphaA : baseAlphaR;
                    yL = a * yL + (1.0 - a) * rc;
                }
                else
                {
                    y1 = std::max (rc, alphaR * y1 + (1.0 - alphaR) * rc);
                    yL = alphaA * yL + (1.0 - alphaA) * y1;
                }
                if (y1 < flush)
                    y1 = 0.0;
                if (yL < flush)
                    yL = 0.0;
                return yL;
            };

            std::array<double, 2> applied {};
            for (int c = 0; c < channels; ++c)
            {
                auto& d = detectors[(size_t) c];
                const auto rc = std::min (target[(size_t) c], to.range);
                auto reductionDb = 0.0;
                if (fading)
                {
                    if (! inFade)
                    {
                        d.y1New = d.y1;
                        d.yLNew = d.yL;
                    }
                    const auto old = step (d.y1, d.yL, std::min (before[(size_t) c], from.range), d.alphaR);
                    reductionDb = old + weight * (step (d.y1New, d.yLNew, rc, d.alphaR) - old);
                }
                else
                {
                    if (inFade)
                    {
                        d.y1 = d.y1New;
                        d.yL = d.yLNew;
                    }
                    reductionDb = step (d.y1, d.yL, rc, d.alphaR);
                }
                applied[(size_t) c] = reductionDb < snapDb ? 0.0 : reductionDb;
            }
            inFade = fading;

            std::array<double, 2> gain {};
            for (int c = 0; c < 2; ++c)
                gain[(size_t) c] = myzel::dsp::isExactly (applied[(size_t) c], 0.0) ? 1.0 : std::pow (10.0, -applied[(size_t) c] / 20.0);

            const auto x0 = audio[0][audioAt];
            const auto x1 = audio[(size_t) mainChannels - 1][audioAt];
            if (myzel::dsp::isExactly (gain[0], 1.0) && myzel::dsp::isExactly (gain[1], 1.0))
            {
                main[0][i] = x0;
                if (mainChannels == 2)
                    main[1][i] = x1;
                return;
            }

            if (mainChannels == 1)
            {
                main[0][i] = x0 * (float) gain[0];
                return;
            }

            switch (settings.stereo)
            {
                case StereoMode::linked:
                case StereoMode::unlinked:
                    main[0][i] = x0 * (float) gain[0];
                    main[1][i] = x1 * (float) gain[1];
                    break;
                case StereoMode::mid:
                case StereoMode::side:
                case StereoMode::midSide:
                {
                    auto m = 0.5 * ((double) x0 + x1), s = 0.5 * ((double) x0 - x1);
                    if (settings.stereo == StereoMode::side)
                        s *= gain[0];
                    else
                        m *= gain[0];
                    if (settings.stereo == StereoMode::midSide)
                        s *= gain[1];
                    main[0][i] = (float) (m + s);
                    main[1][i] = (float) (m - s);
                    break;
                }
            }
        }

        const bool onePole;
        Settings settings;
        double rate = 48000.0;
        int numMain = 2, numKey = 0, delay = 0, lookahead = 0, write = 0;
        long long sampleCount = 0;
        std::vector<std::vector<float>> audio;
        std::vector<std::vector<double>> source;
        std::array<DetectorState, 2> detectors;
        Crest sharedCrest;
        CurveFade curve;
        Glide attackGlide, releaseGlide;    // ln ms
        bool inFade = false;
        double attackMs = 10.0, releaseMs = 150.0, drag = 0.0;
        double blend = 0.0, tauA = 0.01, alphaA = 0.0, baseAlphaR = 0.0, beta = 0.0, alphaC = 0.0;
    };
}

double crestSquared (const Channels& key, double rate, bool linked, int at, int channel)
{
    const auto alpha = std::exp (-1.0 / (crestSeconds * rate));
    Crest crest;
    for (int i = 0; i <= at && i < (int) key[0].size(); ++i)
    {
        const auto l = (double) key[0][(size_t) i];
        const auto r = (double) key[key.size() > 1 ? 1 : 0][(size_t) i];
        const auto pick = (double) key[(size_t) std::min<int> (channel, (int) key.size() - 1)][(size_t) i];
        crest.push (linked ? std::max (l * l, r * r) : pick * pick, alpha);
        if (i % crestUpdate == 0)
            crest.update();
    }
    return crest.c2;
}

std::unique_ptr<Device> makeDevice (bool onePole)
{
    return std::make_unique<CleanReference> (onePole);
}

} // namespace basalt::measure::reference
