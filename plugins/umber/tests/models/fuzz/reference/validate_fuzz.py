"""Independent reference for FuzzModel: continuous-time DAE of the same netlist.
7 reactive states integrated with scipy Radau (rtol 1e-10); the 3 algebraic node voltages
(grid1, plate1, grid2) are solved inside every RHS call by Newton. Exact Koren equations."""
import numpy as np, subprocess
import os, pathlib, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[4]
DUMP_SOURCE = r"""
#include MODEL_HEADER
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>
int main (int argc, char** argv)
{
    const double amp = std::atof (argv[1]), rate = std::atof (argv[2]), ms = std::atof (argv[3]);
    MODEL model;
    model.prepare (rate);
    model.reset();
    const int n = (int) (ms * 1.0e-3 * rate);
    std::vector<float> s ((size_t) n);
    for (int i = 0; i < n; ++i)
        s[(size_t) i] = (float) (amp * std::sin (2.0 * std::numbers::pi * 1000.0 / rate * i));
    model.process (s.data(), n);
    for (auto v : s)
        std::printf ("%.9g\n", v);
}
"""

def build_dump(name, header, cls, source):
    """Compiles a tiny tool that prints the model's response to a 1 kHz sine (C++20 compiler needed)."""
    out = pathlib.Path(tempfile.gettempdir()) / ("umber_dump_" + name)
    src = out.with_suffix(".cpp")
    src.write_text(DUMP_SOURCE)
    cxx = os.environ.get("CXX", "c++")
    subprocess.run([cxx, "-std=c++20", "-O2", "-DNDEBUG", "-I", str(ROOT / "src"),
                    '-DMODEL_HEADER="%s"' % header, "-DMODEL=umber::dsp::%s" % cls,
                    str(src), str(ROOT / source), "-o", str(out)], check=True)
    return str(out)

from scipy.integrate import solve_ivp
from scipy.optimize import fsolve

VB = 285.0
Cin1, Rg1, Rs1, Ra1, Rk1, Ck1 = 22e-9, 1e6, 33e3, 100e3, 1.5e3, 6.8e-6
Cc, Rg2, Rs2, Rk2, Ck2 = 47e-9, 220e3, 10e3, 180.0, 220e-6
Rscr, Cscr = 1e3, 22e-6
Rw, Lp, RL, Cw = 250.0, 40.0, 5e3, 1e-9
KIN = 0.12
# 12AX7 (Koren) + grid diode
tmu, tex, tkg1, tkp, tkvb = 100.0, 1.4, 1060.0, 600.0, 300.0
# EL84 (fitted) + grid diode + current division
pmu, pex, pkg1, pkg2, pkp, pkvb, kdiv = 19.0, 1.35, 659.0, 2774.0, 200.0, 30.0, 0.4
cg, v0 = 11.6, 0.2

def softplus(a):
    return max(a, 0.0) + np.log1p(np.exp(-abs(a)))

def grid(v, rgi):
    return softplus(cg * (v - v0)) / (cg * rgi)

def triode(vgk, vpk):
    ip = 1e-9 * vpk
    if vpk > 0:
        e1 = vpk / tkp * softplus(tkp * (1 / tmu + vgk / np.sqrt(tkvb + vpk * vpk)))
        ip += 2.0 * e1 ** tex / tkg1
    return grid(vgk, 2000.0), ip

def pentode(vg1, vg2, vpk):
    ig1 = grid(vg1, 1000.0)
    ip, ig2 = 1e-9 * vpk, 1e-9 * vg2
    if vg2 > 1.0:
        e1 = vg2 / pkp * softplus(pkp * (1 / pmu + vg1 / vg2))
        ik = 2.0 * e1 ** pex / pkg1
        at = np.arctan(vpk / pkvb)
        ip += ik * at
        ig2 += ik * (pkg1 / (2 * pkg2) + kdiv * (np.pi / 2 - at))
    return ig1, ig2, ip

z_guess = None
def nodes(x, u):
    """Solve the algebraic nodes; returns everything the RHS needs."""
    global z_guess
    vcin1, vk1, vcc, vk2, vscr, il, vcw = x
    vleak1 = u - vcin1
    def f(z):
        vg1, vp1, vg2 = z
        ig1, ip1 = triode(vg1 - vk1, vp1 - vk1)
        vleak2 = vp1 - vcc
        irs2 = (vleak2 - vg2) / Rs2
        igp = grid(vg2 - vk2, 1000.0)
        icc = vleak2 / Rg2 + irs2
        return np.array([((vleak1 - vg1) / Rs1 - ig1) * Rs1,
                         ((vscr - vp1) / Ra1 - ip1 - icc) * Ra1,
                         (irs2 - igp) * Rs2])
    z = np.array(z_guess if z_guess is not None else [vleak1, 150.0, 0.0])
    for it in range(100):
        F0 = f(z)
        J = np.zeros((3, 3))
        for k in range(3):
            dz = np.zeros(3); h = 1e-7 * max(1.0, abs(z[k])); dz[k] = h
            J[:, k] = (f(z + dz) - f(z - dz)) / (2 * h)
        step = np.linalg.solve(J, -F0)
        z = z + step
        if np.abs(step).max() < 1e-12 * max(1.0, np.abs(z).max()):
            break
    z_guess = z.copy()
    return z, vleak1

