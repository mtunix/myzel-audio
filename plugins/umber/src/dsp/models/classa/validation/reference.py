"""
Usage: python3 reference.py   (needs numpy, scipy and g++)

Independent reference for the Umber Class A circuit.

Hand-written nodal (KCL) equations for the same netlist — no DK/state-space reduction, no
trapezoidal companions:
  - DC operating point: scipy.optimize.root on KCL with capacitors open, inductor shorted
  - small-signal: complex nodal admittance matrix of the linearised circuit at s = j*w
  - transient: the circuit as an ODE in node voltages + core flux (1 pF parasitic from every
    node to ground makes the capacitance matrix non-singular) integrated with scipy's adaptive
    5th-order implicit Radau IIA, rtol 1e-9. Compared are the samples (ClassACircuit::process)
    and the averages over each sample period (lastAverage, what the model plays before its
    equaliser), the latter against 4-point Gauss-Legendre averages of the dense solution.
"""
import os, subprocess, sys, tempfile
import numpy as np
from scipy.optimize import root
from scipy.integrate import solve_ivp

# The C++ side: a tiny tool that runs ClassACircuit and prints its raw volts.
DUMP_CPP = r"""// Dumps raw circuit data for the Python reference comparison.
//   dump dc | dump ac <rate> | dump tran <rate> <freq> <amp(dBFS units)> <seconds>
#include "dsp/models/classa/ClassAModel.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
using namespace umber;
int main (int argc, char** argv)
{
    dsp::classa::Components c;
    dsp::classa::ClassACircuit circuit;
    if (! std::strcmp (argv[1], "dc"))
    {
        circuit.prepare (192000.0, c);
        for (auto v : circuit.dcNodeVoltages()) std::printf ("%.9f\n", v);
    }
    else if (! std::strcmp (argv[1], "ac"))
    {
        circuit.prepare (atof (argv[2]), c);
        for (double f = 2.0; f < 90000.0; f *= 1.2589254)
        {
            auto h = circuit.smallSignalResponse (f);
            std::printf ("%.6f %.9e %.9e\n", f, h.real(), h.imag());
        }
    }
    else
    {
        const double rate = atof (argv[2]), freq = atof (argv[3]), amp = atof (argv[4]), secs = atof (argv[5]);
        circuit.prepare (rate, c);
        const int n = (int) (secs * rate);
        const double k = dsp::ClassAModel::defaultInputVolts, rail = dsp::ClassAModel::sourceRailVolts;
        for (int i = 0; i < n; ++i)
        {
            const double v = k * amp * std::sin (2.0 * std::numbers::pi * freq * i / rate);
            const double src = v / std::sqrt (1.0 + v * v / (rail * rail));
            const double y = circuit.process (src);
            auto nodes = circuit.lastNodeVoltages();
            std::printf ("%.12e %.12e %.9e %.9e %.9e %.12e\n", src, y, nodes[1], nodes[5], nodes[6], circuit.lastAverage());
        }
    }
}
"""

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../.."))
HARNESS = os.path.join(tempfile.gettempdir(), "classa_dump")
with open(HARNESS + ".cpp", "w") as f:
    f.write(DUMP_CPP)
subprocess.run(["g++", "-std=c++20", "-O2", "-I" + os.path.join(REPO, "src"), "-o", HARNESS, HARNESS + ".cpp",
                os.path.join(REPO, "src/dsp/models/classa/ClassACircuit.cpp"),
                os.path.join(REPO, "src/dsp/models/classa/ClassAModel.cpp")], check=True)

# --- component values (must match classa::Components defaults) ---
Vcc = 24.0; Rs = 600.0; Cin = 4.7e-6; R1 = 47e3; R2 = 6.8e3; Rbb = 120.0
Re1 = 22.0; Re2 = 1e3; Ce = 1000e-6; Rc = 5.6e3; Ccb = 33e-12
Re3 = 1.5e3; Cout = 100e-6; Rw = 40.0; Lm = 4.0; RL = 600.0; PhiS = 0.016; Knee = 0.3
Is1 = 1e-14; BF1 = 250.0; BR1 = 4.0; Is2 = 4e-14; BF2 = 120.0; Vt = 0.025852
K_IN = 0.035; RAIL = 12.0

A, B, Bi, E1, E2, C1, E3, T0, T = range(9)
NN = 9
names = ["A", "B", "Bi", "E1", "E2", "C1", "E3", "T0", "T"]

def dexp(x):
    return np.exp(np.minimum(x, 80.0))

