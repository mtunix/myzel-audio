// The later card's networks in a translation unit of their own: the generated headers are large.
#include "dsp/common/PortComposition.h"
#include "dsp/models/console/ConsoleCard.h"
#include "dsp/models/console/generated/ConsoleLaterBack.h"
#include "dsp/models/console/generated/ConsoleLaterCoreZ.h"
#include "dsp/models/console/generated/ConsoleLaterCoupling.h"
#include "dsp/models/console/generated/ConsoleLaterCouplingZ.h"
#include "dsp/models/console/generated/ConsoleLaterFilters.h"
#include "dsp/models/console/generated/ConsoleLaterFrontDirect.h"
#include "dsp/models/console/generated/ConsoleLaterSource.h"
#include "dsp/models/console/generated/ConsoleLaterSourceZ.h"

#include <cassert>
#include <cstdint>

namespace viridian::dsp::console
{

namespace
{
    namespace gen = generated;
    namespace pc = composition;

    template <typename To, typename From>
    void copyControls (const double* from, double* to) noexcept
    {
        for (int i = 0; i < To::numControls; ++i)
        {
            const auto j = controlIndex<From> (To::controls[(size_t) i].name);
            to[i] = j >= 0 ? from[j] : To::controls[(size_t) i].defaultValue;
        }
    }

    /**
        A source network driving the EQ input, joined at control rate. With the source's
        open-circuit output h1 = n1 / d1, the sense-resistor responses h2 = n2 / d2 = Zs / (Zs + R)
        of its output impedance and h3 = n3 / d3 = Ze / (Ze + R) of the EQ input, and the bands'
        response hc = nc / dc from the EQ input:

            H = h1 Ze / (Ze + Zs) hc = h1 n3 (d2 - n2) / (n2 (d3 - n3) + n3 (d2 - n2)) hc.

        n3 is dc times a constant (both the EQ's determinant) and d1 is (d2 - n2) times what the
        source buffers off (a polynomial q, or a constant), so both cancel exactly:
        H = kappa n1 nc / (q (n2 (d3 - n3) + n3 (d2 - n2))). Each side comes from tools/netlist.
    */
    template <typename Source, typename SourceZ, typename LoadZ, typename Load>
    struct Joined
    {
        static constexpr int numControls = 8;
        static constexpr std::array<NetworkControl, 8> controls { {
            { "hpf_freq", NetworkControl::Kind::pot, 0, 1.0 },
            { "lpf_freq", NetworkControl::Kind::pot, 0, 0.0 },
            { "hf_gain", NetworkControl::Kind::pot, 0, 0.5 },
            { "hf_freq", NetworkControl::Kind::pot, 0, 0.5 },
            { "hf_bell", NetworkControl::Kind::selector, 2, 1.0 },
            { "hmf_gain", NetworkControl::Kind::pot, 0, 0.5 },
            { "hmf_freq", NetworkControl::Kind::pot, 0, 0.5 },
            { "hmf_q", NetworkControl::Kind::pot, 0, 0.5 },
        } };
        static constexpr int maxBlocks = 1;
        static constexpr int maxDegree = poly::maxDegree;
        static constexpr int maxSections = 1;
        static constexpr double omegaRef = Load::omegaRef;
        static_assert (Source::omegaRef == omegaRef && SourceZ::omegaRef == omegaRef && LoadZ::omegaRef == omegaRef);

        using Polynomials = NetworkPolynomials<maxBlocks, maxDegree>;

        static void evaluate (const double* c, Polynomials& out) noexcept
        {
            pc::Poly n1, d1, n2, d2, n3, d3, nc, dc;
            std::uint32_t mask = 0;
            mask |= expandFrom<Source> (c, n1, d1);
            mask |= expandFrom<SourceZ> (c, n2, d2);
            mask |= expandFrom<LoadZ> (c, n3, d3);
            mask |= expandFrom<Load> (c, nc, dc);

            const auto sourceDet = pc::difference (d2, n2);
            const auto den = pc::add (pc::multiply (n2, pc::difference (d3, n3)), pc::multiply (n3, sourceDet));
            const auto q = pc::quotient (d1, sourceDet);
            const std::complex<double> probe { 0.0, 1.0 };
            const auto kappa = (pc::evaluate (n3, probe) / pc::evaluate (dc, probe)).real();
            const auto num = pc::multiply (n1, nc);
            const auto full = pc::multiply (q, den);
            assert (num.degree <= maxDegree && full.degree <= maxDegree);

            out.numBlocks = 1;
            auto& b = out.blocks[0];
            b.numDegree = std::min (num.degree, maxDegree);
            b.denDegree = std::min (full.degree, maxDegree);
            b.controls = mask;
            // One block in sigma, scaled to O(1) as the generated ones.
            double scaleNum = 0.0, scaleDen = 0.0;
            for (int k = 0; k <= b.numDegree; ++k)
                scaleNum = std::max (scaleNum, std::abs (num.c[(size_t) k]));
            for (int k = 0; k <= b.denDegree; ++k)
                scaleDen = std::max (scaleDen, std::abs (full.c[(size_t) k]));
            scaleNum = scaleNum > 0.0 ? scaleNum : 1.0;
            scaleDen = scaleDen > 0.0 ? scaleDen : 1.0;
            for (int k = 0; k <= maxDegree; ++k)
            {
                b.num[(size_t) k] = k <= b.numDegree ? num.c[(size_t) k] / scaleNum : 0.0;
                b.den[(size_t) k] = k <= b.denDegree ? full.c[(size_t) k] / scaleDen : 0.0;
            }
            out.gain = kappa * scaleNum / scaleDen;
        }

