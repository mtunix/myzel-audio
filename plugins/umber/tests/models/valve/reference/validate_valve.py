"""Independent reference for ValveModel: continuous-time nodal ODE of the same netlist,
integrated with scipy Radau (implicit, order 5, rtol 1e-10), exact Koren equations (no tables)."""
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

# netlist values (ValveModel.cpp)
VS, Rsup, Csup, Cin, Rg, Rs, Ra, Cgp, Cs, Rk, Ck, Cout, RL = 262.0, 10e3, 22e-6, 100e-9, 220e3, 47e3, 100e3, 1.7e-12, 10e-12, 1.5e3, 47e-6, 47e-9, 1e6
KIN = 0.6
mu, ex, kg1, kp, kvb = 100.0, 1.4, 1060.0, 600.0, 300.0
rgi, cg, v0 = 2000.0, 11.6, 0.2

def softplus(a):
    return np.where(a > 0, a + np.log1p(np.exp(-np.abs(a))), np.log1p(np.exp(np.minimum(a, 0))))

def tube(vgk, vpk):
    ig = softplus(cg * (vgk - v0)) / (cg * rgi)
    if vpk <= 0:
        return ig, 1e-9 * vpk
    e1 = vpk / kp * softplus(kp * (1 / mu + vgk / np.sqrt(kvb + vpk * vpk)))
    return ig, 2.0 * e1 ** ex / kg1 + 1e-9 * vpk

def rhs(t, x, ufun):
    # states: Cin (in - leak), Cgp (plate - grid), Cs (plate), Ck (cathode), Cout (plate - out), Csup (B+)
    vcin, vcgp, vp, vk, vcout, vb = x
    u = ufun(t)
    vleak = u - vcin
    vg = vp - vcgp
    vout = vp - vcout
    ig, ip = tube(vg - vk, vp - vk)
    irs = (vleak - vg) / Rs
    icin = vleak / Rg + irs
    icgp = irs - ig                      # grid -> plate through Cgp
    ira = (vb - vp) / Ra
    iout = vout / RL                     # plate -> out through Cout
    ics = ira + icgp - ip - iout
    ick = ig + ip - vk / Rk
    icsup = (VS - vb) / Rsup - ira
    return [icin / Cin, -icgp / Cgp, ics / Cs, ick / Ck, iout / Cout, icsup / Csup]

from scipy.optimize import brentq
def _f(vk):
    ip = vk / Rk
    vp = (VS - ip * (Ra + Rsup)) 
    return tube(-vk, vp - vk)[1] - ip
_vk = brentq(_f, 0.01, 5.0); _ip = _vk / Rk; _vp = VS - _ip * (Ra + Rsup)
CAPS = np.array([Cin, Cgp, Cs, Ck, Cout, Csup])
x0 = fsolve(lambda x: np.array(rhs(0, x, lambda t: 0.0)) * CAPS, [0.0, _vp, _vp, _vk, _vp, VS - Rsup * _ip], xtol=1e-15)
assert np.abs(np.array(rhs(0, x0, lambda t: 0.0)) * CAPS).max() < 1e-13
vp0 = x0[2] - x0[4]  # quiescent output (0)

# small-signal gain at 1 kHz from the linearised ODE (numerical Jacobian)
def jac(fun, x, eps=1e-6):
    f0 = np.array(fun(x)); J = np.zeros((len(f0), len(x)))
    for k in range(len(x)):
        dx = np.zeros(len(x)); dx[k] = eps * max(1.0, abs(x[k]))
        J[:, k] = (np.array(fun(x + dx)) - np.array(fun(x - dx))) / (2 * dx[k])
    return J
A = jac(lambda x: rhs(0, x, lambda t: 0.0), x0)
B = jac(lambda uu: rhs(0, x0, lambda t: uu[0]), np.array([0.0]))[:, 0]
C = np.array([0, 0, 1.0, 0, -1.0, 0])  # Vout = Vp - Vcout
w = 2 * np.pi * 1000
H = C @ np.linalg.solve(1j * w * np.eye(6) - A, B)
makeup = np.sign(H.real) / (abs(H) * KIN)
print("operating point: Vgk %.4f V  Vpk %.3f V  Vk %.4f V  Ip %.4f mA  |H(1k)| %.3f (%.2f dB)" % (x0[2] - x0[1] - x0[3], x0[2] - x0[3], x0[3], tube(x0[2]-x0[1]-x0[3], x0[2]-x0[3])[1]*1e3, abs(H), 20*np.log10(abs(H))))

DUMP = build_dump("valve", "dsp/models/valve/ValveModel.h", "ValveModel", "src/dsp/models/valve/ValveModel.cpp")

def cpp(amp, rate, ms):
    out = subprocess.run([DUMP, str(amp), str(rate), str(ms)], capture_output=True, text=True).stdout
    return np.array([float(v) for v in out.split()])

def harmonics(y, rate, f0=1000.0, n=5, periods=10):
    N = int(round(periods * rate / f0)); seg = y[-N:] - y[-N:].mean()
    t = np.arange(N) / rate
    return np.array([2 * abs(np.sum(seg * np.exp(-2j * np.pi * h * f0 * t))) / N for h in range(1, n + 1)])

ms = 40.0
print("\n  A   rate    max|err| / peak   rms err (dB rel rms)   H1..H5 ref (dB)            H1..H5 C++ (dB)")
for amp in [0.063, 0.25, 1.0, 8.0]:
    T = ms * 1e-3
    sol = solve_ivp(rhs, (0, T), x0, method="Radau", rtol=1e-10, atol=1e-12, dense_output=True,
                    max_step=1e-5, args=(lambda t, a=amp: KIN * a * np.sin(2 * np.pi * 1000 * t),))
    for rate in [192000, 768000]:
        n = int(ms * 1e-3 * rate)
        t = np.arange(n) / rate
        st = sol.sol(t); yref = makeup * (st[2] - st[4] - vp0)
        ycpp = cpp(amp, rate, ms)[:n]
        err = ycpp - yref
        hr, hc = harmonics(yref, rate), harmonics(ycpp, rate)
        print("%5g %6d   %9.2e (%6.1f dB)   %7.1f dB          %s   %s" % (amp, rate, abs(err).max() / abs(yref).max(),
              20 * np.log10(abs(err).max() / abs(yref).max()), 20 * np.log10(np.sqrt(np.mean(err**2)) / np.sqrt(np.mean(yref**2))),
              " ".join("%6.1f" % (20 * np.log10(v + 1e-15)) for v in hr), " ".join("%6.1f" % (20 * np.log10(v + 1e-15)) for v in hc)))