def bjt_currents(V):
    """Terminal currents INTO the devices (Ebers–Moll transport model)."""
    IF = Is1 * (dexp((V[Bi] - V[E1]) / Vt) - 1)
    IR = Is1 * (dexp((V[Bi] - V[C1]) / Vt) - 1)
    ic1 = IF - IR * (1 + BR1) / BR1
    ib1 = IF / BF1 + IR / BR1
    ie1 = ic1 + ib1                 # out of emitter
    IF2 = Is2 * (dexp((V[C1] - V[E3]) / Vt) - 1)
    ib2 = IF2 / BF2
    ie2 = IF2 * (1 + BF2) / BF2     # out of emitter (collector on the rail)
    return ib1, ic1, ie1, ib2, ie2

def core_current(phi):
    r = phi / PhiS
    u = np.maximum(0.0, r * r - Knee * Knee)
    return phi / Lm * u * u

def resistive_kcl(V, vin):
    """Sum of currents LEAVING each node through resistors, sources and devices."""
    f = np.zeros(NN)
    ib1, ic1, ie1, ib2, ie2 = bjt_currents(V)
    f[A]  += (V[A] - vin) / Rs
    f[B]  += (V[B] - Vcc) / R1 + V[B] / R2 + (V[B] - V[Bi]) / Rbb
    f[Bi] += (V[Bi] - V[B]) / Rbb + ib1
    f[E1] += (V[E1] - V[E2]) / Re1 - ie1
    f[E2] += (V[E2] - V[E1]) / Re1 + V[E2] / Re2
    f[C1] += (V[C1] - Vcc) / Rc + ic1 + ib2
    f[E3] += V[E3] / Re3 - ie2
    f[T0] += (V[T0] - V[T]) / Rw
    f[T]  += (V[T] - V[T0]) / Rw + V[T] / RL
    return f

# ---------------- DC ----------------
def dc_eqs(V):
    f = resistive_kcl(V, 0.0)
    # capacitors open: A floats on Rs only (fine); Cout open: T0/T tied to ground by the inductor short
    f[T] += V[T] * 1e4  # inductor = short
    return f

guess = np.array([0, 3, 3, 2.3, 2.25, 11, 10.4, 0, 0])
sol = root(dc_eqs, guess, method="hybr", tol=1e-14)
Vdc = sol.x

cpp_dc = np.array([float(l) for l in subprocess.run([HARNESS, "dc"], capture_output=True, text=True).stdout.split()])
print("== DC operating point (V) ==")
for i in range(NN):
    print(f"  {names[i]:3s} python {Vdc[i]:10.6f}   C++ {cpp_dc[i]:10.6f}   diff {Vdc[i]-cpp_dc[i]:+.2e}")
print(f"  Ic1 = {(Vcc - Vdc[C1]) / Rc * 1e3:.3f} mA, Ie2 = {Vdc[E3] / Re3 * 1e3:.3f} mA")

# ---------------- AC ----------------
def jacobian(fun, V, h=1e-7):
    J = np.zeros((NN, NN))
    f0 = fun(V)
    for j in range(NN):
        d = np.zeros(NN); d[j] = h
        J[:, j] = (fun(V + d) - fun(V - d)) / (2 * h)
    return J

G = jacobian(lambda V: resistive_kcl(V, 0.0), Vdc)
Bin = np.zeros(NN); Bin[A] = 1.0 / Rs   # d(-f)/dvin

def cap_matrix():
    Cm = np.zeros((NN, NN))
    def stamp(a, b, c):
        Cm[a, a] += c
        if b is not None:
            Cm[b, b] += c; Cm[a, b] -= c; Cm[b, a] -= c
    stamp(A, B, Cin); stamp(C1, B, Ccb); stamp(E2, None, Ce); stamp(E3, T0, Cout)
    return Cm

Cm = cap_matrix()

def ac_response(f):
    s = 2j * np.pi * f
    Y = G + s * Cm
    Y[T, T] += 1.0 / (s * Lm)     # core is linear below its knee at rest
    v = np.linalg.solve(Y, Bin)
    return v[T]

cpp_ac = np.loadtxt(subprocess.run([HARNESS, "ac", "768000"], capture_output=True, text=True).stdout.splitlines())
ref1k = ac_response(1000.0)
print("\n== small-signal response vin -> V(T) (C++ at fs = 768 kHz, bilinear) ==")
print(f"  |H(1 kHz)|: python {abs(ref1k):.4f}  C++ {abs(complex(np.interp(1000.0, cpp_ac[:,0], cpp_ac[:,1]), np.interp(1000.0, cpp_ac[:,0], cpp_ac[:,2]))):.4f}")
worst = 0.0
for row in cpp_ac[::4]:
    f, hr, hi = row
    if f > 40000: continue
    hp = ac_response(f)
    hc = complex(hr, hi)
    dmag = 20 * np.log10(abs(hc) / abs(hp)); dph = np.degrees(np.angle(hc / hp))
    worst = max(worst, abs(dmag))
    print(f"  {f:9.1f} Hz  ref {20*np.log10(abs(hp)/abs(ref1k)):+7.2f} dB  C++ {20*np.log10(abs(hc)/abs(ref1k)):+7.2f} dB   diff {dmag:+.3f} dB {dph:+.2f} deg")
