#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <type_traits>
#include <utility>
#include <vector>

namespace umber::dsp::tubes
{

/** Fixed-size vector / row-major matrix used in the audio loop. */
template <int N>
using Vector = std::array<double, (std::size_t) N>;

template <int Rows, int Cols>
using Matrix = std::array<Vector<Cols>, (std::size_t) Rows>;

/**
    A circuit netlist: linear R / C / L elements, one audio input (an ideal voltage source),
    DC supplies, nonlinear current ports and one output voltage. Node 0 is ground.

    A nonlinear port (a, b) is a voltage-controlled current: its controlling voltage is
    V(a) - V(b) and its current flows from a to b through the device. A triode is two ports
    sharing the cathode (grid-cathode, plate-cathode), a pentode three.
*/
struct Netlist
{
    struct Element
    {
        int a = 0, b = 0;
        double value = 0.0;
    };

    struct Terminals
    {
        int a = 0, b = 0;
    };

    int numNodes = 0;
    std::vector<Element> resistors, capacitors, inductors, supplies;
    std::vector<Terminals> ports;
    Terminals input, output;
    int biasSupplyIndex = -1;   // the adjustable supply (see biasSupply), or -1

    constexpr int node() { return ++numNodes; }

    constexpr void resistor  (int a, int b, double ohms)   { resistors.push_back ({ a, b, ohms }); }
    constexpr void capacitor (int a, int b, double farads) { capacitors.push_back ({ a, b, farads }); }
    constexpr void inductor  (int a, int b, double henrys) { inductors.push_back ({ a, b, henrys }); }
    constexpr void supply    (int a, int b, double volts)  { supplies.push_back ({ a, b, volts }); }
    constexpr int  port      (int a, int b)                { ports.push_back ({ a, b }); return (int) ports.size() - 1; }

    /** An adjustable DC supply (0 V nominally) from b to a, set at run time with
        NodalCircuit::setBiasVolts: a bias supply or trim. At most one per netlist. */
    constexpr void biasSupply (int a, int b)
    {
        biasSupplyIndex = (int) supplies.size();
        supplies.push_back ({ a, b, 0.0 });
    }

    constexpr int numStates() const { return (int) (capacitors.size() + inductors.size()); }
};

template <int Rows, int Cols>
using Pattern = std::array<std::array<bool, (std::size_t) Cols>, (std::size_t) Rows>;

/**
    Structural non-zero pattern of a netlist's state-space matrices (see StateSpace), derived
    from its topology alone: a superset of the entries that can be non-zero at any sample rate.

    Ideal voltage sources (the input and the supplies) pin their node, so for every injected
    current they behave like ground: the nodes split into components connected by R / C / L
    elements without passing through a pinned node or ground, and an injection into one
    component cannot move the voltage of another. A matrix entry is structurally zero when its
    row's terminals and its column's injection touch no common component. Typical tube stages
    are mostly such separate islands (each cathode, each grid network, each plate), so most of
    the per-sample products are structurally zero and can be skipped at compile time.

    The derived products (D A, D C, dOut A, ...) are the boolean products of the patterns.
    NodalCircuit::prepare checks that every entry outside the pattern is exactly zero.
*/
template <int NS, int NP>
struct Structure
{
    Pattern<NS, NS> A {};
    Pattern<NS, NP> C {};
    Pattern<NP, NS> D {};
    Pattern<NP, NP> F {};
    std::array<bool, (std::size_t) NS> bIn {}, dOut {};
    std::array<bool, (std::size_t) NP> eIn {}, fOut {};

    // derived products
    Pattern<NP, NS> DA {};
    Pattern<NP, NP> DC {};
    std::array<bool, (std::size_t) NP> DbIn {};
    std::array<bool, (std::size_t) NS> oA {};
    std::array<bool, (std::size_t) NP> oC {};