        /** Evaluates one side at the shared controls; returns the controls it depends on (ours). */
        template <typename T>
        static std::uint32_t expandFrom (const double* c, pc::Poly& n, pc::Poly& d) noexcept
        {
            std::array<double, (size_t) T::numControls + 1> own {};
            copyControls<T, Joined> (c, own.data());
            typename T::Polynomials p;
            T::evaluate (own.data(), p);
            pc::expand (p, n, d);
            std::uint32_t mask = 0;
            for (int b = 0; b < p.numBlocks; ++b)
                for (int i = 0; i < T::numControls; ++i)
                    if ((p.blocks[(size_t) b].controls >> i) & 1u)
                        if (const auto j = controlIndex<Joined> (T::controls[(size_t) i].name); j >= 0)
                            mask |= 1u << j;
            return mask;
        }
    };

    // With the low-pass in, its op-amp feeds the EQ input through the output coupling; with only
    // the high-pass in, the high-pass's output does, with its own feedback taken there.
    using CoupledFront = Joined<gen::ConsoleLaterCoupling, gen::ConsoleLaterCouplingZ,
                                gen::ConsoleLaterCoreZ, gen::ConsoleLaterFrontDirect>;
    using HighPassFront = Joined<gen::ConsoleLaterSource, gen::ConsoleLaterSourceZ,
                                 gen::ConsoleLaterCoreZ, gen::ConsoleLaterFrontDirect>;

    /** The later card: the front its filter switches call for, then the back. */
    class LaterCard final : public CardChannel
    {
    public:
        void prepare (double sampleRate) override
        {
            const auto [frontPart, backPart] = staggerFor (sampleRate);
            filters.setStagger (backPart, 4);   // the low-pass front's two networks take two turns
            coupled.setStagger (frontPart, 4);
            highPass.setStagger (frontPart, 4);
            direct.setStagger (frontPart, 4);
            back.setStagger (backPart, 4);
            filters.prepare (sampleRate);
            coupled.prepare (sampleRate);
            highPass.prepare (sampleRate);
            direct.prepare (sampleRate);
            back.prepare (sampleRate);
        }

        void align (std::int64_t clock) noexcept override
        {
            filters.align (clock);
            coupled.align (clock);
            highPass.align (clock);
            direct.align (clock);
            back.align (clock);
        }

        void setPots (const Pots& p) noexcept override
        {
            switch (front)
            {
                case Front::lowPass:
                    controls (filters, p);
                    controls (coupled, p);
                    break;
                case Front::highPass: controls (highPass, p); break;
                case Front::direct:   controls (direct, p); break;
            }
            controls (back, p);
        }

        void beginReset (const Pots& p) noexcept override
        {
            front = frontFor (p);
            switch (front)
            {
                case Front::lowPass:
                    controls (filters, p);
                    controls (coupled, p);
                    break;
                case Front::highPass: controls (highPass, p); break;
                case Front::direct:   controls (direct, p); break;
            }
            controls (back, p);
            building = 0;
            withNetwork (0, [] (auto& n) { n.beginReset(); });
        }

        bool resetStep() noexcept override
        {
            bool done = false;
            withNetwork (building, [&done] (auto& n) { done = n.resetStep(); });
            if (! done)
                return false;
            if (++building == (front == Front::lowPass ? 3 : 2))
                return true;
            withNetwork (building, [] (auto& n) { n.beginReset(); });
            return false;
        }

        void clear() noexcept override
        {
            filters.clear();
            coupled.clear();
            highPass.clear();
            direct.clear();
            back.clear();
        }

