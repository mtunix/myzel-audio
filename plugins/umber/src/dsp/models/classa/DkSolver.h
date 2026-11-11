#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace umber::dsp::classa
{

/**
    Nodal DK-method solver (Yeh/Abel/Smith 2010, Holters/Zölzer 2015) for small nonlinear
    circuits with one audio input, one constant supply and one adjustable DC bias current.

    A netlist is stamped into MNA form (resistors, trapezoidal companion models of capacitors
    and inductors, Norton sources). Once per sample rate the linear part is reduced to a
    discrete state-space system in which the only per-sample unknowns are the controlling
    variables v of the nonlinear ports:

        p  = Px·x + pu·vin + p0
        v  = p + K·i,   i = f(v)            (Newton–Raphson, |ports| × |ports|)
        x' = Ax·x + au·vin + Ai·i + x0
        y  = cx·x + cu·vin + ci·i + y0

    Port kinds:
      - junction: Ebers–Moll diode i = Is·(exp(v/Vt) − 1), SPICE-style pnjlim step limiting,
        exp() continued linearly past a ceiling so it can never overflow;
      - core: excess magnetising current of an inductor, controlled by its flux. Exactly zero
        below a knee, C1-continuous above it.

    Newton details (real-time safe, bounded):
      - warm start from the previous sample's solution, re-using its currents and slopes, so the
        first step costs no exp();
      - only ports with non-zero slope enter the linear solve (zero-slope ports decouple exactly),
        which is usually a 2×2 system solved in closed form;
      - convergence when the step on every active port is below `tolerance` (in Vt for junctions,
        in phiSat for cores); the currents then get a first-order update to the final iterate, so
        the residual error is ~tolerance²;
      - at most maxIterations iterations.

    For event handling (see ClassACircuit): save / restore a sample's state, transferFrom() a
    solver of the same netlist at another rate (exact for every reactive element), and
    conductingMask(), whose change between samples marks a junction switching.
*/
template <size_t NumNodes, size_t NumStates, size_t NumPorts>
class DkSolver
{
public:
    static constexpr size_t numNodes  = NumNodes;
    static constexpr size_t numStates = NumStates;
    static constexpr size_t numPorts  = NumPorts;
    static constexpr int maxIterations = 16;

    using StateVector = std::array<double, NumStates>;
    using PortVector  = std::array<double, NumPorts>;
    using NodeVector  = std::array<double, NumNodes>;

    template <size_t R, size_t C>
    using Mat = std::array<std::array<double, C>, R>;

    static constexpr int ground = -1;

    /** A rest (operating) point of the discrete system: states and port solution. */
    struct RestPoint { StateVector x {}; PortVector v {}, i {}, slope {}; };

    /** Everything process() carries from one sample to the next (see save / restore). */
    struct Snapshot { StateVector x {}, xLast {}; PortVector v {}, i {}, slope {}; double vin = 0.0; };

    //==============================================================================
    /** MNA stamps. Node indices are 0-based; ground is -1. Input 0 is vin, input 1 the supply,
        input 2 an adjustable DC bias current (setBiasCurrent). */
    struct Netlist
    {
        Mat<NumNodes, NumNodes> g {};
        Mat<NumNodes, 3> bu {};
        Mat<NumNodes, NumStates> bx {};
        Mat<NumNodes, NumPorts> bi {};
        Mat<NumPorts, NumNodes> nv {};
        Mat<NumPorts, NumStates> dv {};
        Mat<NumStates, NumNodes> ne {};
        StateVector ge {}, stateSign {};
        NodeVector output {};
        double sampleRate = 48000.0;
        bool dc = false; // capacitors open, inductors shorted

        void conductance (int a, int b, double gab)
        {
            if (a >= 0) g[(size_t) a][(size_t) a] += gab;
            if (b >= 0) g[(size_t) b][(size_t) b] += gab;
            if (a >= 0 && b >= 0)
            {
                g[(size_t) a][(size_t) b] -= gab;
                g[(size_t) b][(size_t) a] -= gab;
            }
        }

        void resistor (int a, int b, double r) { conductance (a, b, 1.0 / r); }

        /** Resistor from node a to the supply rail. */
        void resistorToSupply (int a, double r)
        {
            g[(size_t) a][(size_t) a] += 1.0 / r;
            bu[(size_t) a][1] += 1.0 / r;
        }

        /** The adjustable bias current (amps, see setBiasCurrent) flows into node a. */
        void biasCurrentInto (int a)
        {
            bu[(size_t) a][2] += 1.0;
        }

        /** The audio EMF in series with r, from node b (−) to node a (+). */
        void source (int a, int b, double r)
        {
            conductance (a, b, 1.0 / r);
            if (a >= 0) bu[(size_t) a][0] += 1.0 / r;
            if (b >= 0) bu[(size_t) b][0] -= 1.0 / r;
        }

        /** Trapezoidal capacitor: i_ab = gc·v_ab − J,  J' = 2·gc·v_ab − J. */
        void capacitor (int a, int b, double c, int s)
        {
            if (dc)
                return;

            const auto gc = 2.0 * c * sampleRate;
            conductance (a, b, gc);
            select (a, b, s);
            if (a >= 0) bx[(size_t) a][(size_t) s] += 1.0;
            if (b >= 0) bx[(size_t) b][(size_t) s] -= 1.0;
            ge[(size_t) s] = gc;
            stateSign[(size_t) s] = -1.0;
        }

        /**
            Trapezoidal inductor: i_ab = gl·v_ab + J,  J' = 2·gl·v_ab + J. Its flux L·i_ab is the
            controlling variable of fluxPort (pass -1 for a linear inductor). In DC mode it is a
            short whose current still defines the flux.
        */
        void inductor (int a, int b, double l, int s, int fluxPort)
        {
            const auto gl = dc ? dcShort : 0.5 / (l * sampleRate);
            conductance (a, b, gl);

            if (fluxPort >= 0)
            {
                if (a >= 0) nv[(size_t) fluxPort][(size_t) a] += l * gl;
                if (b >= 0) nv[(size_t) fluxPort][(size_t) b] -= l * gl;
                if (! dc)
                    dv[(size_t) fluxPort][(size_t) s] = l;
            }

            if (dc)
                return;

            select (a, b, s);
            if (a >= 0) bx[(size_t) a][(size_t) s] -= 1.0;
            if (b >= 0) bx[(size_t) b][(size_t) s] += 1.0;
            ge[(size_t) s] = gl;
            stateSign[(size_t) s] = 1.0;
        }

        /** Port controlling voltage v = V(a) − V(b). */
        void controlVoltage (int port, int a, int b)
        {
            if (a >= 0) nv[(size_t) port][(size_t) a] += 1.0;
            if (b >= 0) nv[(size_t) port][(size_t) b] -= 1.0;
        }

        /** The port current enters node `node` scaled by gain. */
        void inject (int port, int node, double gain)
        {
            if (node >= 0)
                bi[(size_t) node][(size_t) port] += gain;
        }

        /**
            Ebers–Moll NPN in transport form: forward current I_F (port pf, controlled by v_BE) and,
            if pr >= 0, reverse current I_R (port pr, controlled by v_BC). A collector tied to the
            supply is passed as c = -2 (its current then flows into the ideal rail).
        */
        void npn (int b, int c, int e, double betaF, double betaR, int pf, int pr)
        {
            const auto alphaF = betaF / (1.0 + betaF);
            controlVoltage (pf, b, e);
            inject (pf, b, -1.0 / betaF);
            inject (pf, c, -1.0);
            inject (pf, e, 1.0 / alphaF);

            if (pr >= 0)
            {
                const auto alphaR = betaR / (1.0 + betaR);
                controlVoltage (pr, b, c);
                inject (pr, b, -1.0 / betaR);
                inject (pr, c, 1.0 / alphaR);
                inject (pr, e, -1.0);
            }
        }

        void select (int a, int b, int s)
        {
            if (a >= 0) ne[(size_t) s][(size_t) a] += 1.0;
            if (b >= 0) ne[(size_t) s][(size_t) b] -= 1.0;
        }

        static constexpr double dcShort = 1.0e4;
    };

    struct PortModel
    {
        enum Kind { junction, core } kind = junction;
        double is = 1.0e-14;                           // junction saturation current
        double invL = 0.0, invPhiSat = 0.0, knee2 = 0.0; // core
    };

    //==============================================================================
    /**
        Builds the discrete system. stamp (Netlist&) is called twice: once for DC (to find the
        operating point) and once for the transient system. Not real-time safe.
    */
    template <typename StampFn>
    void prepare (double newSampleRate, double supply, double thermalVoltage,
                  const std::array<PortModel, NumPorts>& portModels, StampFn&& stamp)
    {
        sampleRate = newSampleRate;
        vt = thermalVoltage;
        invVt = 1.0 / vt;
        ports = portModels;

        for (size_t q = 0; q < NumPorts; ++q)
        {
            gmax[q] = ports[q].is * invVt * std::exp (maxExponent);
            vcrit[q] = vt * std::log (vt / (std::numbers::sqrt2 * ports[q].is));
            tolScale[q] = ports[q].kind == PortModel::junction ? invVt : ports[q].invPhiSat;
        }

        solveDc (supply, stamp);

        Netlist net;
        net.sampleRate = sampleRate;
        stamp (net);
        addGmin (net);

        auto sInv = net.g;
        invert<NumNodes> (sInv);

        const auto sbx = multiply<NumNodes, NumNodes, NumStates> (sInv, net.bx);
        const auto sbu = multiply<NumNodes, NumNodes, 3> (sInv, net.bu);
        const auto sbi = multiply<NumNodes, NumNodes, NumPorts> (sInv, net.bi);

        const auto pxm = multiply<NumPorts, NumNodes, NumStates> (net.nv, sbx);
        const auto pum = multiply<NumPorts, NumNodes, 3> (net.nv, sbu);
        const auto km  = multiply<NumPorts, NumNodes, NumPorts> (net.nv, sbi);
        const auto exm = multiply<NumStates, NumNodes, NumStates> (net.ne, sbx);
        const auto eum = multiply<NumStates, NumNodes, 3> (net.ne, sbu);
        const auto eim = multiply<NumStates, NumNodes, NumPorts> (net.ne, sbi);

        for (size_t p = 0; p < NumPorts; ++p)
        {
            for (size_t s = 0; s < NumStates; ++s)
                px[p][s] = pxm[p][s] + net.dv[p][s];
            k[p] = km[p];
            kSelf[p] = std::abs (km[p][p]);
            pu[p] = pum[p][0];
            p0[p] = pum[p][1] * supply;
            pBias[p] = pum[p][2];
        }

        for (size_t s = 0; s < NumStates; ++s)
        {
            // branch voltage of every reactive element, for transferFrom()
            brX[s] = exm[s];
            brI[s] = eim[s];
            brU[s] = eum[s][0];
            brConst[s] = eum[s][1] * supply;
            brBias[s] = eum[s][2];
            ge[s] = net.ge[s];
            stateSign[s] = net.stateSign[s];

            const auto twoGe = 2.0 * net.ge[s];
            for (size_t t = 0; t < NumStates; ++t)
                ax[s][t] = twoGe * exm[s][t] + (s == t ? net.stateSign[s] : 0.0);
            for (size_t q = 0; q < NumPorts; ++q)
                ai[s][q] = twoGe * eim[s][q];
            au[s] = twoGe * eum[s][0];
            x0[s] = twoGe * eum[s][1] * supply;
            xBias[s] = twoGe * eum[s][2];
        }

        for (size_t nd = 0; nd < NumNodes; ++nd)
        {
            nodeX[nd] = sbx[nd];
            nodeI[nd] = sbi[nd];
            nodeU[nd] = sbu[nd][0];
            nodeConst[nd] = sbu[nd][1] * supply;
            nodeBias[nd] = sbu[nd][2];
        }

        cx = {}; ci = {}; cu = 0.0; y0 = 0.0; yBias = 0.0;
        for (size_t nd = 0; nd < NumNodes; ++nd)
        {
            const auto w = net.output[nd];
            for (size_t s = 0; s < NumStates; ++s) cx[s] += w * nodeX[nd][s];
            for (size_t q = 0; q < NumPorts; ++q)  ci[q] += w * nodeI[nd][q];
            cu += w * nodeU[nd];
            y0 += w * nodeConst[nd];
            yBias += w * nodeBias[nd];
        }

        // states from the DC solution: capacitor J = gc·v (no current), inductor J = i (no voltage)
        for (size_t s = 0; s < NumStates; ++s)
        {
            double v = 0.0;
            for (size_t nd = 0; nd < NumNodes; ++nd)
                v += net.ne[s][nd] * dcNodes[nd];
            xRest[s] = net.stateSign[s] < 0.0 ? net.ge[s] * v : dcInductorCurrent[s];
        }

        // Settle round-off differences between the DC netlist and the discrete system, so that
        // reset() starts exactly at the discrete fixed point.
        x = xRest;
        vPrev = vRest;
        evaluate (vPrev, iPrev, slopePrev);
        for (int i = 0; i < 4096; ++i)
            process (0.0);
        xRest = x;
        vRest = vPrev;
        iRest = iPrev;
        slopeRest = slopePrev;

        double yIdle = y0;
        for (size_t s = 0; s < NumStates; ++s) yIdle += cx[s] * xRest[s];
        for (size_t q = 0; q < NumPorts; ++q)  yIdle += ci[q] * iRest[q];
        y0 -= yIdle;

        p0Nominal = p0;
        x0Nominal = x0;
        y0Nominal = y0;
        biasAmps = 0.0;

        prepareBiasGrid (supply, stamp, net);
        reset();
    }

    /**
        Sets the adjustable DC bias current (amps). Cheap and real-time safe. 0 restores the
        nominal circuit exactly.

        The operating point moves quasi-statically: the reactive states are shifted by the
        change of their rest values (interpolated from the pre-solved grid), so the circuit is
        at its new operating point at once, with whatever signal it carries superimposed. Left
        to themselves the coupling capacitors would recharge through the audio path and thump,
        as a trim pot turned on a running amplifier does.
    */
    void setBiasCurrent (double amps) noexcept
    {
        if (amps == biasAmps)
            return;

        biasAmps = amps;

        for (size_t a = 0; a < NumPorts; ++a)
            p0[a] = p0Nominal[a] + pBias[a] * amps;
        for (size_t s = 0; s < NumStates; ++s)
            x0[s] = x0Nominal[s] + xBias[s] * amps;
        y0 = y0Nominal + yBias * amps;

        if (! biasGridReady)
            return;

        RestPoint next;
        interpolateRest (amps, next);

        for (size_t s = 0; s < NumStates; ++s)
        {
            x[s] += next.x[s] - restAtBias.x[s];
            xLast[s] += next.x[s] - restAtBias.x[s];
        }
        for (size_t q = 0; q < NumPorts; ++q)
        {
            vPrev[q] += next.v[q] - restAtBias.v[q];
            iPrev[q] += next.i[q] - restAtBias.i[q];
            slopePrev[q] += next.slope[q] - restAtBias.slope[q];
        }

        restAtBias = next;
    }

    double getBiasCurrent() const noexcept { return biasAmps; }

    /** Range of bias currents whose operating points reset() can start from directly. */
    void setBiasRange (double maxAmps) noexcept { biasRange = maxAmps; }

    void reset() noexcept
    {
        x = xRest;
        vPrev = vRest;
        iPrev = iRest;
        slopePrev = slopeRest;

        if (biasGridReady)
        {
            // Start at the biased operating point (interpolated between pre-solved ones), so a
            // reset with Bias set does not glide from the nominal point. Exact at 0.
            interpolateRest (biasAmps, restAtBias);

            if (biasAmps != 0.0)
            {
                x = restAtBias.x;
                vPrev = restAtBias.v;
                iPrev = restAtBias.i;
                slopePrev = restAtBias.slope;
            }
        }

        xLast = x;
        lastVin = 0.0;
        iterationsUsed = 0;
    }

    //==============================================================================
    double process (double vin) noexcept
    {
        PortVector p;
        for (size_t a = 0; a < NumPorts; ++a)
        {
            auto s = p0[a] + pu[a] * vin;
            for (size_t t = 0; t < NumStates; ++t)
                s += px[a][t] * x[t];
            p[a] = s;
        }

        PortVector v = vPrev, cur = iPrev, slope = slopePrev;
        bool converged = false;
        int it = 0;

        while (it < maxIterations)
        {
            if (it > 0)
                evaluate (v, cur, slope);
            ++it;

            const auto step = newtonStep (p, v, cur, slope);

            // Junctions get pnjlim; a zero-slope port only follows linearly, unless this step
            // moved it into its nonlinear region, which forces another iteration.
            double maxStep = 0.0;
            bool woke = false;
            PortVector applied;
            for (size_t a = 0; a < NumPorts; ++a)
            {
                auto vNew = v[a] + step[a];
                if (ports[a].kind == PortModel::junction)
                    vNew = limitJunction (vNew, v[a], vcrit[a]);

                applied[a] = vNew - v[a];
                v[a] = vNew;

                if (slope[a] > 0.0)
                    maxStep = std::max (maxStep, std::abs (applied[a]) * tolScale[a]);
                else
                    woke = woke || isNonlinear (a, vNew);
            }

            if (maxStep < tolerance && ! woke)
            {
                for (size_t a = 0; a < NumPorts; ++a)
                    cur[a] += slope[a] * applied[a];

                for (size_t a = 0; a < NumPorts; ++a)
                {
                    if (slope[a] > 0.0)
                        continue;
                    auto acc = p[a];
                    for (size_t b = 0; b < NumPorts; ++b)
                        acc += k[a][b] * cur[b];
                    v[a] = acc;
                }

                converged = true;
                break;
            }
        }

        if (! converged)
            evaluate (v, cur, slope);

        iterationsUsed = it;

        double y = y0 + cu * vin;
        for (size_t t = 0; t < NumStates; ++t)
            y += cx[t] * x[t];
        for (size_t b = 0; b < NumPorts; ++b)
            y += ci[b] * cur[b];

        StateVector xNew;
        for (size_t s = 0; s < NumStates; ++s)
        {
            auto acc = x0[s] + au[s] * vin;
            for (size_t t = 0; t < NumStates; ++t)
                acc += ax[s][t] * x[t];
            for (size_t b = 0; b < NumPorts; ++b)
                acc += ai[s][b] * cur[b];

            // states that rest at zero (inductor currents) must never decay into denormals
            xNew[s] = std::abs (acc) < 1.0e-20 ? 0.0 : acc;
        }

        for (size_t a = 0; a < NumPorts; ++a)
            if (std::abs (v[a]) < 1.0e-20)
                v[a] = 0.0;

        xLast = x;
        x = xNew;
        vPrev = v;
        iPrev = cur;
        slopePrev = slope;
        lastVin = vin;
        return y;
    }

    //==============================================================================
    void save (Snapshot& s) const noexcept
    {
        s.x = x;
        s.xLast = xLast;
        s.v = vPrev;
        s.i = iPrev;
        s.slope = slopePrev;
        s.vin = lastVin;
    }

    void restore (const Snapshot& s) noexcept
    {
        x = s.x;
        xLast = s.xLast;
        vPrev = s.v;
        iPrev = s.i;
        slopePrev = s.slope;
        lastVin = s.vin;
    }

    /**
        Takes over the state of `other`, a DkSolver for the same netlist discretised at another
        sample rate, at the sample it processed last. Node voltages, the port solution and the
        voltage and current of every capacitor and inductor carry over exactly: with the
        trapezoidal companion the state after a sample is J = g·v + i (both element kinds) and the
        one used during it is J = g·v − i (capacitor) or i − g·v (inductor), so only the g·v part
        changes with the rate. Real-time safe; both solvers must have the same bias current.
    */
    void transferFrom (const DkSolver& other) noexcept
    {
        for (size_t s = 0; s < NumStates; ++s)
        {
            auto vb = other.brConst[s] + other.brBias[s] * other.biasAmps + other.brU[s] * other.lastVin;
            for (size_t t = 0; t < NumStates; ++t)
                vb += other.brX[s][t] * other.xLast[t];
            for (size_t q = 0; q < NumPorts; ++q)
                vb += other.brI[s][q] * other.iPrev[q];

            const auto d = (ge[s] - other.ge[s]) * vb;
            x[s] = other.x[s] + d;
            xLast[s] = other.xLast[s] - stateSign[s] * d;
        }

        vPrev = other.vPrev;
        iPrev = other.iPrev;
        slopePrev = other.slopePrev;
        lastVin = other.lastVin;
    }

    /**
        Bit a is set while junction port a conducts in the sense that matters for switching: its
        incremental conductance exceeds a multiple of the conductance the rest of the circuit
        presents to it (`scale`, from switchScale(); pass the same one to solvers of the same
        circuit at different rates, as the companion conductances make their own differ).
        Between the two lies the junction's knee, so a change of the mask from one
        sample to the next marks a switching event (a transistor cutting off, turning on or
        saturating) inside that sample period. A conducting port (bit set in `previous`) counts
        as conducting down to offRatio, a blocking one only from onRatio up (hysteresis).
    */
    unsigned conductingMask (unsigned previous, double onRatio, double offRatio, const PortVector& scale) const noexcept
    {
        unsigned m = 0;
        for (size_t a = 0; a < NumPorts; ++a)
        {
            if (ports[a].kind != PortModel::junction)
                continue;
            const auto r = slopePrev[a] * scale[a];
            if (r > ((previous >> a) & 1u ? offRatio : onRatio))
                m |= 1u << a;
        }
        return m;
    }

    /** Resistance (V/A) the circuit presents to each port at this rate. */
    const PortVector& switchScale() const noexcept { return kSelf; }

    /** Output at the current operating point; valid right after reset(). */
    double restOutput() const noexcept
    {
        double y = y0;
        for (size_t s = 0; s < NumStates; ++s) y += cx[s] * x[s];
        for (size_t q = 0; q < NumPorts; ++q)  y += ci[q] * iPrev[q];
        return y;
    }

    //==============================================================================
    /** Small-signal response (vin -> y) of the discrete system linearised at rest. */
    std::complex<double> smallSignalResponse (double freqHz) const noexcept
    {
        using cd = std::complex<double>;

        PortVector cur {}, d {};
        evaluate (vRest, cur, d);

        Mat<NumPorts, NumPorts> m {};
        for (size_t a = 0; a < NumPorts; ++a)
            for (size_t b = 0; b < NumPorts; ++b)
                m[a][b] = (a == b ? 1.0 : 0.0) - k[a][b] * d[b];
        invert<NumPorts> (m);

        Mat<NumPorts, NumStates> lx {};
        PortVector lu {};
        for (size_t a = 0; a < NumPorts; ++a)
            for (size_t b = 0; b < NumPorts; ++b)
            {
                for (size_t s = 0; s < NumStates; ++s)
                    lx[a][s] += d[a] * m[a][b] * px[b][s];
                lu[a] += d[a] * m[a][b] * pu[b];
            }

        Mat<NumStates, NumStates> aLin {};
        StateVector bLin {}, cLin {};
        double dLin = cu;
        for (size_t s = 0; s < NumStates; ++s)
        {
            bLin[s] = au[s];
            cLin[s] = cx[s];
            for (size_t t = 0; t < NumStates; ++t)
            {
                aLin[s][t] = ax[s][t];
                for (size_t q = 0; q < NumPorts; ++q)
                    aLin[s][t] += ai[s][q] * lx[q][t];
            }
            for (size_t q = 0; q < NumPorts; ++q)
            {
                bLin[s] += ai[s][q] * lu[q];
                cLin[s] += ci[q] * lx[q][s];
            }
        }
        for (size_t q = 0; q < NumPorts; ++q)
            dLin += ci[q] * lu[q];

        const auto z = std::polar (1.0, 2.0 * std::numbers::pi * freqHz / sampleRate);
        std::array<std::array<cd, NumStates>, NumStates> sys {};
        std::array<cd, NumStates> rhs {};
        for (size_t s = 0; s < NumStates; ++s)
        {
            for (size_t t = 0; t < NumStates; ++t)
                sys[s][t] = (s == t ? z : cd (0.0)) - aLin[s][t];
            rhs[s] = bLin[s];
        }

        for (size_t col = 0; col < NumStates; ++col)
        {
            size_t piv = col;
            for (size_t r = col + 1; r < NumStates; ++r)
                if (std::abs (sys[r][col]) > std::abs (sys[piv][col]))
                    piv = r;
            std::swap (sys[col], sys[piv]);
            std::swap (rhs[col], rhs[piv]);

            for (size_t r = col + 1; r < NumStates; ++r)
            {
                const auto f = sys[r][col] / sys[col][col];
                for (size_t c = col; c < NumStates; ++c)
                    sys[r][c] -= f * sys[col][c];
                rhs[r] -= f * rhs[col];
            }
        }
        for (size_t r = NumStates; r-- > 0;)
        {
            auto acc = rhs[r];
            for (size_t c = r + 1; c < NumStates; ++c)
                acc -= sys[r][c] * rhs[c];
            rhs[r] = acc / sys[r][r];
        }

        cd h = dLin;
        for (size_t s = 0; s < NumStates; ++s)
            h += cLin[s] * rhs[s];
        return h;
    }

    //==============================================================================
    const NodeVector& restNodeVoltages() const noexcept { return dcNodes; }
    bool dcConverged() const noexcept { return dcOk; }
    int lastIterations() const noexcept { return iterationsUsed; }

    /** Node voltages of the last processed sample (validation only). */
    NodeVector lastNodeVoltages() const noexcept
    {
        NodeVector out {};
        for (size_t nd = 0; nd < NumNodes; ++nd)
        {
            auto s = nodeConst[nd] + nodeBias[nd] * biasAmps + nodeU[nd] * lastVin;
            for (size_t t = 0; t < NumStates; ++t)
                s += nodeX[nd][t] * xLast[t];
            for (size_t b = 0; b < NumPorts; ++b)
                s += nodeI[nd][b] * iPrev[b];
            out[nd] = s;
        }
        return out;
    }

private:
    static constexpr double maxExponent = 40.0;  // exp() is continued linearly beyond this
    static constexpr double minExponent = -40.0; // below this a junction is fully off
    static constexpr double tolerance   = 1.0e-4;

    //==============================================================================
    void evaluate (const PortVector& v, PortVector& current, PortVector& slope) const noexcept
    {
        for (size_t a = 0; a < NumPorts; ++a)
        {
            const auto& pm = ports[a];

            if (pm.kind == PortModel::junction)
            {
                const auto xj = v[a] * invVt;

                if (xj > maxExponent)
                {
                    current[a] = gmax[a] * vt * (1.0 + xj - maxExponent) - pm.is;
                    slope[a]   = gmax[a];
                }
                else if (xj < minExponent)
                {
                    current[a] = -pm.is;
                    slope[a]   = 0.0;
                }
                else
                {
                    const auto e = std::exp (xj);
                    current[a] = pm.is * (e - 1.0);
                    slope[a]   = pm.is * invVt * e;
                }
            }
            else
            {
                // i = (phi / L) * u^2,  u = max (0, r^2 - knee^2),  r = phi / phiSat
                const auto r = v[a] * pm.invPhiSat;
                const auto u = r * r - pm.knee2;
                if (u > 0.0)
                {
                    current[a] = v[a] * pm.invL * u * u;
                    slope[a]   = pm.invL * u * (u + 4.0 * r * r);
                }
                else
                {
                    current[a] = 0.0;
                    slope[a]   = 0.0;
                }
            }
        }
    }

    bool isNonlinear (size_t a, double v) const noexcept
    {
        if (ports[a].kind == PortModel::junction)
            return v * invVt >= minExponent;

        const auto r = v * ports[a].invPhiSat;
        return r * r > ports[a].knee2;
    }

    double limitJunction (double vNew, double vOld, double vc) const noexcept
    {
        // SPICE pnjlim: move along the exponential in the current domain, never jump over it
        if (vNew > vc && std::abs (vNew - vOld) > 2.0 * vt)
        {
            if (vOld > 0.0)
            {
                const auto arg = 1.0 + (vNew - vOld) * invVt;
                vNew = arg > 0.0 ? vOld + vt * std::log (arg) : vc;
            }
            else
            {
                vNew = vt * std::log (vNew * invVt);
            }
        }
        return vNew;
    }

    /**
        Newton step for r = p + K·i − v, J = K·diag(slope) − I. Zero-slope columns are −e_b,
        so only the active block is solved; inactive rows follow as d_a = r_a + Σ K_ab·slope_b·d_b.
    */
    PortVector newtonStep (const PortVector& p, const PortVector& v, const PortVector& cur,
                           const PortVector& slope) const noexcept
    {
        PortVector r;
        for (size_t a = 0; a < NumPorts; ++a)
        {
            auto acc = p[a] - v[a];
            for (size_t b = 0; b < NumPorts; ++b)
                acc += k[a][b] * cur[b];
            r[a] = acc;
        }

        std::array<size_t, NumPorts> active {};
        size_t n = 0;
        for (size_t b = 0; b < NumPorts; ++b)
            if (slope[b] > 0.0)
                active[n++] = b;

        PortVector step {};

        if (n == 1)
        {
            const auto i0 = active[0];
            const auto j00 = k[i0][i0] * slope[i0] - 1.0;
            step[i0] = -r[i0] / j00;
        }
        else if (n == 2)
        {
            const auto i0 = active[0], i1 = active[1];
            const auto j00 = k[i0][i0] * slope[i0] - 1.0, j01 = k[i0][i1] * slope[i1];
            const auto j10 = k[i1][i0] * slope[i0],       j11 = k[i1][i1] * slope[i1] - 1.0;
            const auto det = j00 * j11 - j01 * j10;
            const auto invDet = std::abs (det) > 1.0e-300 ? 1.0 / det : 0.0;
            step[i0] = (-r[i0] * j11 + r[i1] * j01) * invDet;
            step[i1] = (-r[i1] * j00 + r[i0] * j10) * invDet;
        }
        else if (n > 2)
        {
            Mat<NumPorts, NumPorts> jac {};
            PortVector rhs {};
            for (size_t a = 0; a < n; ++a)
            {
                for (size_t b = 0; b < n; ++b)
                    jac[a][b] = k[active[a]][active[b]] * slope[active[b]];
                jac[a][a] -= 1.0;
                rhs[a] = -r[active[a]];
            }
            solveLeading (jac, rhs, n);
            for (size_t a = 0; a < n; ++a)
                step[active[a]] = rhs[a];
        }

        for (size_t a = 0; a < NumPorts; ++a)
        {
            if (slope[a] > 0.0)
                continue;
            auto acc = r[a];
            for (size_t c = 0; c < n; ++c)
                acc += k[a][active[c]] * slope[active[c]] * step[active[c]];
            step[a] = acc;
        }

        return step;
    }

    /** Partial-pivoting elimination on the leading n×n block. Real-time safe. */
    static void solveLeading (Mat<NumPorts, NumPorts>& j, PortVector& rhs, size_t n) noexcept
    {
        for (size_t col = 0; col < n; ++col)
        {
            size_t piv = col;
            for (size_t r = col + 1; r < n; ++r)
                if (std::abs (j[r][col]) > std::abs (j[piv][col]))
                    piv = r;

            if (piv != col)
            {
                std::swap (j[col], j[piv]);
                std::swap (rhs[col], rhs[piv]);
            }

            const auto d = j[col][col];
            const auto invD = std::abs (d) > 1.0e-300 ? 1.0 / d : 0.0;
            j[col][col] = invD;

            for (size_t r = col + 1; r < n; ++r)
            {
                const auto f = j[r][col] * invD;
                for (size_t c = col + 1; c < n; ++c)
                    j[r][c] -= f * j[col][c];
                rhs[r] -= f * rhs[col];
            }
        }

        for (size_t r = n; r-- > 0;)
        {
            auto s = rhs[r];
            for (size_t c = r + 1; c < n; ++c)
                s -= j[r][c] * rhs[c];
            rhs[r] = s * j[r][r];
        }
    }

    //==============================================================================
    template <typename StampFn>
    void solveDc (double supply, StampFn& stamp, double bias = 0.0)
    {
        Netlist net;
        net.sampleRate = sampleRate;
        net.dc = true;
        stamp (net);
        addGmin (net);

        auto sInv = net.g;
        invert<NumNodes> (sInv);
        const auto sbu = multiply<NumNodes, NumNodes, 3> (sInv, net.bu);
        const auto sbi = multiply<NumNodes, NumNodes, NumPorts> (sInv, net.bi);
        const auto pum = multiply<NumPorts, NumNodes, 3> (net.nv, sbu);
        const auto km  = multiply<NumPorts, NumNodes, NumPorts> (net.nv, sbi);

        PortVector v {}, p {};
        for (size_t a = 0; a < NumPorts; ++a)
        {
            p[a] = pum[a][1] * supply + pum[a][2] * bias;
            v[a] = ports[a].kind == PortModel::junction ? 0.5 : p[a];
        }

        PortVector cur {}, slope {};
        dcOk = false;
        for (int it = 0; it < 1000 && ! dcOk; ++it)
        {
            evaluate (v, cur, slope);

            Mat<NumPorts, NumPorts> jac {};
            PortVector rhs {};
            for (size_t a = 0; a < NumPorts; ++a)
            {
                double r = p[a] - v[a];
                for (size_t b = 0; b < NumPorts; ++b)
                {
                    r += km[a][b] * cur[b];
                    jac[a][b] = km[a][b] * slope[b] - (a == b ? 1.0 : 0.0);
                }
                rhs[a] = -r;
            }
            solveLeading (jac, rhs, NumPorts);

            double step = 0.0;
            for (size_t a = 0; a < NumPorts; ++a)
            {
                auto vNew = v[a] + rhs[a];
                if (ports[a].kind == PortModel::junction)
                    vNew = limitJunction (vNew, v[a], vcrit[a]);
                step = std::max (step, std::abs (vNew - v[a]) * tolScale[a]);
                v[a] = vNew;
            }
            dcOk = step < 1.0e-10;
        }

        evaluate (v, cur, slope);
        vRest = v;

        for (size_t nd = 0; nd < NumNodes; ++nd)
        {
            dcNodes[nd] = sbu[nd][1] * supply + sbu[nd][2] * bias;
            for (size_t q = 0; q < NumPorts; ++q)
                dcNodes[nd] += sbi[nd][q] * cur[q];
        }

        // inductor DC currents: the short's conductance times its (tiny) voltage
        dcInductorCurrent = {};
        Netlist probe;
        probe.sampleRate = sampleRate;
        stamp (probe);
        for (size_t s = 0; s < NumStates; ++s)
        {
            if (probe.stateSign[s] <= 0.0)
                continue;
            double v2 = 0.0;
            for (size_t nd = 0; nd < NumNodes; ++nd)
                v2 += probe.ne[s][nd] * dcNodes[nd];
            dcInductorCurrent[s] = Netlist::dcShort * v2;
        }
    }

    /**
        Operating points across the bias range (DC solve plus a short settle of the discrete
        system each), for reset(). The nominal point and every nominal member are restored
        afterwards, bit for bit.
    */
    template <typename StampFn>
    void prepareBiasGrid (double supply, StampFn& stamp, const Netlist& net)
    {
        biasGridReady = false;

        if (biasRange <= 0.0)
            return;

        const auto savedDcNodes = dcNodes;
        const auto savedInductor = dcInductorCurrent;
        const auto savedVRest = vRest;
        const auto savedOk = dcOk;

        for (size_t g = 0; g < biasGridSize; ++g)
        {
            const auto amps = gridRange() * (2.0 * (double) g / (double) (biasGridSize - 1) - 1.0);
            auto& point = biasGrid[g];

            if (g == biasGridSize / 2)
            {
                point = { xRest, savedVRest, iRest, slopeRest };   // solveDc overwrites vRest
                continue;
            }

            solveDc (supply, stamp, amps);

            StateVector xs {};
            for (size_t s = 0; s < NumStates; ++s)
            {
                double v = 0.0;
                for (size_t nd = 0; nd < NumNodes; ++nd)
                    v += net.ne[s][nd] * dcNodes[nd];
                xs[s] = net.stateSign[s] < 0.0 ? net.ge[s] * v : dcInductorCurrent[s];
            }

            setBiasCurrent (amps);
            x = xs;
            vPrev = vRest;
            evaluate (vPrev, iPrev, slopePrev);
            for (int i = 0; i < 4096; ++i)
                process (0.0);

            point = { x, vPrev, iPrev, slopePrev };
        }

        setBiasCurrent (0.0);
        dcNodes = savedDcNodes;
        dcInductorCurrent = savedInductor;
        vRest = savedVRest;
        dcOk = savedOk;
        biasGridReady = true;
    }

    /** Rest point at any bias: Catmull-Rom (C1) through the pre-solved grid. Exact on grid points. */
    void interpolateRest (double amps, RestPoint& out) const noexcept
    {
        constexpr auto last = biasGridSize - 1;
        const auto u = std::clamp ((amps / gridRange() + 1.0) * 0.5 * (double) last, 0.0, (double) last);
        const auto i1 = std::min ((size_t) u, last - 1);
        const auto t = u - (double) i1;
        const auto i0 = i1 > 0 ? i1 - 1 : 0;
        const auto i2 = i1 + 1;
        const auto i3 = std::min (i2 + 1, last);

        // Catmull-Rom weights
        const auto t2 = t * t, t3 = t2 * t;
        const auto w0 = -0.5 * t3 + t2 - 0.5 * t;
        const auto w1 = 1.5 * t3 - 2.5 * t2 + 1.0;
        const auto w2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
        const auto w3 = 0.5 * t3 - 0.5 * t2;

        const auto& a = biasGrid[i0];
        const auto& b = biasGrid[i1];
        const auto& c = biasGrid[i2];
        const auto& d = biasGrid[i3];

        if (t == 0.0)
        {
            out = b;
            return;
        }

        for (size_t s = 0; s < NumStates; ++s)
            out.x[s] = w0 * a.x[s] + w1 * b.x[s] + w2 * c.x[s] + w3 * d.x[s];
        for (size_t q = 0; q < NumPorts; ++q)
        {
            out.v[q] = w0 * a.v[q] + w1 * b.v[q] + w2 * c.v[q] + w3 * d.v[q];
            out.i[q] = w0 * a.i[q] + w1 * b.i[q] + w2 * c.i[q] + w3 * d.i[q];
            out.slope[q] = std::max (0.0, w0 * a.slope[q] + w1 * b.slope[q] + w2 * c.slope[q] + w3 * d.slope[q]);
        }
    }

    static void addGmin (Netlist& net)
    {
        for (size_t i = 0; i < NumNodes; ++i)
            net.g[i][i] += 1.0e-12;
    }

    template <size_t R, size_t M, size_t C>
    static Mat<R, C> multiply (const Mat<R, M>& a, const Mat<M, C>& b)
    {
        Mat<R, C> out {};
        for (size_t r = 0; r < R; ++r)
            for (size_t m = 0; m < M; ++m)
                for (size_t c = 0; c < C; ++c)
                    out[r][c] += a[r][m] * b[m][c];
        return out;
    }

    template <size_t N>
    static bool invert (Mat<N, N>& a)
    {
        Mat<N, N> inv {};
        for (size_t i = 0; i < N; ++i)
            inv[i][i] = 1.0;

        for (size_t col = 0; col < N; ++col)
        {
            size_t piv = col;
            for (size_t r = col + 1; r < N; ++r)
                if (std::abs (a[r][col]) > std::abs (a[piv][col]))
                    piv = r;

            if (std::abs (a[piv][col]) < 1.0e-300)
                return false;

            std::swap (a[col], a[piv]);
            std::swap (inv[col], inv[piv]);

            const auto scale = 1.0 / a[col][col];
            for (size_t c = 0; c < N; ++c)
            {
                a[col][c] *= scale;
                inv[col][c] *= scale;
            }

            for (size_t r = 0; r < N; ++r)
            {
                if (r == col)
                    continue;
                const auto f = a[r][col];
                for (size_t c = 0; c < N; ++c)
                {
                    a[r][c] -= f * a[col][c];
                    inv[r][c] -= f * inv[col][c];
                }
            }
        }

        a = inv;
        return true;
    }

    //==============================================================================
    std::array<PortModel, NumPorts> ports {};
    PortVector gmax {}, vcrit {}, tolScale {};
    double vt = 0.025852, invVt = 1.0 / 0.025852, sampleRate = 48000.0;

    Mat<NumPorts, NumStates> px {};
    Mat<NumPorts, NumPorts> k {};
    PortVector kSelf {};
    PortVector pu {}, p0 {}, p0Nominal {}, pBias {};
    Mat<NumStates, NumStates> ax {};
    Mat<NumStates, NumPorts> ai {};
    StateVector au {}, x0 {}, cx {}, x0Nominal {}, xBias {};
    PortVector ci {};
    double cu = 0.0, y0 = 0.0, y0Nominal = 0.0, yBias = 0.0;

    // reactive elements: companion conductance, sign of J in its update, branch voltage
    StateVector ge {}, stateSign {};
    Mat<NumStates, NumStates> brX {};
    Mat<NumStates, NumPorts> brI {};
    StateVector brU {}, brConst {}, brBias {};
    double biasAmps = 0.0, biasRange = 0.0;

    static constexpr size_t biasGridSize = 17;  // odd: the middle point is the nominal one

    /** The grid reaches past the usable range, so the knob never uses the (less accurate)
        end segments of the spline. */
    double gridRange() const noexcept { return 1.25 * biasRange; }
    std::array<RestPoint, biasGridSize> biasGrid {};
    RestPoint restAtBias {};   // rest point the states are currently referenced to
    bool biasGridReady = false;

    Mat<NumNodes, NumStates> nodeX {};
    Mat<NumNodes, NumPorts> nodeI {};
    NodeVector nodeU {}, nodeConst {}, nodeBias {}, dcNodes {};
    StateVector dcInductorCurrent {};
    bool dcOk = false;

    StateVector xRest {}, x {}, xLast {};
    PortVector vRest {}, iRest {}, slopeRest {};
    PortVector vPrev {}, iPrev {}, slopePrev {};
    double lastVin = 0.0;
    int iterationsUsed = 0;
};

} // namespace umber::dsp::classa