    /** Everything structurally non-zero (no knowledge of the netlist). */
    static constexpr Structure dense() noexcept
    {
        Structure s;
        auto fill = [] (auto& m) { for (auto& row : m) row.fill (true); };
        fill (s.A); fill (s.C); fill (s.D); fill (s.F); fill (s.DA); fill (s.DC);
        s.bIn.fill (true); s.dOut.fill (true); s.eIn.fill (true); s.fOut.fill (true);
        s.DbIn.fill (true); s.oA.fill (true); s.oC.fill (true);
        return s;
    }
};

/** Structural analysis of a netlist (usable at compile time, see Structure). */
template <int NS, int NP>
constexpr Structure<NS, NP> analyseStructure (const Netlist& net)
{
    const int n = net.numNodes;
    if (n > 63 || net.numStates() != NS || (int) net.ports.size() != NP)
        return Structure<NS, NP>::dense();

    // union-find over nodes; pinned nodes (one side of a grounded source) act as ground
    std::array<int, 64> parent {};
    std::array<bool, 64> pinned {};
    for (int k = 0; k < 64; ++k)
        parent[(size_t) k] = k;

    auto find = [&] (int a)
    {
        while (parent[(size_t) a] != a)
            a = parent[(size_t) a];
        return a;
    };
    auto unite = [&] (int a, int b) { parent[(size_t) find (a)] = find (b); };

    bool floatingInput = false;
    auto pinSource = [&] (Netlist::Terminals t, bool isInput)
    {
        if (t.a == 0 && t.b == 0)
            return;
        if (t.a == 0 || t.b == 0)
            pinned[(size_t) (t.a == 0 ? t.b : t.a)] = true;
        else
        {
            unite (t.a, t.b); // floating source: couples its terminals (conservative)
            floatingInput = floatingInput || isInput;
        }
    };

    pinSource (net.input, true);
    for (const auto& s : net.supplies)
        pinSource ({ s.a, s.b }, false);

    auto regular = [&] (int node) { return node > 0 && ! pinned[(size_t) node]; };
    auto link = [&] (const Netlist::Element& e)
    {
        if (regular (e.a) && regular (e.b))
            unite (e.a, e.b);
    };

    for (const auto& e : net.resistors)  link (e);
    for (const auto& e : net.capacitors) link (e);
    for (const auto& e : net.inductors)  link (e);

    // component mask of a node / of a pair of terminals
    auto nodeMask = [&] (int node) -> unsigned long long { return regular (node) ? 1ull << find (node) : 0ull; };
    auto pairMask = [&] (int a, int b) { return nodeMask (a) | nodeMask (b); };

    // components driven by the input source: those its pinned node connects to through an element
    const int inputNode = net.input.a != 0 ? net.input.a : net.input.b;
    unsigned long long inputMask = 0;
    auto touchInput = [&] (const Netlist::Element& e)
    {
        if (e.a == inputNode) inputMask |= nodeMask (e.b);
        if (e.b == inputNode) inputMask |= nodeMask (e.a);
    };
    for (const auto& e : net.resistors)  touchInput (e);
    for (const auto& e : net.capacitors) touchInput (e);
    for (const auto& e : net.inductors)  touchInput (e);

    // does V(a) - V(b) respond to an injection into the components in mask (input: also pinned)?
    auto responds = [&] (int a, int b, unsigned long long mask, bool inputColumn)
    {
        if (inputColumn && (floatingInput || a == inputNode || b == inputNode))
            return true;
        return (pairMask (a, b) & mask) != 0;
    };

    std::vector<Netlist::Terminals> states;
    for (const auto& c : net.capacitors) states.push_back ({ c.a, c.b });
    for (const auto& l : net.inductors)  states.push_back ({ l.a, l.b });

    Structure<NS, NP> s;
    for (int k = 0; k < NS; ++k)
    {
        const auto t = states[(size_t) k];
        for (int j = 0; j < NS; ++j)
        {
            const auto u = states[(size_t) j];
            s.A[(size_t) k][(size_t) j] = k == j || responds (t.a, t.b, pairMask (u.a, u.b), false);
        }
        for (int q = 0; q < NP; ++q)
        {
            const auto u = net.ports[(size_t) q];
            s.C[(size_t) k][(size_t) q] = responds (t.a, t.b, pairMask (u.a, u.b), false);
        }
        s.bIn[(size_t) k] = responds (t.a, t.b, inputMask, true);
        s.dOut[(size_t) k] = responds (net.output.a, net.output.b, pairMask (t.a, t.b), false);
    }

    for (int p = 0; p < NP; ++p)
    {
        const auto t = net.ports[(size_t) p];
        for (int j = 0; j < NS; ++j)
        {
            const auto u = states[(size_t) j];
            s.D[(size_t) p][(size_t) j] = responds (t.a, t.b, pairMask (u.a, u.b), false);
        }
        for (int q = 0; q < NP; ++q)
        {
            const auto u = net.ports[(size_t) q];
            s.F[(size_t) p][(size_t) q] = responds (t.a, t.b, pairMask (u.a, u.b), false);
        }
        s.eIn[(size_t) p] = responds (t.a, t.b, inputMask, true);
        s.fOut[(size_t) p] = responds (net.output.a, net.output.b, pairMask (t.a, t.b), false);
    }

    for (int p = 0; p < NP; ++p)
    {
        for (int c = 0; c < NS; ++c)
            for (int k = 0; k < NS; ++k)
                s.DA[(size_t) p][(size_t) c] = s.DA[(size_t) p][(size_t) c] || (s.D[(size_t) p][(size_t) k] && s.A[(size_t) k][(size_t) c]);
        for (int q = 0; q < NP; ++q)
            for (int k = 0; k < NS; ++k)
                s.DC[(size_t) p][(size_t) q] = s.DC[(size_t) p][(size_t) q] || (s.D[(size_t) p][(size_t) k] && s.C[(size_t) k][(size_t) q]);
        for (int k = 0; k < NS; ++k)
            s.DbIn[(size_t) p] = s.DbIn[(size_t) p] || (s.D[(size_t) p][(size_t) k] && s.bIn[(size_t) k]);
    }
    for (int c = 0; c < NS; ++c)
        for (int k = 0; k < NS; ++k)
            s.oA[(size_t) c] = s.oA[(size_t) c] || (s.dOut[(size_t) k] && s.A[(size_t) k][(size_t) c]);
    for (int q = 0; q < NP; ++q)
        for (int k = 0; k < NS; ++k)
            s.oC[(size_t) q] = s.oC[(size_t) q] || (s.dOut[(size_t) k] && s.C[(size_t) k][(size_t) q]);

    return s;
}

/**
    Discrete-time nonlinear state-space model of a Netlist (the "DK method": Yeh, Abel & Smith,
    IEEE TASLP 2010; Holters & Zoelzer, EUSIPCO 2015). Reactances are discretised with the
    trapezoidal rule (= the bilinear transform, exactly what a wave digital filter does), so the
    result is equivalent to a WDF whose nonlinear ports all sit at one R-type root, without having
    to derive the scattering matrix by hand.

        p     = D x + eIn u + eDc                      (linear prediction of the port voltages)
        v     = p + F i(v)                             (solved by Newton-Raphson)
        y     = dOut . x + eOutIn u + eOutDc + fOut . i
        x'    = A x + bIn u + bDc + C i

    x are the reactances' history currents, u the audio input voltage, i the port currents.
*/
template <int NS, int NP>
struct StateSpace
{
    Matrix<NS, NS> A {};
    Vector<NS> bIn {}, bDc {}, bBias {};
    Matrix<NS, NP> C {};
    Matrix<NP, NS> D {};
    Vector<NP> eIn {}, eDc {}, eBias {};
    Matrix<NP, NP> F {};
    Vector<NS> dOut {};
    double eOutIn = 0.0, eOutDc = 0.0, eOutBias = 0.0;   // *Bias: response to 1 V on the bias supply
    Vector<NP> fOut {};
};

namespace detail
{
    /** Dense Gaussian elimination with partial pivoting: solves M X = B in place (B becomes X). */
    template <typename T>
    inline void solveDense (std::vector<T>& m, std::vector<T>& b, int n, int numRhs)
    {
        for (int col = 0; col < n; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs (m[(size_t) (r * n + col)]) > std::abs (m[(size_t) (pivot * n + col)]))
                    pivot = r;

            if (pivot != col)
            {
                for (int c = 0; c < n; ++c)
                    std::swap (m[(size_t) (col * n + c)], m[(size_t) (pivot * n + c)]);
                for (int c = 0; c < numRhs; ++c)
                    std::swap (b[(size_t) (col * numRhs + c)], b[(size_t) (pivot * numRhs + c)]);
            }

            const T diag = m[(size_t) (col * n + col)];
            assert (std::abs (diag) > 0.0);

            for (int r = col + 1; r < n; ++r)
            {
                const T f = m[(size_t) (r * n + col)] / diag;
                if (std::abs (f) <= 0.0)
                    continue;
                for (int c = col; c < n; ++c)
                    m[(size_t) (r * n + c)] -= f * m[(size_t) (col * n + c)];
                for (int c = 0; c < numRhs; ++c)
                    b[(size_t) (r * numRhs + c)] -= f * b[(size_t) (col * numRhs + c)];
            }
        }

        for (int r = n - 1; r >= 0; --r)
        {
            for (int c = 0; c < numRhs; ++c)
            {
                T sum = b[(size_t) (r * numRhs + c)];
                for (int k = r + 1; k < n; ++k)
                    sum -= m[(size_t) (r * n + k)] * b[(size_t) (k * numRhs + c)];
                b[(size_t) (r * numRhs + c)] = sum / m[(size_t) (r * n + r)];
            }
        }
    }

    /** Fixed-size solve of M d = rhs with partial pivoting; used inside the audio loop. */
    template <int N>
    inline Vector<N> solveSmall (Matrix<N, N> m, Vector<N> rhs) noexcept
    {
        if constexpr (N == 1)
        {
            return { rhs[0] / m[0][0] };
        }
        else if constexpr (N == 2)
        {
            const double det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
            const double inv = 1.0 / det;
            return { (rhs[0] * m[1][1] - rhs[1] * m[0][1]) * inv,
                     (rhs[1] * m[0][0] - rhs[0] * m[1][0]) * inv };
        }
        else
        {
            for (int col = 0; col < N; ++col)
            {
                int pivot = col;
                for (int r = col + 1; r < N; ++r)
                    if (std::abs (m[(size_t) r][(size_t) col]) > std::abs (m[(size_t) pivot][(size_t) col]))
                        pivot = r;

                std::swap (m[(size_t) col], m[(size_t) pivot]);
                std::swap (rhs[(size_t) col], rhs[(size_t) pivot]);

                const double inv = 1.0 / m[(size_t) col][(size_t) col];
                for (int r = col + 1; r < N; ++r)
                {
                    const double f = m[(size_t) r][(size_t) col] * inv;
                    for (int c = col + 1; c < N; ++c)
                        m[(size_t) r][(size_t) c] -= f * m[(size_t) col][(size_t) c];
                    rhs[(size_t) r] -= f * rhs[(size_t) col];
                }
            }

            Vector<N> out {};
            for (int r = N - 1; r >= 0; --r)
            {
                double sum = rhs[(size_t) r];
                for (int c = r + 1; c < N; ++c)
                    sum -= m[(size_t) r][(size_t) c] * out[(size_t) c];
                out[(size_t) r] = sum / m[(size_t) r][(size_t) r];
            }
            return out;
        }
    }

