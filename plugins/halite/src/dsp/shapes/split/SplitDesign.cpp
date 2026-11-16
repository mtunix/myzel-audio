#include "dsp/shapes/split/SplitDesign.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace halite::dsp::splitDesign
{

namespace
{
    // The bases' minimax tables (split/NOTES.md, *The base*): for each u = i / 128 (node 0 the u → 0
    // limit), the (p, q) whose composed cut is closest to its analog prototype at the deepest depth.
    constexpr std::array<double, 69> essBaseP {
        0.6364147078225133, 0.6364147078225133, 0.6365965142359565, 0.6376775643292628,
        0.6374082065911244, 0.6388188919893807, 0.6394257365271461, 0.640220194218292,
        0.6404022673002872, 0.6420302717527495, 0.642975072279357, 0.6438825378333768,
        0.6448326648366383, 0.6454465983574549, 0.6465819786512627, 0.6471581768740398,
        0.6489562365996931, 0.6451065719550102, 0.6421268503364678, 0.6390217601575482,
        0.6354850027507262, 0.6316863171948472, 0.6277653731324572, 0.6239926169008531,
        0.6205354165276378, 0.6173966466065096, 0.6144072559840434, 0.6113080674955875,
        0.6083682047445773, 0.6059381892144062, 0.6031791123674081, 0.5986376486033612,
        0.5933647783123703, 0.5883513127627065, 0.5824984474809165, 0.5769480849765348,
        0.5709957924240516, 0.5643214477719096, 0.5578946620194584, 0.5511971826694981,
        0.5436270889745023, 0.535781767117975, 0.5279980758549115, 0.5198401996047436,
        0.5112602773503849, 0.5022280338642072, 0.4927533222057249, 0.4828264584894162,
        0.47243114680688736, 0.46154583001995636, 0.4501431505002847, 0.43819421303092965,
        0.4256673378846682, 0.4125240144082206, 0.39872615756752705, 0.38423218479124666,
        0.36898894491181017, 0.3529408528250687, 0.33601857067237184, 0.3181398155497621,
        0.2997819031223369, 0.28497719857910636, 0.2736340987876502, 0.2644884732660614,
        0.25656511077081895, 0.2493509301416343, 0.24268378840550825, 0.23658820493677443,
        0.23166941806215346
    };
    constexpr std::array<double, 69> essBaseQ {
        1.0, 0.9999857960032497, 0.9999436677132616, 0.9998783416541439,
        0.9997834646259186, 0.9996818640447115, 0.9995576483096056, 0.9994264455608596,
        0.9992698375735232, 0.9991598174418616, 0.9990359499839219, 0.9989212466214635,
        0.9988329875564818, 0.9987432892725342, 0.9987299528279818, 0.9987034748916375,
        0.9988727340067942, 0.9983423302623105, 0.9978650087786465, 0.9973024413660094,
        0.9965938577051647, 0.9957437267219778, 0.9947678535676903, 0.9937248442469706,
        0.992669190931345, 0.9916238772157024, 0.9905598089580453, 0.9894021286881259,
        0.9882236646676044, 0.9872065461989723, 0.9860167988966801, 0.9840224644693547,
        0.9815531761992504, 0.9790520073191131, 0.9759462403482327, 0.9728345620530536,
        0.9692815596124351, 0.9650832198134228, 0.9608261220026271, 0.9561282190843168,
        0.9505455159717986, 0.9444493691710074, 0.93811489343757, 0.931148515689655,
        0.9234684189164487, 0.9150162680034533, 0.9057461241096395, 0.8956004438713464,
        0.8845123892273087, 0.8724054750822539, 0.859192051012706, 0.8447772008062283,
        0.8290566454051829, 0.8119108135797951, 0.7932126529049914, 0.7728221345387056,
        0.7505736290991307, 0.7262861844737509, 0.6997451249567683, 0.6706996058753241,
        0.6397993014978269, 0.6138397436358008, 0.5932043740791947, 0.5761063591743246,
        0.5610298022034439, 0.5471522166316565, 0.5342319635539534, 0.5223508516679737,
        0.5127218258356667
    };
    constexpr std::array<double, 69> harshBaseP {
        0.6268941044312142, 0.6268941044312142, 0.627054465007723, 0.6274402287187597,
        0.6277458737289894, 0.6286023047321742, 0.6287710309168759, 0.6297834245738683,
        0.630185137864927, 0.6312925493286892, 0.6321521466761364, 0.632880509487856,
        0.6337595011782585, 0.634832002187733, 0.6359368564586803, 0.6366532607136035,
        0.6375496574741805, 0.6384678816363023, 0.6391404000450869, 0.643782019532399,
        0.6368337013992894, 0.6334170729355714, 0.6296609690698449, 0.6255866870055449,
        0.6211373054430165, 0.6163380458579846, 0.6118321354040922, 0.6074071697961259,
        0.6033317351899448, 0.5989225566917629, 0.5952322898074587, 0.5912321612362845,
        0.5878952465345222, 0.5842015217666746, 0.5810065932159271, 0.5775782772138869,
        0.5717375541221823, 0.5651134257591119, 0.5587042388625725, 0.5519909508912033,
        0.5446758530762135, 0.5370107171433478, 0.5291064965694304, 0.5209990628926153,
        0.5125041277528362, 0.5035640478827487, 0.49418797002386117, 0.48436982406547757,
        0.4740790017407832, 0.46330403611703197, 0.452018403453439, 0.44019082310001006,
        0.4277902157707931, 0.4147836452054242, 0.4011273059226914, 0.38678143895401773,
        0.3716921489421243, 0.3558015927942888, 0.33903956079053454, 0.32132085855928727,
        0.303123959535836, 0.28741701280826637, 0.2744946808114766, 0.2639497862064861,
        0.2553003671287921, 0.24811823530210778, 0.24209383135061086, 0.237027797753819,
        0.2411075313290414
    };
    constexpr std::array<double, 69> harshBaseQ {
        1.0, 0.999981399646132, 0.9999260870800337, 0.9998365352234302,
        0.9997124709013718, 0.9995648985527632, 0.9993829558937782, 0.999191355031755,
        0.9989687109426552, 0.9987590415828473, 0.9985361591034462, 0.9983080995920934,
        0.9980973217123152, 0.9979114729850672, 0.9977597225471506, 0.99759832881296,
        0.9974961254502792, 0.9974455678546963, 0.9974150801817013, 0.9981247740502054,
        0.9968451440102138, 0.9961026198728602, 0.9952002737952408, 0.9941213516148404,
        0.9928336583023294, 0.9913144480907341, 0.9897463756418456, 0.9880768162991314,
        0.9863870550818677, 0.9844518649373466, 0.9827115988667581, 0.9807234492805359,
        0.9789652606611001, 0.9769615227304698, 0.9751487547267481, 0.9731614689782067,
        0.9697007451691044, 0.9655465265963168, 0.9613176162519071, 0.956653058055827,
        0.9512786365527716, 0.9453554488087419, 0.938947601824397, 0.9320637740823439,
        0.9244978560889019, 0.9161733896298752, 0.9070457034978974, 0.8970618533559089,
        0.8861400942801361, 0.8742156032670463, 0.8612027341529032, 0.8470048874184963,
        0.8315193813832626, 0.8146347333866686, 0.7962181093380096, 0.7761327044940822,
        0.7542140780679816, 0.730278298763771, 0.7041106625487874, 0.6754571244117036,
        0.6449700473066051, 0.6176336380605464, 0.5943718907955595, 0.5748649091627887,
        0.5585325139497092, 0.5447735925581397, 0.5331165274203991, 0.5232458456342434,
        0.5313195732826685
    };

    using Complex = std::complex<double>;

    /** N (t) = B (t) − (1 − G) A (t), ascending. */
    std::array<double, Prototype::maxOrder + 1> numerator (const Prototype& p, double gain) noexcept
    {
        std::array<double, Prototype::maxOrder + 1> n {};
        for (int k = 0; k <= p.order; ++k)
            n[(size_t) k] = p.b[(size_t) k] - (1.0 - gain) * p.a[(size_t) k];
        return n;
    }

    /** Newton on N from `t` until the step is at rounding level (a real start stays real). */
    Complex polish (const std::array<double, Prototype::maxOrder + 1>& n, int order, Complex t) noexcept
    {
        for (int iteration = 0; iteration < 60; ++iteration)
        {
            Complex value = n[(size_t) order], slope = 0.0;
            for (int k = order - 1; k >= 0; --k)
            {
                slope = slope * t + value;
                value = value * t + n[(size_t) k];
            }
            const auto step = value / slope;
            t -= step;
            if (std::abs (step) <= 1.0e-15 * std::max (1.0, std::abs (t)))
                break;
        }
        return t;
    }

    std::array<double, Prototype::maxOrder + 1> multiply (const std::array<double, Prototype::maxOrder + 1>& x,
                                                          std::initializer_list<double> y) noexcept
    {
        std::array<double, Prototype::maxOrder + 1> r {};
        for (size_t i = 0; i < x.size(); ++i)
        {
            size_t j = 0;
            for (const auto c : y)
            {
                if (i + j < r.size())
                    r[i + j] += x[i] * c;
                ++j;
            }
        }
        return r;
    }

    /**
        From the elliptic low-pass (scipy.signal.ellipap (order, rp, rs), split/NOTES.md): the
        polynomials and the roots per dB, followed continuously from the depth-0 roots −p², so each
        section pairs a numerator root with the denominator root it starts from.
    */
    Prototype make (int order, double deepestDb, std::initializer_list<Root> poles, std::initializer_list<double> zeros, double gain,
                    const Prototype::BaseTable& baseP, const Prototype::BaseTable& baseQ) noexcept
    {
        Prototype p;
        p.order = order;
        p.deepestDb = deepestDb;
        p.deepestGain = std::pow (10.0, -deepestDb / 20.0);
        p.lowPassGain = gain;
        p.baseP = &baseP;
        p.baseQ = &baseQ;

        // B (t) = Π |jΩ − p|²: (t + |p|²)² − 4 Im (p)² t per pair, t + p² per real pole. A (t) = k² Π (Ωz² − t)².
        std::array<double, Prototype::maxOrder + 1> a {}, b {};
        a[0] = gain * gain;
        b[0] = 1.0;
        std::array<Complex, Prototype::maxRoots> starts {};
        for (const auto& pole : poles)
        {
            const auto m = pole.re * pole.re + pole.im * pole.im;
            if (pole.im > 0.0)
            {
                b = multiply (b, { m * m, 2.0 * (pole.re * pole.re - pole.im * pole.im), 1.0 });
                starts[(size_t) p.pairs++] = -Complex (pole.re, pole.im) * Complex (pole.re, pole.im);
            }
            else
            {
                b = multiply (b, { m, 1.0 });
                ++p.reals;
            }
        }
        for (const auto& pole : poles)
            if (! (pole.im > 0.0))
                starts[(size_t) p.pairs] = -pole.re * pole.re;
        for (const auto z : zeros)
            a = multiply (multiply (a, { z * z, -1.0 }), { z * z, -1.0 });
        p.a = a;
        p.b = b;

        // Pairs in order of their root's angle at depth 0 (the study's order): the sharpest first.
        for (int j = 1; j < p.pairs; ++j)
            for (auto k = j; k > 0 && std::arg (starts[(size_t) k]) < std::arg (starts[(size_t) k - 1]); --k)
                std::swap (starts[(size_t) k], starts[(size_t) k - 1]);
        std::size_t i = 0;
        for (const auto& pole : poles)
            p.poles[i++] = pole;
        i = 0;
        for (const auto z : zeros)
            p.zeros[i++] = z;

        const auto roots = p.pairs + p.reals;
        const auto deepest = (int) std::lround (deepestDb);
        p.rows = 2 * deepest + 1;
        const auto store = [&] (int row, const std::array<Complex, Prototype::maxRoots>& t)
        {
            for (int j = 0; j < roots; ++j)
                p.roots[(size_t) row][(size_t) j] = { t[(size_t) j].real(), t[(size_t) j].imag() };
        };
        store (deepest, starts);

        constexpr int substeps = 32;
        for (const auto direction : { 1, -1 })
        {
            auto t = starts;
            for (int row = 1; row <= deepest; ++row)
            {
                for (int s = 1; s <= substeps; ++s)
                {
                    const auto db = direction * (row - 1 + (double) s / substeps);
                    const auto n = numerator (p, std::pow (10.0, -db / 20.0));
                    for (int j = 0; j < roots; ++j)
                        t[(size_t) j] = polish (n, order, t[(size_t) j]);
                }
                store (deepest + direction * row, t);
            }
        }
        return p;
    }

    double lead (const Prototype& p, double gain) noexcept
    {
        return p.b[(size_t) p.order] - (1.0 - gain) * p.a[(size_t) p.order];
    }

    double catmullRom (const Prototype::BaseTable& v, double u) noexcept
    {
        const auto last = (int) v.size() - 1;
        const auto s = std::clamp (u * 128.0, 0.0, (double) last - 1.0e-6);
        const auto i = (int) s;
        const auto t = s - i;
        const auto at = [&] (int k) { return v[(size_t) std::clamp (k, 0, last)]; };
        const auto y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        return 0.5 * (2.0 * y1 + (y2 - y0) * t + (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3) * t * t + (3.0 * (y1 - y2) + y3 - y0) * t * t * t);
    }

    /** One shelf's sections: numerator roots `num`, denominator roots `den`, on one edge's base. */
    Section* shelf (const Prototype& p, const Base& base, const std::array<Root, Prototype::maxRoots>& num,
                    const std::array<Root, Prototype::maxRoots>& den, Section* out) noexcept
    {
        // Each root t gives |1 − w x|² with w = (1 − a1) + t c1; in M2's basis P1 = |1 − w|², P2 = −|w|² / 4.
        const auto pair = [&] (const Root& t, double& p1, double& p2)
        {
            const auto im = t.im * base.c1;
            const auto oneMinus = base.a1 - t.re * base.c1, w = (1.0 - base.a1) + t.re * base.c1;
            p1 = oneMinus * oneMinus + im * im;
            p2 = -0.25 * (w * w + im * im);
        };
        for (int j = 0; j < p.pairs; ++j)
        {
            double p1, p2, q1, q2;
            pair (num[(size_t) j], p1, p2);
            pair (den[(size_t) j], q1, q2);
            *out++ = svf (p1, p2, q1, q2);
        }
        for (int j = p.pairs; j < p.pairs + p.reals; ++j)
            *out++ = onePole (base.a1 - num[(size_t) j].re * base.c1, base.a1 - den[(size_t) j].re * base.c1);
        return out;
    }
}

const Prototype& ess() noexcept
{
    static const auto p = make (6, 24.0,
                                { { -1.0194800097134589, 0.6433439994964246 },
                                  { -0.3697546096964525, 1.0940947562286505 },
                                  { -0.08161482326002682, 1.1323669449896732 } },
                                { 3.92417330409406, 1.5978206792427154, 1.2864337662268048 }, 0.038386529991478666, essBaseP, essBaseQ);
    return p;
}

const Prototype& harsh() noexcept
{
    static const auto p = make (5, 12.0,
                                { { -1.2562664001460317, 0.0 },
                                  { -0.5313946037846892, 1.0562216188862183 },
                                  { -0.10055678798332211, 1.1364096507973003 } },
                                { 1.8531451704201656, 1.2987861294730862 }, 0.39459076854329816, harshBaseP, harshBaseQ);
    return p;
}

double analogPower (const Prototype& p, SplitKind kind, double lowHz, double highHz, double hz, double gain) noexcept
{
    const auto shelfPower = [&] (double cornerHz)
    {
        const auto t = (cornerHz / hz) * (cornerHz / hz);
        auto a = 0.0, b = 0.0;
        for (int k = p.order; k >= 0; --k)
        {
            a = a * t + p.a[(size_t) k];
            b = b * t + p.b[(size_t) k];
        }
        return (b - (1.0 - gain) * a) / (b - (1.0 - 1.0 / gain) * a);
    };
    const auto cut = shelfPower (lowHz);
    return kind == SplitKind::bell ? cut / shelfPower (highHz * guard) : cut;
}

double analogShelfFromPoles (const Prototype& p, double cornerHz, double hz, double gain) noexcept
{
    const auto omega = cornerHz / hz;
    const Complex s (0.0, omega);
    Complex numerator = p.lowPassGain, denominator = 1.0;
    for (int j = 0; j < p.pairs + p.reals; ++j)
    {
        const Complex pole (p.poles[(size_t) j].re, p.poles[(size_t) j].im);
        denominator *= s - pole;
        if (pole.imag() > 0.0)
            denominator *= s - std::conj (pole);
    }
    for (int j = 0; j < p.order / 2; ++j)
        numerator *= (p.zeros[(size_t) j] * p.zeros[(size_t) j] - omega * omega);
    const auto lowPass = std::norm (numerator) / std::norm (denominator);
    const auto rho = lowPass / (1.0 - lowPass);
    return (1.0 + gain * rho) / (1.0 + rho / gain);
}

std::array<Root, Prototype::maxRoots> rootsAt (const Prototype& p, double depthDb) noexcept
{
    const auto position = std::clamp (depthDb + p.deepestDb, 0.0, (double) (p.rows - 1));
    const auto i = std::min ((int) position, p.rows - 2);
    const auto f = position - i;
    std::array<Root, Prototype::maxRoots> r {};
    for (int j = 0; j < p.pairs + p.reals; ++j)
    {
        const auto& x = p.roots[(size_t) i][(size_t) j];
        const auto& y = p.roots[(size_t) i + 1][(size_t) j];
        r[(size_t) j] = { x.re + f * (y.re - x.re), x.im + f * (y.im - x.im) };
    }
    return r;
}

Base baseFor (const Prototype& p, double cornerHz, double sampleRate) noexcept
{
    const auto u = cornerHz / sampleRate;
    const auto pi = std::numbers::pi * u;
    return { catmullRom (*p.baseP, u), catmullRom (*p.baseQ, u) / (pi * pi) };
}

Section svf (double p1, double p2, double q1, double q2) noexcept
{
    const auto iq1 = 1.0 / q1;
    const auto g2 = std::sqrt (iq1);
    const auto g = std::sqrt (g2);
    const auto ig = g * g2 * q1;    // 1 / g, since g⁴ = 1 / q1

    // The floors only guard rounding: |1 − w|, |w| and 1 form a triangle, so each argument is ≥ 0.
    const auto k = std::sqrt (std::max (4.0 * q2 * iq1 + (1.0 + g2) * (1.0 + g2), 1.0e-12)) * ig;
    const auto n2 = std::sqrt (std::max (p1 * iq1, 0.0));
    const auto n1 = std::sqrt (std::max (4.0 * p2 * iq1 + (g2 + n2) * (g2 + n2), 0.0));
    const auto a1 = 1.0 / (1.0 + g * (g + k));
    return { a1, g * a1, g * g * a1, 1.0 - n2, n2 * k - n1 * ig };
}

Section onePole (double p1, double q1) noexcept
{
    const auto g = 1.0 / std::sqrt (q1);
    return { g / (1.0 + g), 0.0, 0.0, 1.0 - std::sqrt (std::max (p1 / q1, 0.0)), 0.0 };
}

void design (const Prototype& p, SplitKind kind, const Base& low, const Base& upper, double gain, Design& out) noexcept
{
    gain = std::clamp (gain, p.deepestGain, 1.0);
    const auto db = -20.0 * std::log10 (gain);
    const auto num = rootsAt (p, db), den = rootsAt (p, -db);

    auto* next = shelf (p, low, num, den, out.sections.data());
    if (kind == SplitKind::bell)
    {
        shelf (p, upper, den, num, next);
        out.gain = 1.0;
    }
    else
    {
        out.gain = std::sqrt (lead (p, gain) / lead (p, 1.0 / gain));
    }
}

} // namespace halite::dsp::splitDesign