print(f"  worst magnitude difference (2 Hz - 40 kHz): {worst:.3f} dB")

if os.environ.get("CLASSA_QUICK"):   # DC + AC only
    sys.exit(0)

# ---------------- transient ----------------
PAR = 1e-12
Ct = Cm + np.eye(NN) * PAR
Ct_inv = np.linalg.inv(Ct)

def make_rhs(freq, amp):
    def src(t):
        v = K_IN * amp * np.sin(2 * np.pi * freq * t)
        return v / np.sqrt(1 + (v / RAIL) ** 2)
    def rhs(t, y):
        V = y[:NN]; phi = y[NN]
        i_out = resistive_kcl(V, src(t))
        i_out[T] += phi / Lm + core_current(phi)   # inductor current leaves T
        dV = -Ct_inv @ i_out
        return np.concatenate([dV, [V[T]]])
    return rhs, src

def run_case(freq, amp, secs, rate=192000.0):
    rhs, src = make_rhs(freq, amp)
    y0 = np.concatenate([Vdc, [0.0]])
    n = int(secs * rate)
    t_eval = np.arange(n) / rate
    sol = solve_ivp(rhs, (0, t_eval[-1]), y0, method="Radau", t_eval=t_eval, rtol=1e-9, atol=1e-11,
                    max_step=1.0 / (8 * freq), dense_output=True)
    out = subprocess.run([HARNESS, "tran", str(rate), str(freq), str(amp), str(secs)], capture_output=True, text=True).stdout
    cpp = np.loadtxt(out.splitlines())
    # reference period averages over [t_{n-1}, t_n]
    gx, gw = np.polynomial.legendre.leggauss(4)
    avg = np.zeros(n)
    for x, w in zip(gx, gw):
        avg[1:] += 0.5 * w * sol.sol(t_eval[1:] - (1 - x) / (2 * rate))[T]
    return t_eval, sol.y[T], cpp[:, 1], sol.y[C1], cpp[:, 3], avg, cpp[:, 5]

def harmonics(x, freq, rate, nh=5):
    # last 20 periods, Hann window
    n = int(round(20 * rate / freq)); seg = x[-n:] - np.mean(x[-n:])
    w = np.hanning(n); t = np.arange(n) / rate
    amps = [abs(np.sum(w * seg * np.exp(-2j * np.pi * h * freq * t))) * 2 / np.sum(w) for h in range(1, nh + 2)]
    fund = amps[0]
    return fund, [a / fund * 100 for a in amps[1:]], np.sqrt(sum(a * a for a in amps[1:])) / fund * 100

print("\n== transient, 1 kHz sine from rest, 40 ms, fs = 192 kHz (C++) vs Radau IIA ==")
print("  level      max|err| V(T)   rms err / rms    THD ref   THD C++   H2/H3 ref        H2/H3 C++")
averages = []
for amp in [0.063, 0.25, 1.0, 8.0, 80.0]:
    t, ref, cpp, refc, cppc, ravg, cavg = run_case(1000.0, amp, 0.04)
    averages.append((amp, ravg[3:], cavg[3:]))
    err = np.max(np.abs(ref - cpp)); rel = np.sqrt(np.mean((ref - cpp) ** 2)) / np.sqrt(np.mean(ref ** 2))
    fr, hr, thr = harmonics(ref, 1000.0, 192000.0); fc, hc, thc = harmonics(cpp, 1000.0, 192000.0)
    print(f"  {20*np.log10(amp):+6.1f} dBFS  {err:.3e} V     {rel:.2e}      {thr:6.2f}%  {thc:6.2f}%   {hr[0]:6.3f}/{hr[1]:6.3f}   {hc[0]:6.3f}/{hc[1]:6.3f}")

print("\n== the same runs, averages over each sample period ==")
for amp, ravg, cavg in averages:
    err = np.max(np.abs(ravg - cavg)); rel = np.sqrt(np.mean((ravg - cavg) ** 2)) / np.sqrt(np.mean(ravg ** 2))
    print(f"  {20*np.log10(amp):+6.1f} dBFS  max|err| {err:.3e} V   rms err/rms {rel:.2e}")

print("\n== transient, 40 Hz (core saturation region), 100 ms ==")
for amp in [1.0, 4.0]:
    t, ref, cpp, refc, cppc, ravg, cavg = run_case(40.0, amp, 0.1)
    err = np.max(np.abs(ref - cpp)); rel = np.sqrt(np.mean((ref - cpp) ** 2)) / np.sqrt(np.mean(ref ** 2))
    print(f"  {20*np.log10(amp):+6.1f} dBFS  max|err| {err:.3e} V   rms err/rms {rel:.2e}   peak V(T) ref {np.max(np.abs(ref)):.3f} C++ {np.max(np.abs(cpp)):.3f}")