def rhs(t, x, ufun):
    u = ufun(t)
    vcin1, vk1, vcc, vk2, vscr, il, vcw = x
    (vg1, vp1, vg2), vleak1 = nodes(x, u)
    ig1, ip1 = triode(vg1 - vk1, vp1 - vk1)
    vleak2 = vp1 - vcc
    irs2 = (vleak2 - vg2) / Rs2
    vp2 = VB - vcw
    igp, ig2, ip2 = pentode(vg2 - vk2, vscr - vk2, vp2 - vk2)
    icc = vleak2 / Rg2 + irs2
    icin1 = vleak1 / Rg1 + (vleak1 - vg1) / Rs1
    return [icin1 / Cin1,
            (ig1 + ip1 - vk1 / Rk1) / Ck1,
            icc / Cc,
            (igp + ig2 + ip2 - vk2 / Rk2) / Ck2,
            ((VB - vscr) / Rscr - ig2 - (vscr - vp1) / Ra1) / Cscr,
            (VB - Rw * il - vp2) / Lp,
            (ip2 - il - vcw / RL) / Cw]

CAPS = np.array([Cin1, Ck1, Cc, Ck2, Cscr, Lp, Cw])
x0 = fsolve(lambda x: np.array(rhs(0, x, lambda t: 0.0)) * CAPS, [0.0, 1.2, 150.0, 7.5, 275.0, 0.045, 11.0], xtol=1e-14)
res = np.abs(np.array(rhs(0, x0, lambda t: 0.0)) * CAPS).max()
(vg1, vp1, vg2), _ = nodes(x0, 0.0)
print("operating point (residual %.1e): Vgk1 %.3f V, Vpk1 %.2f V, Vg1k2 %.3f V, Vg2k2 %.2f V, Vpk2 %.2f V, Ip2 %.2f mA" %
      (res, vg1 - x0[1], vp1 - x0[1], vg2 - x0[3], x0[4] - x0[3], VB - x0[6] - x0[3], x0[5] * 1e3))

def jac(fun, x, eps=1e-6):
    f0 = np.array(fun(x)); J = np.zeros((len(f0), len(x)))
    for k in range(len(x)):
        dx = np.zeros(len(x)); h = eps * max(1.0, abs(x[k])); dx[k] = h
        J[:, k] = (np.array(fun(x + dx)) - np.array(fun(x - dx))) / (2 * h)
    return J
A = jac(lambda x: rhs(0, x, lambda t: 0.0), x0)
B = jac(lambda uu: rhs(0, x0, lambda t: uu[0]), np.array([0.0]))[:, 0]
C = np.zeros(7); C[6] = -1.0                       # output = Vp2 - B+ = -vCw
H = C @ np.linalg.solve(2j * np.pi * 1000 * np.eye(7) - A, B)
makeup = np.sign(H.real) / (abs(H) * KIN)
print("small-signal gain at 1 kHz: %.2f dB" % (20 * np.log10(abs(H))))

DUMP = build_dump("fuzz", "dsp/models/fuzz/FuzzModel.h", "FuzzModel", "src/dsp/models/fuzz/FuzzModel.cpp")

def cpp(amp, rate, ms):
    out = subprocess.run([DUMP, str(amp), str(rate), str(ms)], capture_output=True, text=True).stdout
    return np.array([float(v) for v in out.split()])

def harmonics(y, rate, f0=1000.0, n=5, periods=10):
    N = int(round(periods * rate / f0)); seg = y[-N:] - y[-N:].mean(); t = np.arange(N) / rate
    return np.array([2 * abs(np.sum(seg * np.exp(-2j * np.pi * h * f0 * t))) / N for h in range(1, n + 1)])

ms = 40.0
print("\n  A    rate   max|err|/peak (after 1 ms)   rms err    H1..H5 ref (dB)               H1..H5 C++ (dB)")
for amp in [0.063, 0.25, 1.0, 8.0]:
    z_guess = None
    sol = solve_ivp(rhs, (0, ms * 1e-3), x0, method="Radau", rtol=1e-10, atol=1e-13, dense_output=True, max_step=1e-5,
                    args=(lambda t, a=amp: KIN * a * np.sin(2 * np.pi * 1000 * t),))
    for rate in [192000, 768000]:
        n = int(ms * 1e-3 * rate); t = np.arange(n) / rate
        yref = makeup * (-sol.sol(t)[6] + x0[6])
        ycpp = cpp(amp, rate, ms)[:n]
        err = ycpp - yref; m = t > 1e-3
        hr, hc = harmonics(yref, rate), harmonics(ycpp, rate)
        print("%5g %6d   %6.1f dB (%6.1f dB)          %6.1f dB   %s   %s" % (amp, rate,
              20 * np.log10(abs(err).max() / abs(yref).max()), 20 * np.log10(abs(err[m]).max() / abs(yref).max()),
              20 * np.log10(np.sqrt(np.mean(err**2)) / np.sqrt(np.mean(yref**2))),
              " ".join("%6.1f" % (20 * np.log10(v + 1e-15)) for v in hr), " ".join("%6.1f" % (20 * np.log10(v + 1e-15)) for v in hc)), flush=True)