        void process (double* const* ch, int numChannels, int numSamples) noexcept override
        {
            switch (front)
            {
                case Front::lowPass:
                    filters.process (ch, numChannels, numSamples);
                    coupled.process (ch, numChannels, numSamples);
                    break;
                case Front::highPass: highPass.process (ch, numChannels, numSamples); break;
                case Front::direct:   direct.process (ch, numChannels, numSamples); break;
            }
            back.process (ch, numChannels, numSamples);
        }

        void processPair (CardChannel& otherCard, double* a, double* b, int numSamples) noexcept override
        {
            auto& other = static_cast<LaterCard&> (otherCard);
            if (other.front != front)
            {
                double* chA[] { a };
                double* chB[] { b };
                process (chA, 1, numSamples);
                other.process (chB, 1, numSamples);
                return;
            }
            switch (front)
            {
                case Front::lowPass:
                    filters.processPair (other.filters, a, b, numSamples);
                    coupled.processPair (other.coupled, a, b, numSamples);
                    break;
                case Front::highPass: highPass.processPair (other.highPass, a, b, numSamples); break;
                case Front::direct:   direct.processPair (other.direct, a, b, numSamples); break;
            }
            back.processPair (other.back, a, b, numSamples);
        }

        std::complex<double> response (double hz) const noexcept override { return frontResponse (hz, false) * back.response (hz); }

        std::complex<double> analogResponse (double hz) const noexcept override
        {
            return frontResponse (hz, true) * back.analogResponse (hz);
        }

    private:
        enum class Front
        {
            lowPass,        // the filters to the low-pass's op-amp, then the coupling joined to the bands
            highPass,       // the high-pass joined to the bands
            direct          // the bands alone
        };

        static Front frontFor (const Pots& p) noexcept
        {
            return p.lpfIn ? Front::lowPass : (p.hpfIn ? Front::highPass : Front::direct);
        }

        template <typename T>
        static void controls (Network<T>& n, const Pots& p) noexcept
        {
            typename Network<T>::Controls c {};
            for (int i = 0; i < T::numControls; ++i)
            {
                const std::string_view name { T::controls[(size_t) i].name };
                c[(size_t) i] = name == "hpf_in" ? (p.hpfIn ? 1.0 : 0.0)
                              : name == "hpf_freq" ? p.hpf
                              : name == "lpf_freq" ? p.lpf
                              : name == "hf_gain" ? p.hf.gain
                              : name == "hf_freq" ? p.hf.freq
                              : name == "hf_bell" ? (p.hf.bell ? 1.0 : 0.0)
                              : name == "hmf_gain" ? p.hmf.gain
                              : name == "hmf_freq" ? p.hmf.freq
                              : name == "hmf_q" ? p.hmf.q
                              : name == "lmf_gain" ? p.lmf.gain
                              : name == "lmf_freq" ? p.lmf.freq
                              : name == "lmf_q" ? p.lmf.q
                              : name == "lf_gain" ? p.lf.gain
                              : name == "lf_freq" ? p.lf.freq
                              : name == "lf_bell" ? (p.lf.bell ? 1.0 : 0.0)
                                                  : T::controls[(size_t) i].defaultValue;
            }
            n.setTargets (c);
        }

        /** The networks of this front in signal order, the back last. */
        template <typename Fn>
        void withNetwork (int k, Fn&& fn) noexcept
        {
            const int last = front == Front::lowPass ? 2 : 1;
            if (k == last)
                fn (back);
            else if (front == Front::lowPass)
                k == 0 ? fn (filters) : fn (coupled);
            else if (front == Front::highPass)
                fn (highPass);
            else
                fn (direct);
        }

        std::complex<double> frontResponse (double hz, bool analog) const noexcept
        {
            switch (front)
            {
                case Front::lowPass:
                    return analog ? filters.analogResponse (hz) * coupled.analogResponse (hz)
                                  : filters.response (hz) * coupled.response (hz);
                case Front::highPass: return analog ? highPass.analogResponse (hz) : highPass.response (hz);
                case Front::direct:   return analog ? direct.analogResponse (hz) : direct.response (hz);
            }
            return 1.0;
        }

        Front front = Front::direct;
        int building = 0;
        Network<gen::ConsoleLaterFilters> filters;
        Network<CoupledFront> coupled;
        Network<HighPassFront> highPass;
        Network<gen::ConsoleLaterFrontDirect> direct;
        Network<gen::ConsoleLaterBack> back;
    };
} // namespace

std::unique_ptr<CardChannel> makeLaterCard()
{
    return std::make_unique<LaterCard>();
}

} // namespace viridian::dsp::console