    /** Guarded reciprocal of a determinant / pivot: keeps a (pathologically) singular matrix finite. */
    inline double guardedInverse (double d) noexcept
    {
        constexpr double tiny = 1.0e-30;
        return 1.0 / (std::abs (d) > tiny ? d : (d < 0.0 ? -tiny : tiny));
    }

    /** Explicit inverse of a 1x1 / 2x2 / 3x3 matrix by the adjugate: one division, short
        dependency chain (used inside the audio loop). */
    template <int N>
    inline Matrix<N, N> invertSmall (const Matrix<N, N>& m) noexcept
    {
        static_assert (N >= 1 && N <= 3);

        if constexpr (N == 1)
        {
            return { { { guardedInverse (m[0][0]) } } };
        }
        else if constexpr (N == 2)
        {
            const double inv = guardedInverse (m[0][0] * m[1][1] - m[0][1] * m[1][0]);
            return { { { m[1][1] * inv, -m[0][1] * inv }, { -m[1][0] * inv, m[0][0] * inv } } };
        }
        else
        {
            const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
            const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
            const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
            const double inv = guardedInverse (m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02);

            return { { { c00 * inv, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv, (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv },
                       { c01 * inv, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv, (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv },
                       { c02 * inv, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv, (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv } } };
        }
    }

} // namespace detail

/** Builds the StateSpace matrices of a netlist for a given sample rate (allocates; not real-time). */
template <int NS, int NP>
StateSpace<NS, NP> discretise (const Netlist& net, double sampleRate)
{
    assert (net.numStates() == NS);
    assert ((int) net.ports.size() == NP);

    const double T = 1.0 / sampleRate;
    const int numNodes = net.numNodes;
    const int numSources = 1 + (int) net.supplies.size();
    const int n = numNodes + numSources;

    // reactances as trapezoidal companions: conductance G and sign (+1 capacitor, -1 inductor)
    struct Companion { int a, b; double g, sign; };
    std::vector<Companion> comps;
    for (auto& c : net.capacitors) comps.push_back ({ c.a, c.b, 2.0 * c.value / T, 1.0 });
    for (auto& l : net.inductors)  comps.push_back ({ l.a, l.b, T / (2.0 * l.value), -1.0 });

    std::vector<double> s ((size_t) (n * n), 0.0);
    auto at = [&] (int r, int c) -> double& { return s[(size_t) (r * n + c)]; };

    auto stamp = [&] (int a, int b, double g)
    {
        if (a > 0) at (a - 1, a - 1) += g;
        if (b > 0) at (b - 1, b - 1) += g;
        if (a > 0 && b > 0) { at (a - 1, b - 1) -= g; at (b - 1, a - 1) -= g; }
    };

    for (auto& r : net.resistors) stamp (r.a, r.b, 1.0 / r.value);
    for (auto& c : comps)         stamp (c.a, c.b, c.g);

    auto stampSource = [&] (int index, int a, int b)
    {
        const int row = numNodes + index;
        if (a > 0) { at (a - 1, row) += 1.0; at (row, a - 1) += 1.0; }
        if (b > 0) { at (b - 1, row) -= 1.0; at (row, b - 1) -= 1.0; }
    };

    stampSource (0, net.input.a, net.input.b);
    for (size_t k = 0; k < net.supplies.size(); ++k)
        stampSource (1 + (int) k, net.supplies[k].a, net.supplies[k].b);

    // right-hand sides: [states | audio input | DC supplies (combined) | ports | bias supply (1 V)]
    const int colIn = NS, colDc = NS + 1, colPort = NS + 2, colBias = NS + 2 + NP, numRhs = NS + 3 + NP;
    std::vector<double> rhs ((size_t) (n * numRhs), 0.0);
    auto put = [&] (int node, int col, double value)
    {
        if (node > 0)
            rhs[(size_t) ((node - 1) * numRhs + col)] += value;
    };

    for (int k = 0; k < NS; ++k)
    {
        put (comps[(size_t) k].a, k,  1.0);
        put (comps[(size_t) k].b, k, -1.0);
    }

    rhs[(size_t) (numNodes * numRhs + colIn)] = 1.0;
    for (size_t k = 0; k < net.supplies.size(); ++k)
        rhs[(size_t) ((numNodes + 1 + (int) k) * numRhs + colDc)] = net.supplies[k].value;
    if (net.biasSupplyIndex >= 0)
        rhs[(size_t) ((numNodes + 1 + net.biasSupplyIndex) * numRhs + colBias)] = 1.0;

    for (int q = 0; q < NP; ++q)
    {
        put (net.ports[(size_t) q].a, colPort + q, -1.0);
        put (net.ports[(size_t) q].b, colPort + q,  1.0);
    }

    detail::solveDense (s, rhs, n, numRhs);

    auto sel = [&] (int a, int b, int col)
    {
        double v = 0.0;
        if (a > 0) v += rhs[(size_t) ((a - 1) * numRhs + col)];
        if (b > 0) v -= rhs[(size_t) ((b - 1) * numRhs + col)];
        return v;
    };

    StateSpace<NS, NP> ss;

    for (int k = 0; k < NS; ++k)
    {
        const auto& c = comps[(size_t) k];
        const double f = c.sign * 2.0 * c.g;
        for (int j = 0; j < NS; ++j)
            ss.A[(size_t) k][(size_t) j] = f * sel (c.a, c.b, j) - (k == j ? c.sign : 0.0);
        ss.bIn[(size_t) k] = f * sel (c.a, c.b, colIn);
        ss.bDc[(size_t) k] = f * sel (c.a, c.b, colDc);
        ss.bBias[(size_t) k] = f * sel (c.a, c.b, colBias);
        for (int q = 0; q < NP; ++q)
            ss.C[(size_t) k][(size_t) q] = f * sel (c.a, c.b, colPort + q);
    }

    for (int p = 0; p < NP; ++p)
    {
        const auto& t = net.ports[(size_t) p];
        for (int j = 0; j < NS; ++j)
            ss.D[(size_t) p][(size_t) j] = sel (t.a, t.b, j);
        ss.eIn[(size_t) p] = sel (t.a, t.b, colIn);
        ss.eDc[(size_t) p] = sel (t.a, t.b, colDc);
        ss.eBias[(size_t) p] = sel (t.a, t.b, colBias);
        for (int q = 0; q < NP; ++q)
            ss.F[(size_t) p][(size_t) q] = sel (t.a, t.b, colPort + q);
    }

    const auto& o = net.output;
    for (int j = 0; j < NS; ++j)
        ss.dOut[(size_t) j] = sel (o.a, o.b, j);
    ss.eOutIn = sel (o.a, o.b, colIn);
    ss.eOutDc = sel (o.a, o.b, colDc);
    ss.eOutBias = sel (o.a, o.b, colBias);
    for (int q = 0; q < NP; ++q)
        ss.fOut[(size_t) q] = sel (o.a, o.b, colPort + q);

    return ss;
}

/**
    Real-time nonlinear circuit solver.

    Device: a callable  void (const Vec& v, Vec& i, Mat& dIdV) const noexcept  giving the port
    currents and their Jacobian at port voltages v. It may also provide
    void limit (const Vec& previous, Vec& next) const noexcept,  a SPICE-style junction-voltage
    limiter applied to every Newton iterate (e.g. to stop a plate voltage jumping into the
    region where the tube conducts nothing and the Jacobian is flat), and a
    static constexpr std::array<std::array<bool, NP>, NP> jacobianPattern  marking the
    structurally non-zero dI/dV entries (skipped at compile time otherwise), and a
    static constexpr Structure<NS, NP> structure  (analyseStructure of its netlist) so the
    per-sample state-space products skip the netlist's structural zeros.

    Per sample the Newton-Raphson iteration on v = p + F i(v) is warm-started from the last
    device evaluation: the first Newton step re-uses that linearisation, so it costs no device
    evaluation and is the linear prediction of the new solution (its LU factorisation is cached
    from the previous sample). Iteration stops when every port's Newton step is below its
    tolerance (the remaining error is then second order in that step) or after maxIterations
    device evaluations. The Jacobian is factorised without pivoting: it is -(I + Z G), Z the
    passive port impedances, G the device conductances, whose pivots are <= -1 in practice.
    When its structure allows (see InversePlan) its explicit inverse is cached instead.
*/
template <int NS, int NP, typename Device>
class NodalCircuit
{
public:
    using Vec = Vector<NP>;
    using Mat = Matrix<NP, NP>;

    Device device;
    Vec tolerance {};      // per-port Newton step tolerance, volts
    int maxIterations = 8;

    /** Builds the matrices and solves the operating point (not real-time). */
    void prepare (const Netlist& net, double sampleRate)
    {
        ss = discretise<NS, NP> (net, sampleRate);

        // Runtime copies, column-major so the loops vectorise over rows, plus the products that
        // let next sample's port prediction p' = D x' + ... be formed straight from (x, i):
        //   p' = (D A) x + (D C) i + (D bIn) u + (D bDc + eDc) + eIn u'
        for (size_t r = 0; r < (size_t) NS; ++r)
        {
            for (size_t c = 0; c < (size_t) NS; ++c)
                aT[c][r] = ss.A[r][c];
            for (size_t q = 0; q < (size_t) NP; ++q)
                cT[q][r] = ss.C[r][q];
        }

        for (size_t r = 0; r < (size_t) NP; ++r)
        {
            for (size_t c = 0; c < (size_t) NS; ++c)
            {
                double sum = 0.0;
                for (size_t k = 0; k < (size_t) NS; ++k)
                    sum += ss.D[r][k] * ss.A[k][c];
                daT[c][r] = sum;
            }
            for (size_t q = 0; q < (size_t) NP; ++q)
            {
                double sum = 0.0;
                for (size_t k = 0; k < (size_t) NS; ++k)
                    sum += ss.D[r][k] * ss.C[k][q];
                dcT[q][r] = sum;
            }
            dbIn[r] = 0.0;
            pConst[r] = ss.eDc[r];
            for (size_t k = 0; k < (size_t) NS; ++k)
            {
                dbIn[r]   += ss.D[r][k] * ss.bIn[k];
                pConst[r] += ss.D[r][k] * ss.bDc[k];
            }
        }

        for (size_t c = 0; c < (size_t) NS; ++c)
        {
            oA[c] = 0.0;
            for (size_t k = 0; k < (size_t) NS; ++k)
                oA[c] += ss.dOut[k] * ss.A[k][c];
        }
        for (size_t q = 0; q < (size_t) NP; ++q)
        {
            oC[q] = 0.0;
            for (size_t k = 0; k < (size_t) NS; ++k)
                oC[q] += ss.dOut[k] * ss.C[k][q];
        }
        obIn = 0.0;
        oConstNoY0 = ss.eOutDc;
        for (size_t k = 0; k < (size_t) NS; ++k)
        {
            obIn       += ss.dOut[k] * ss.bIn[k];
            oConstNoY0 += ss.dOut[k] * ss.bDc[k];
        }

        structureExact = checkStructure();
        assert (structureExact);

        // Bias supply: nominal copies of the DC terms and their response per volt.
        hasBias = net.biasSupplyIndex >= 0;
        biasVolts = 0.0;
        nominalDc = { ss.bDc, ss.eDc, ss.eOutDc, pConst, oConstNoY0 };
        for (size_t r = 0; r < (size_t) NP; ++r)
        {
            pConstBias[r] = ss.eBias[r];
            for (size_t k = 0; k < (size_t) NS; ++k)
                pConstBias[r] += ss.D[r][k] * ss.bBias[k];
        }
        oConstBias = ss.eOutBias;
        for (size_t k = 0; k < (size_t) NS; ++k)
            oConstBias += ss.dOut[k] * ss.bBias[k];

        prepareBiasGrid();
        solveOperatingPoint();     // the nominal point (x0, v0, i0, j0, y0), as without a bias supply
        y0Nominal = y0;
        restAtBias = { x0, v0, y0 };
        reset();
    }

    /**
        Range of the bias supply (volts, +-) over which operating points are pre-solved. Set
        before prepare(); 0 (default) for circuits without a bias supply.
    */
    void setBiasRange (double maxVolts) noexcept { biasRange = maxVolts; }

    /**
        Sets the bias supply (volts; 0 = the nominal circuit, exactly). Real-time safe.

        The operating point moves quasi-statically: the states are shifted by the change of
        their rest values (Catmull-Rom through the pre-solved grid), so the circuit sits at its
        new operating point at once, with whatever signal it carries superimposed. Left to
        themselves the coupling capacitors would recharge through the audio path and thump, as
        turning a bias pot on a running amplifier does.
    */
    void setBiasVolts (double volts) noexcept
    {
        if (! hasBias || volts == biasVolts)
            return;

        const auto dv = volts - biasVolts;
        biasVolts = volts;
        applyBiasDc (volts);

        if (! biasGridReady)
            return;

        RestPoint next;
        interpolateRest (volts, next);

        Vector<NS> dx;
        for (size_t c = 0; c < (size_t) NS; ++c)
        {
            dx[c] = next.x[c] - restAtBias.x[c];
            x[c] += dx[c];
        }

        // keep the per-sample predictions consistent: pState = D x + eDc, yState = dOut.x + eOutDc - y0
        for (size_t r = 0; r < (size_t) NP; ++r)
        {
            auto d = ss.eBias[r] * dv;
            for (size_t c = 0; c < (size_t) NS; ++c)
                d += ss.D[r][c] * dx[c];
            pState[r] += d;
            vEval[r] += next.v[r] - restAtBias.v[r];
        }

        double dy = ss.eOutBias * dv - (next.y - restAtBias.y);
        for (size_t c = 0; c < (size_t) NS; ++c)
            dy += ss.dOut[c] * dx[c];
        yState += dy;
        y0 = next.y;

        restAtBias = next;
        evaluateDevice();
    }

    double getBiasVolts() const noexcept { return biasVolts; }

    /** Back to the quiescent (DC) operating point. */
    void reset() noexcept
    {
        // the rest point at the current bias (the nominal one, exactly, at 0)
        if (biasVolts == 0.0 || ! biasGridReady)
            restAtBias = { x0, v0, y0Nominal };
        else
            interpolateRest (biasVolts, restAtBias);

        const auto& rest = restAtBias;
        x = rest.x;
        y0 = rest.y;

        for (size_t r = 0; r < (size_t) NP; ++r)
        {
            pState[r] = ss.eDc[r];
            for (size_t c = 0; c < (size_t) NS; ++c)
                pState[r] += ss.D[r][c] * rest.x[c];
        }

        yState = ss.eOutDc - y0;
        for (size_t c = 0; c < (size_t) NS; ++c)
            yState += ss.dOut[c] * rest.x[c];

        vEval = rest.v;
        evaluateDevice();
        iterationsUsed = 0;
    }

    /** One sample: u is the input voltage, returns the output voltage minus its quiescent value. */
    double process (double u) noexcept
    {
        // Every product below skips the netlist's structural zeros at compile time (see
        // Structure); the remaining terms are summed in the same order as a dense product.
        [[maybe_unused]] static constexpr auto st = structure();
        constexpr auto states = std::make_index_sequence<(size_t) NS> {};
        constexpr auto ports  = std::make_index_sequence<(size_t) NP> {};

        // linear prediction of the port voltages, p = D x + eIn u + eDc
        Vec p;
        forEachIndex (ports, [&] (auto r)
        {
            if constexpr (st.eIn[r])
                p[r] = pState[r] + ss.eIn[r] * u;
            else
                p[r] = pState[r];
        });

        // the parts of the state update and of next sample's predictions that do not depend
        // on this sample's solution (independent of the Newton solve: they overlap with it)
        Vector<NS> next;
        Vec pNext;
        double yNext = oConstNoY0 - y0 + obIn * u;

        forEachIndex (states, [&] (auto r)
        {
            if constexpr (st.bIn[r])
                next[r] = ss.bDc[r] + ss.bIn[r] * u;
            else
                next[r] = ss.bDc[r];
        });
        forEachIndex (ports, [&] (auto r)
        {
            if constexpr (st.DbIn[r])
                pNext[r] = pConst[r] + dbIn[r] * u;
            else
                pNext[r] = pConst[r];
        });

        forEachIndex (states, [&] (auto c)
        {
            forEachIndex (states, [&] (auto r)
            {
                if constexpr (st.A[r][c])
                    next[r] += aT[c][r] * x[c];
            });
            forEachIndex (ports, [&] (auto r)
            {
                if constexpr (st.DA[r][c])
                    pNext[r] += daT[c][r] * x[c];
            });
            if constexpr (st.oA[c])
                yNext += oA[c] * x[c];
        });

        const Vec i = solve (p);

        double y = yState + ss.eOutIn * u;
        forEachIndex (ports, [&] (auto q)
        {
            if constexpr (st.fOut[q])
                y += ss.fOut[q] * i[q];
            if constexpr (st.oC[q])
                yNext += oC[q] * i[q];
            forEachIndex (states, [&] (auto r)
            {
                if constexpr (st.C[r][q])
                    next[r] += cT[q][r] * i[q];
            });
            forEachIndex (ports, [&] (auto r)
            {
                if constexpr (st.DC[r][q])
                    pNext[r] += dcT[q][r] * i[q];
            });
        });

        x = next;
        pState = pNext;
        yState = yNext;
        return y;
    }

    /** True when every entry outside the compile-time Structure is exactly zero (checked by
        prepare; tests assert it at every rate). */
    bool structureHolds() const noexcept { return structureExact; }

    /** Flushes vanishingly small states to zero (call once per block; denormal insurance). */
    void flushTinyStates() noexcept
    {
        for (auto& s : x)
            if (std::abs (s) < 1.0e-200)
                s = 0.0;
    }

    /** Small-signal (linearised at the operating point) gain at a frequency, complex. */
    std::complex<double> smallSignalGain (double hz, double sampleRate) const
    {
        // linearised ports: v = M (D x + eIn u), M = (I - F J0)^-1
        std::vector<double> m ((size_t) (NP * NP)), mRhs ((size_t) (NP * NP), 0.0);
        for (int r = 0; r < NP; ++r)
        {
            for (int c = 0; c < NP; ++c)
            {
                double fj = 0.0;
                for (int k = 0; k < NP; ++k)
                    fj += ss.F[(size_t) r][(size_t) k] * j0[(size_t) k][(size_t) c];
                m[(size_t) (r * NP + c)] = (r == c ? 1.0 : 0.0) - fj;
            }
            mRhs[(size_t) (r * NP + r)] = 1.0;
        }
        detail::solveDense (m, mRhs, NP, NP); // mRhs = M

        // G = J0 M  (port currents per port-voltage prediction)
        std::vector<double> g ((size_t) (NP * NP), 0.0);
        for (int r = 0; r < NP; ++r)
            for (int c = 0; c < NP; ++c)
                for (int k = 0; k < NP; ++k)
                    g[(size_t) (r * NP + c)] += j0[(size_t) r][(size_t) k] * mRhs[(size_t) (k * NP + c)];

        // di = G (D dx + eIn du)
        auto diFromState = [&] (int q, int stateIndex)
        {
            double sum = 0.0;
            for (int k = 0; k < NP; ++k)
                sum += g[(size_t) (q * NP + k)] * ss.D[(size_t) k][(size_t) stateIndex];
            return sum;
        };
        auto diFromInput = [&] (int q)
        {
            double sum = 0.0;
            for (int k = 0; k < NP; ++k)
                sum += g[(size_t) (q * NP + k)] * ss.eIn[(size_t) k];
            return sum;
        };

        std::vector<double> aL ((size_t) (NS * NS)), bL ((size_t) NS), cL ((size_t) NS);
        double dL = ss.eOutIn;
        for (int q = 0; q < NP; ++q)
            dL += ss.fOut[(size_t) q] * diFromInput (q);

        for (int r = 0; r < NS; ++r)
        {
            bL[(size_t) r] = ss.bIn[(size_t) r];
            for (int q = 0; q < NP; ++q)
                bL[(size_t) r] += ss.C[(size_t) r][(size_t) q] * diFromInput (q);

            cL[(size_t) r] = ss.dOut[(size_t) r];
            for (int q = 0; q < NP; ++q)
                cL[(size_t) r] += ss.fOut[(size_t) q] * diFromState (q, r);

            for (int c = 0; c < NS; ++c)
            {
                double sum = ss.A[(size_t) r][(size_t) c];
                for (int q = 0; q < NP; ++q)
                    sum += ss.C[(size_t) r][(size_t) q] * diFromState (q, c);
                aL[(size_t) (r * NS + c)] = sum;
            }
        }

        // H(z) = cL (zI - aL)^-1 bL + dL
        const auto z = std::polar (1.0, 2.0 * std::numbers::pi * hz / sampleRate);
        std::vector<std::complex<double>> zm ((size_t) (NS * NS)), zb ((size_t) NS);
        for (int r = 0; r < NS; ++r)
        {
            for (int c = 0; c < NS; ++c)
                zm[(size_t) (r * NS + c)] = (r == c ? z : 0.0) - aL[(size_t) (r * NS + c)];
            zb[(size_t) r] = bL[(size_t) r];
        }
        detail::solveDense (zm, zb, NS, 1);

        std::complex<double> h = dL;
        for (int r = 0; r < NS; ++r)
            h += cL[(size_t) r] * zb[(size_t) r];
        return h;
    }

    const Vec& operatingPointVoltages() const noexcept { return v0; }
    const Vec& portVoltages() const noexcept { return vEval; }
    const Vector<NS>& states() const noexcept { return x; }
    int lastIterations() const noexcept { return iterationsUsed; }

private:
    using Pattern = std::array<std::array<bool, (std::size_t) NP>, (std::size_t) NP>;

    /** The device's declared netlist Structure (Device::structure), or dense. */
    static constexpr Structure<NS, NP> structure() noexcept
    {
        if constexpr (requires { Device::structure; })
            return Device::structure;
        else
            return Structure<NS, NP>::dense();
    }

    bool checkStructure() const noexcept
    {
        constexpr auto st = structure();
        bool ok = true;
        auto zeroUnless = [&] (bool structural, double value) { ok = ok && (structural || ! (std::abs (value) > 0.0)); };

        for (size_t r = 0; r < (size_t) NS; ++r)
        {
            for (size_t c = 0; c < (size_t) NS; ++c) zeroUnless (st.A[r][c], ss.A[r][c]);
            for (size_t q = 0; q < (size_t) NP; ++q) zeroUnless (st.C[r][q], ss.C[r][q]);
            zeroUnless (st.bIn[r], ss.bIn[r]);
            zeroUnless (st.dOut[r], ss.dOut[r]);
            zeroUnless (st.oA[r], oA[r]);
        }
        for (size_t r = 0; r < (size_t) NP; ++r)
        {
            for (size_t c = 0; c < (size_t) NS; ++c) zeroUnless (st.D[r][c], ss.D[r][c]);
            for (size_t c = 0; c < (size_t) NS; ++c) zeroUnless (st.DA[r][c], daT[c][r]);
            for (size_t q = 0; q < (size_t) NP; ++q) zeroUnless (st.F[r][q], ss.F[r][q]);
            for (size_t q = 0; q < (size_t) NP; ++q) zeroUnless (st.DC[r][q], dcT[q][r]);
            zeroUnless (st.eIn[r], ss.eIn[r]);
            zeroUnless (st.fOut[r], ss.fOut[r]);
            zeroUnless (st.DbIn[r], dbIn[r]);
            zeroUnless (st.oC[r], oC[r]);
        }
        return ok;
    }

    /** Jacobian sparsity of the device (dI_k / dV_c), if it declares one; dense otherwise. */
    static constexpr Pattern jacobianPattern() noexcept
    {
        if constexpr (requires { Device::jacobianPattern; })
        {
            return Device::jacobianPattern;
        }
        else
        {
            Pattern m {};
            for (auto& row : m)
                row.fill (true);
            return m;
        }
    }

    /** Structural pattern of the Newton matrix F dI/dV - I. */
    static constexpr tubes::Pattern<NP, NP> newtonPattern() noexcept
    {
        constexpr auto st = structure();
        constexpr auto jp = jacobianPattern();
        tubes::Pattern<NP, NP> m {};
        for (size_t r = 0; r < (size_t) NP; ++r)
            for (size_t c = 0; c < (size_t) NP; ++c)
            {
                m[r][c] = r == c;
                for (size_t k = 0; k < (size_t) NP; ++k)
                    m[r][c] = m[r][c] || (st.F[r][k] && jp[k][c]);
            }
        return m;
    }

    /**
        Partition for the explicit inverse: a set E of ports that are structurally decoupled
        from each other in the Newton matrix (M[e][e'] = 0 both ways), so M_EE is diagonal and
        is inverted with |E| independent divisions; the rest (at most 3 ports) is the Schur
        complement. decoupled = |E|, or 0 when no such split exists (then the unrolled LU is
        used). Example, Fuzz: E = {grid 1, plate 2}, whose Newton rows only touch their own tube.
    */
    struct InversePlan
    {
        int decoupled = 0;
        std::array<int, (std::size_t) NP> e {}, d {};
    };

    static constexpr InversePlan inversePlan() noexcept
    {
        constexpr auto m = newtonPattern();
        InversePlan result;
        std::array<bool, (std::size_t) NP> in {};
        int k = 0;
        for (int r = 0; r < NP; ++r)
        {
            bool free = true;
            for (int j = 0; j < k; ++j)
                free = free && ! m[(size_t) r][(size_t) result.e[(size_t) j]] && ! m[(size_t) result.e[(size_t) j]][(size_t) r];
            if (free)
            {
                result.e[(size_t) k++] = r;
                in[(size_t) r] = true;
            }
        }

        int q = 0;
        for (int r = 0; r < NP; ++r)
            if (! in[(size_t) r])
                result.d[(size_t) q++] = r;

        result.decoupled = (k >= 2 && q >= 1 && q <= 3) ? k : 0;
        return result;
    }

    static constexpr InversePlan plan = inversePlan();

    /** With a decoupled partition the explicit inverse of the Newton matrix is cached instead
        of LU factors: |E| + 1 independent divisions instead of NP chained ones, and every
        Newton solve becomes a matrix-vector product. */
    static constexpr bool explicitInverse = NP >= 3 && plan.decoupled > 0;

    /**
        Explicit inverse of the Newton matrix M through the decoupled partition (see
        InversePlan), skipping structural zeros:

            M = [A B; C D] (A diagonal),  S = D - C A^-1 B (inverted by the adjugate),
            M^-1 = [A^-1 + A^-1 B S^-1 C A^-1,  -A^-1 B S^-1;  -S^-1 C A^-1,  S^-1]

        (Keeping the block factors and solving by block substitution instead takes fewer
        instructions but lengthens every Newton solve's dependency chain: measured slower.)
    */
    static Mat invertDecoupled (const Mat& m) noexcept
    {
        [[maybe_unused]] static constexpr auto mp = newtonPattern();
        constexpr int K = plan.decoupled, Q = NP - K;
        constexpr auto ks = std::make_index_sequence<(size_t) K> {};
        constexpr auto qs = std::make_index_sequence<(size_t) Q> {};

        Vector<K> ai;
        forEachIndex (ks, [&] (auto k) { ai[k] = detail::guardedInverse (m[plan.e[k]][plan.e[k]]); });

        Matrix<K, Q> aiB {}; // A^-1 B
        Matrix<Q, K> cAi {}; // C A^-1
        forEachIndex (ks, [&] (auto k)
        {
            forEachIndex (qs, [&] (auto j)
            {
                if constexpr (mp[plan.e[k]][plan.d[j]])
                    aiB[k][j] = ai[k] * m[plan.e[k]][plan.d[j]];
                if constexpr (mp[plan.d[j]][plan.e[k]])
                    cAi[j][k] = m[plan.d[j]][plan.e[k]] * ai[k];
            });
        });

        Matrix<Q, Q> schur;
        forEachIndex (qs, [&] (auto i)
        {
            forEachIndex (qs, [&] (auto j)
            {
                double sum = m[plan.d[i]][plan.d[j]];
                forEachIndex (ks, [&] (auto k)
                {
                    if constexpr (mp[plan.d[i]][plan.e[k]] && mp[plan.e[k]][plan.d[j]])
                        sum -= cAi[i][k] * m[plan.e[k]][plan.d[j]];
                });
                schur[i][j] = sum;
            });
        });

        const auto si = detail::invertSmall<Q> (schur);

        Mat out;
        Matrix<Q, K> x21 {};
        forEachIndex (qs, [&] (auto i)
        {
            forEachIndex (qs, [&] (auto j) { out[plan.d[i]][plan.d[j]] = si[i][j]; });
            forEachIndex (ks, [&] (auto k)
            {
                double sum = 0.0;
                forEachIndex (qs, [&] (auto j)
                {
                    if constexpr (mp[plan.d[j]][plan.e[k]])
                        sum -= si[i][j] * cAi[j][k];
                });
                x21[i][k] = sum;
                out[plan.d[i]][plan.e[k]] = sum;
            });
        });

        forEachIndex (ks, [&] (auto k)
        {
            forEachIndex (qs, [&] (auto j)
            {
                double sum = 0.0;
                forEachIndex (qs, [&] (auto l)
                {
                    if constexpr (mp[plan.e[k]][plan.d[l]])
                        sum -= aiB[k][l] * si[l][j];
                });
                out[plan.e[k]][plan.d[j]] = sum;
            });
            forEachIndex (ks, [&] (auto k2)
            {
                double sum = k == k2 ? ai[k] : 0.0;
                forEachIndex (qs, [&] (auto l)
                {
                    if constexpr (mp[plan.e[k]][plan.d[l]])
                        sum -= aiB[k][l] * x21[l][k2];
                });
                out[plan.e[k]][plan.e[k2]] = sum;
            });
        });
        return out;
    }

    template <typename Fn, size_t... I>
    static void forEachIndex (std::index_sequence<I...>, Fn&& fn) noexcept
    {
        (fn (std::integral_constant<size_t, I> {}), ...);
    }

    /** Factorises the Newton Jacobian F dI/dV - I at the last evaluation point. */
    void factoriseNewtonJacobian() noexcept
    {

        [[maybe_unused]] static constexpr auto pattern = jacobianPattern();
        constexpr auto indices = std::make_index_sequence<(size_t) NP> {};

        // F * dI/dV - I, skipping the device's and the netlist's structural zeros at compile time
        [[maybe_unused]] static constexpr auto st = structure();
        Mat m;
        forEachIndex (indices, [&] (auto r)
        {
            forEachIndex (indices, [&] (auto c)
            {
                double sum = r == c ? -1.0 : 0.0;
                forEachIndex (indices, [&] (auto k)
                {
                    if constexpr (pattern[k][c] && st.F[r][k])
                        sum += ss.F[r][k] * jEval[k][c];
                });
                m[r][c] = sum;
            });
        });

        if constexpr (NP == 2)
        {
            const double det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
            const double inv = std::abs (det) > 1.0e-300 ? 1.0 / det : 0.0;
            lu = { { { m[1][1] * inv, -m[0][1] * inv }, { -m[1][0] * inv, m[0][0] * inv } } }; // explicit inverse
                return;
        }
        else if constexpr (explicitInverse)
        {
            lu = invertDecoupled (m);
            return;
        }

        // Doolittle LU without pivoting, unrolled at compile time. The matrix is
        // -(I + Z G): Z the (passive) port impedance matrix, G the device conductances, which
        // are dominated by their diagonal (dI/dV of each port) and lower-triangular
        // transconductances, so every pivot is <= -1 in practice and pivoting is unnecessary.
        // The pivot guard below keeps a pathological matrix finite rather than exact.
        lu = m;
        forEachIndex (indices, [&] (auto col)
        {
            const double d = lu[col][col];
            const double inv = 1.0 / (std::abs (d) > 1.0e-12 ? d : (d < 0.0 ? -1.0e-12 : 1.0e-12));
            invPivot[col] = inv;

            forEachIndex (indices, [&] (auto r)
            {
                if constexpr (r > col)
                {
                    const double f = lu[r][col] * inv;
                    lu[r][col] = f;
                    forEachIndex (indices, [&] (auto c)
                    {
                        if constexpr (c > col)
                            lu[r][c] -= f * lu[col][c];
                    });
                }
            });
        });
    }

    Vec luSolve (const Vec& b) const noexcept
    {
        if constexpr (NP == 2)
        {
            return { lu[0][0] * b[0] + lu[0][1] * b[1], lu[1][0] * b[0] + lu[1][1] * b[1] };
        }
        else if constexpr (explicitInverse)
        {
            Vec out;
            for (size_t r = 0; r < (size_t) NP; ++r)
            {
                double sum = 0.0;
                for (size_t c = 0; c < (size_t) NP; ++c)
                    sum += lu[r][c] * b[c];
                out[r] = sum;
            }
            return out;
        }
        else
        {
            constexpr auto indices = std::make_index_sequence<(size_t) NP> {};
            constexpr auto last = (size_t) NP - 1;
            Vec y;

            forEachIndex (indices, [&] (auto r)
            {
                double sum = b[r];
                forEachIndex (indices, [&] (auto c)
                {
                    if constexpr (c < r)
                        sum -= lu[r][c] * y[c];
                });
                y[r] = sum;
            });

            forEachIndex (indices, [&] (auto k)
            {
                constexpr size_t r = last - decltype (k)::value;
                double sum = y[r];
                forEachIndex (indices, [&] (auto c)
                {
                    if constexpr (c > r)
                        sum -= lu[r][c] * y[c];
                });
                y[r] = sum * invPivot[r];
            });
            return y;
        }
    }

    Vec solve (const Vec& p) noexcept
    {
        [[maybe_unused]] static constexpr auto pattern = jacobianPattern();
        constexpr auto indices = std::make_index_sequence<(size_t) NP> {};

        iterationsUsed = 0;

        for (;;)
        {
            // Newton step on g(v) = p + F i(v) - v from the last evaluation point:
            // step = (F dI/dV - I)^-1 (v - F i - p), with v - F i cached per evaluation and the
            // Jacobian's LU cached from the previous evaluation (possibly the previous sample).
            Vec g;
            for (size_t r = 0; r < (size_t) NP; ++r)
                g[r] = vMinusFi[r] - p[r];

            const Vec step = luSolve (g);

            bool converged = true;
            for (size_t r = 0; r < (size_t) NP; ++r)
                converged &= std::abs (step[r]) <= tolerance[r];

            if (converged)
            {
                // accept the linearised current at the new point (error is O(step^2))
                Vec i = iEval;
                forEachIndex (indices, [&] (auto r)
                {
                    forEachIndex (indices, [&] (auto c)
                    {
                        if constexpr (pattern[r][c])
                            i[r] += jEval[r][c] * step[c];
                    });
                });
                return i;
            }

            if (iterationsUsed >= maxIterations)
                return iEval;

            const Vec previous = vEval;
            for (size_t r = 0; r < (size_t) NP; ++r)
                vEval[r] += std::isfinite (step[r]) ? step[r] : 0.0;

            if constexpr (requires { device.limit (previous, vEval); })
                device.limit (previous, vEval);

            evaluateDevice();
            ++iterationsUsed;
        }
    }

    /** Evaluates the device at vEval and refreshes everything cached from it. */
    void evaluateDevice() noexcept
    {
        device (vEval, iEval, jEval);

        [[maybe_unused]] static constexpr auto st = structure();
        constexpr auto indices = std::make_index_sequence<(size_t) NP> {};
        forEachIndex (indices, [&] (auto r)
        {
            double sum = vEval[r];
            forEachIndex (indices, [&] (auto c)
            {
                if constexpr (st.F[r][c])
                    sum -= ss.F[r][c] * iEval[c];
            });
            vMinusFi[r] = sum;
        });

        factoriseNewtonJacobian();
    }

    struct RestPoint
    {
        Vector<NS> x {};
        Vec v {};
        double y = 0.0;
    };

    struct DcTerms
    {
        Vector<NS> bDc {};
        Vec eDc {};
        double eOutDc = 0.0;
        Vec pConst {};
        double oConstNoY0 = 0.0;
    };

    static constexpr int biasGridSize = 17;   // odd: the middle point is the nominal one

    /** The grid reaches past the usable range, so the knob never uses the (less accurate)
        end segments of the spline. */
    double gridRange() const noexcept { return 1.25 * biasRange; }

    /** The DC terms for a bias supply voltage (exactly nominal at 0). */
    void applyBiasDc (double volts) noexcept
    {
        if (volts == 0.0)
        {
            ss.bDc = nominalDc.bDc;
            ss.eDc = nominalDc.eDc;
            ss.eOutDc = nominalDc.eOutDc;
            pConst = nominalDc.pConst;
            oConstNoY0 = nominalDc.oConstNoY0;
            return;
        }

        for (size_t r = 0; r < (size_t) NS; ++r)
            ss.bDc[r] = nominalDc.bDc[r] + ss.bBias[r] * volts;
        for (size_t r = 0; r < (size_t) NP; ++r)
        {
            ss.eDc[r] = nominalDc.eDc[r] + ss.eBias[r] * volts;
            pConst[r] = nominalDc.pConst[r] + pConstBias[r] * volts;
        }
        ss.eOutDc = nominalDc.eOutDc + ss.eOutBias * volts;
        oConstNoY0 = nominalDc.oConstNoY0 + oConstBias * volts;
    }

    /** Operating points across the bias range, for reset() and quasi-static bias moves. */
    void prepareBiasGrid()
    {
        biasGridReady = false;

        if (! hasBias || biasRange <= 0.0)
            return;

        for (int g = 0; g < biasGridSize; ++g)
        {
            const auto volts = gridRange() * (2.0 * g / (biasGridSize - 1) - 1.0);
            applyBiasDc (g == biasGridSize / 2 ? 0.0 : volts);
            solveOperatingPoint();
            biasGrid[(size_t) g] = { x0, v0, y0 };
        }

        applyBiasDc (0.0);
        biasGridReady = true;
    }

    /** Rest point at any bias: Catmull-Rom (C1) through the grid; exact on grid points. */
    void interpolateRest (double volts, RestPoint& out) const noexcept
    {
        constexpr int last = biasGridSize - 1;
        const auto u = std::clamp ((volts / gridRange() + 1.0) * 0.5 * last, 0.0, (double) last);
        const auto i1 = std::min ((int) u, last - 1);
        const auto t = u - i1;

        if (t == 0.0)
        {
            out = biasGrid[(size_t) i1];
            return;
        }

        const auto& a = biasGrid[(size_t) std::max (i1 - 1, 0)];
        const auto& b = biasGrid[(size_t) i1];
        const auto& c = biasGrid[(size_t) i1 + 1];
        const auto& d = biasGrid[(size_t) std::min (i1 + 2, last)];

        const auto t2 = t * t, t3 = t2 * t;
        const auto w0 = -0.5 * t3 + t2 - 0.5 * t;
        const auto w1 = 1.5 * t3 - 2.5 * t2 + 1.0;
        const auto w2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
        const auto w3 = 0.5 * t3 - 0.5 * t2;

        for (size_t s = 0; s < (size_t) NS; ++s)
            out.x[s] = w0 * a.x[s] + w1 * b.x[s] + w2 * c.x[s] + w3 * d.x[s];
        for (size_t q = 0; q < (size_t) NP; ++q)
            out.v[q] = w0 * a.v[q] + w1 * b.v[q] + w2 * c.v[q] + w3 * d.v[q];
        out.y = w0 * a.y + w1 * b.y + w2 * c.y + w3 * d.y;
    }

    void solveOperatingPoint()
    {
        // discrete fixed point: x = (I - A)^-1 (bDc + C i),  v = eDc + D x + F i
        std::vector<double> ia ((size_t) (NS * NS)), rhs ((size_t) (NS * (NP + 1)));
        for (int r = 0; r < NS; ++r)
        {
            for (int c = 0; c < NS; ++c)
                ia[(size_t) (r * NS + c)] = (r == c ? 1.0 : 0.0) - ss.A[(size_t) r][(size_t) c];
            rhs[(size_t) (r * (NP + 1))] = ss.bDc[(size_t) r];
            for (int q = 0; q < NP; ++q)
                rhs[(size_t) (r * (NP + 1) + 1 + q)] = ss.C[(size_t) r][(size_t) q];
        }
        detail::solveDense (ia, rhs, NS, NP + 1); // rhs = [(I-A)^-1 bDc | (I-A)^-1 C]

        Vec pDc {};
        Mat fDc {};
        for (int r = 0; r < NP; ++r)
        {
            pDc[(size_t) r] = ss.eDc[(size_t) r];
            for (int k = 0; k < NS; ++k)
                pDc[(size_t) r] += ss.D[(size_t) r][(size_t) k] * rhs[(size_t) (k * (NP + 1))];

            for (int q = 0; q < NP; ++q)
            {
                fDc[(size_t) r][(size_t) q] = ss.F[(size_t) r][(size_t) q];
                for (int k = 0; k < NS; ++k)
                    fDc[(size_t) r][(size_t) q] += ss.D[(size_t) r][(size_t) k] * rhs[(size_t) (k * (NP + 1) + 1 + q)];
            }
        }

        // damped Newton with supply stepping (source continuation)
        Vec v {}, i {};
        Mat jac {};
        constexpr int ramps = 40;
        for (int step = 1; step <= ramps; ++step)
        {
            const double lambda = (double) step / ramps;
            for (int it = 0; it < 500; ++it)
            {
                device (v, i, jac);
                Vec g;
                Mat jg;
                for (int r = 0; r < NP; ++r)
                {
                    double sum = lambda * pDc[(size_t) r] - v[(size_t) r];
                    for (int c = 0; c < NP; ++c)
                    {
                        sum += fDc[(size_t) r][(size_t) c] * i[(size_t) c];
                        double fj = 0.0;
                        for (int k = 0; k < NP; ++k)
                            fj += fDc[(size_t) r][(size_t) k] * jac[(size_t) k][(size_t) c];
                        jg[(size_t) r][(size_t) c] = fj - (r == c ? 1.0 : 0.0);
                    }
                    g[(size_t) r] = -sum;
                }

                auto d = detail::solveSmall<NP> (jg, g);
                double largest = 0.0;
                for (auto e : d)
                    largest = std::max (largest, std::abs (e));
                const double scale = largest > 2.0 ? 2.0 / largest : 1.0;
                for (int r = 0; r < NP; ++r)
                    v[(size_t) r] += scale * d[(size_t) r];
                if (largest < 1.0e-12)
                    break;
            }
        }

        device (v, i, jac);
        v0 = v;
        i0 = i;
        j0 = jac;

        for (int r = 0; r < NS; ++r)
        {
            double sum = rhs[(size_t) (r * (NP + 1))];
            for (int q = 0; q < NP; ++q)
                sum += rhs[(size_t) (r * (NP + 1) + 1 + q)] * i[(size_t) q];
            x0[(size_t) r] = sum;
        }

        y0 = ss.eOutDc;
        for (int c = 0; c < NS; ++c)
            y0 += ss.dOut[(size_t) c] * x0[(size_t) c];
        for (int q = 0; q < NP; ++q)
            y0 += ss.fOut[(size_t) q] * i0[(size_t) q];
    }

    StateSpace<NS, NP> ss;
    Matrix<NS, NS> aT {};      // A transposed (column-major copy for the audio loop)
    Matrix<NP, NS> cT {};      // C transposed
    Matrix<NS, NP> daT {};     // (D A) transposed
    Matrix<NP, NP> dcT {};     // (D C) transposed
    Vec dbIn {}, pConst {};
    Vector<NS> oA {};
    Vec oC {};
    double obIn = 0.0, oConstNoY0 = 0.0;
    Vec pState {};             // D x + eDc for the current sample
    double yState = 0.0;       // dOut . x + eOutDc - y0 for the current sample
    Mat lu {};                 // cached LU (explicit inverse when NP <= 6) of the Newton Jacobian
    Vec invPivot {};
    Vec vMinusFi {};           // vEval - F iEval
    Vector<NS> x {}, x0 {};
    Vec v0 {}, i0 {}, vEval {}, iEval {};
    Mat j0 {}, jEval {};
    double y0 = 0.0;
    int iterationsUsed = 0;
    bool structureExact = true;

    // bias supply
    bool hasBias = false, biasGridReady = false;
    double biasVolts = 0.0, biasRange = 0.0;
    DcTerms nominalDc {};
    Vec pConstBias {};
    double oConstBias = 0.0;
    double y0Nominal = 0.0;
    std::array<RestPoint, (size_t) biasGridSize> biasGrid {};
    RestPoint restAtBias {};
};

} // namespace umber::dsp::tubes
