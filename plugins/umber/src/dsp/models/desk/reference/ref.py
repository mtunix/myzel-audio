"""
Independent continuous-time reference for the Desk model.

Same equivalent circuit + Jiles-Atherton core as the C++ model, but integrated as an ODE in
(H, M, i_leak, v_cw, v_cc) with classical RK4 at a very high fixed rate (default 8 MHz),
convergence-checked by step halving. The C++ model instead uses trapezoidal WDFs with a
semi-implicit JA root, so agreement validates the discretisation / solver.
"""
import numpy as np
from numba import njit

MU0 = 4e-7 * np.pi

# ----------------------------------------------------------------------------------------
# parameters (keep in sync with DeskModel.cpp)
# ----------------------------------------------------------------------------------------
# material: Ms, a, k, c, alpha
IT_MAT = np.array([5.5e5, 3.0, 1.0, 0.5, 3.0e-7])
OT_MAT = np.array([1.1e6, 20.0, 5.0, 0.7, 2.0e-6])

def make_params():
    p = {}
    p['it'] = dict(mat=IT_MAT.copy(), R1=950.0, R2=350.0, Ll=30e-3, Cw=150e-12, RL=10e3,
                   Cc=0.0, lamSat=0.06, Lsmall=46.0)
    p['ot'] = dict(mat=OT_MAT.copy(), R1=87.0, R2=40.0, Ll=10e-3, Cw=250e-12, RL=10e3,
                   Cc=2200e-6, lamSat=0.04, Lsmall=2.5)
    p['amp'] = dict(G=8.0, L=3.0, Vr=15.0, Up=1.0, Un=0.92, pp=2.0, pn=2.0)
    p['Kin'] = 2.5
    return p

def chi_init(mat):
    Ms, a, k, c, al = mat
    dMan = Ms / (3 * a)
    return c * dMan / (1 - al * c * dMan)

def derive(tr):
    """K = mu0 N A and lN = l / N from saturation flux and small-signal inductance."""
    Ms = tr['mat'][0]
    K = tr['lamSat'] / Ms
    lN = K * (1 + chi_init(tr['mat'])) / tr['Lsmall']
    return K, lN

# ----------------------------------------------------------------------------------------
@njit(cache=True)
def langevin(x):
    ax = abs(x)
    if ax < 0.05:
        x2 = x * x
        L = x * (1.0 / 3 - x2 * (1.0 / 45 - x2 * (2.0 / 945)))
        dL = 1.0 / 3 - x2 * (1.0 / 15 - x2 * (2.0 / 189 - x2 * (1.0 / 675)))
        return L, dL
    e = np.exp(-2.0 * ax)
    coth = (1.0 + e) / (1.0 - e)
    L = coth - 1.0 / ax
    dL = 1.0 / (ax * ax) - (coth * coth - 1.0)
    if x < 0:
        L = -L
    return L, dL

@njit(cache=True)
def ja_chi(H, M, delta, Ms, a, k, c, al):
    He = H + al * M
    L, dL = langevin(He / a)
    Man = Ms * L
    dMan = Ms / a * dL
    Mdiff = Man - M
    irr = 0.0
    if delta * Mdiff > 0.0:
        den = (1.0 - c) * k - al * abs(Mdiff)
        den = max(den, 0.05 * (1.0 - c) * k)
        irr = (1.0 - c) * abs(Mdiff) / den
    chi = (irr + c * dMan) / (1.0 - al * c * dMan)
    return max(chi, 0.0)

# ----------------------------------------------------------------------------------------
# amp static curve: u + L S(u) = w, y = Vr S(u)
@njit(cache=True)
def amp_S(u, Up, Un, pp, pn):
    if u >= 0:
        r = u / Up
        return u / (1.0 + r ** pp) ** (1.0 / pp)
    r = -u / Un
    return u / (1.0 + r ** pn) ** (1.0 / pn)

@njit(cache=True)
def amp_static(x, G, Lg, Vr, Up, Un, pp, pn):
    """Closed loop u + L S(u) = w by Newton (numerical slope) with bisection safeguard."""
    A = G * (1.0 + Lg)
    w = A * x / Vr
    lo = w - Lg * Up
    hi = w + Lg * Un
    u = min(max(w / (1.0 + Lg), lo), hi)
    for _ in range(200):
        s = amp_S(u, Up, Un, pp, pn)
        g = u + Lg * s - w
        if g > 0: hi = u
        else: lo = u
        e = 1e-7 * max(1.0, abs(u))
        ds = (amp_S(u + e, Up, Un, pp, pn) - amp_S(u - e, Up, Un, pp, pn)) / (2 * e)
        un_ = u - g / (1.0 + Lg * ds)
        if not (lo <= un_ <= hi):
            un_ = 0.5 * (lo + hi)
        if abs(un_ - u) < 1e-15 * (1 + abs(u)) or hi - lo < 1e-15:
            u = un_
            break
        u = un_
    return Vr * amp_S(u, Up, Un, pp, pn)

# ----------------------------------------------------------------------------------------
# transformer ODE: state = [H, M, il, vo, vcc]
@njit(cache=True)
def tr_deriv(st, vs, K, lN, R1, R2, Ll, Cw, RL, Cc, mat):
    H, M, il, vo, vcc = st[0], st[1], st[2], st[3], st[4]
    im = lN * H
    vm = vs - vcc - R1 * (im + il)
    d = 1.0 if vm >= 0 else -1.0
    chi = ja_chi(H, M, d, mat[0], mat[1], mat[2], mat[3], mat[4])
    dH = vm / (K * (1.0 + chi))
    out = np.empty(5)
    out[0] = dH
    out[1] = chi * dH
    out[2] = (vm - R2 * il - vo) / Ll
    out[3] = (il - vo / RL) / Cw
    out[4] = (im + il) / Cc if Cc > 0 else 0.0
    return out

@njit(cache=True)
def simulate_chain(x, fs_in, up, itp, otp, ampp, Kin, mode):
    """x: input samples at fs_in; integrates with RK4 at fs_in*up, linear interpolation of
    the input between samples. mode: 0 full chain, 1 IT only (output = IT vo), 2 OT only
    driven directly by Kin*x. Returns output at fs_in (volts) and the core (H, B) traces."""
    n = x.shape[0]
    h = 1.0 / (fs_in * up)
    s1 = np.zeros(5)
    s2 = np.zeros(5)
    y = np.zeros(n)
    Htr = np.zeros(n)
    Btr = np.zeros(n)
    K1, lN1, R11, R21, Ll1, Cw1, RL1, Cc1 = itp[0], itp[1], itp[2], itp[3], itp[4], itp[5], itp[6], itp[7]
    K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2 = otp[0], otp[1], otp[2], otp[3], otp[4], otp[5], otp[6], otp[7]
    mat1 = itp[8:13]
    mat2 = otp[8:13]
    G, Lg, Vr, Up, Un, pp, pn = ampp[0], ampp[1], ampp[2], ampp[3], ampp[4], ampp[5], ampp[6]
    xprev = 0.0
    for i in range(n):
        for j in range(up):
            t0 = j / up
            # input at stage times (linear interpolation)
            va = Kin * (xprev + (x[i] - xprev) * t0)
            vm_ = Kin * (xprev + (x[i] - xprev) * (t0 + 0.5 / up))
            vb = Kin * (xprev + (x[i] - xprev) * (t0 + 1.0 / up))
            # RK4 over coupled system (IT -> amp -> OT); amp is static so OT source depends on IT vo
            if mode == 2:
                k1b = tr_deriv(s2, va, K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2, mat2)
                k2b = tr_deriv(s2 + 0.5 * h * k1b, vm_, K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2, mat2)
                k3b = tr_deriv(s2 + 0.5 * h * k2b, vm_, K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2, mat2)
                k4b = tr_deriv(s2 + h * k3b, vb, K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2, mat2)
                s2 = s2 + h / 6 * (k1b + 2 * k2b + 2 * k3b + k4b)
                continue
            k1a = tr_deriv(s1, va, K1, lN1, R11, R21, Ll1, Cw1, RL1, Cc1, mat1)
            if mode == 0:
                k1b = tr_deriv(s2, amp_static(s1[3], G, Lg, Vr, Up, Un, pp, pn), K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2, mat2)
            sa = s1 + 0.5 * h * k1a
            k2a = tr_deriv(sa, vm_, K1, lN1, R11, R21, Ll1, Cw1, RL1, Cc1, mat1)
            if mode == 0:
                k2b = tr_deriv(s2 + 0.5 * h * k1b, amp_static(sa[3], G, Lg, Vr, Up, Un, pp, pn), K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2, mat2)
            sa = s1 + 0.5 * h * k2a
            k3a = tr_deriv(sa, vm_, K1, lN1, R11, R21, Ll1, Cw1, RL1, Cc1, mat1)
            if mode == 0:
                k3b = tr_deriv(s2 + 0.5 * h * k2b, amp_static(sa[3], G, Lg, Vr, Up, Un, pp, pn), K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2, mat2)
            sa = s1 + h * k3a
            k4a = tr_deriv(sa, vb, K1, lN1, R11, R21, Ll1, Cw1, RL1, Cc1, mat1)
            if mode == 0:
                k4b = tr_deriv(s2 + h * k3b, amp_static(sa[3], G, Lg, Vr, Up, Un, pp, pn), K2, lN2, R12, R22, Ll2, Cw2, RL2, Cc2, mat2)
                s2 = s2 + h / 6 * (k1b + 2 * k2b + 2 * k3b + k4b)
            s1 = s1 + h / 6 * (k1a + 2 * k2a + 2 * k3a + k4a)
        xprev = x[i]
        if mode == 1:
            y[i] = s1[3]
            Htr[i] = s1[0]
            Btr[i] = MU0 * (s1[0] + s1[1])
        else:
            y[i] = s2[3]
            Htr[i] = s2[0]
            Btr[i] = MU0 * (s2[0] + s2[1])
    return y, Htr, Btr

def pack_tr(tr):
    K, lN = derive(tr)
    return np.concatenate([[K, lN, tr['R1'], tr['R2'], tr['Ll'], tr['Cw'], tr['RL'], tr['Cc']], tr['mat']])

def pack_amp(a):
    return np.array([a['G'], a['L'], a['Vr'], a['Up'], a['Un'], a['pp'], a['pn']])

# ----------------------------------------------------------------------------------------
def tone_amp(x, f, fs):
    n = len(x)
    i = np.arange(n)
    win = 0.5 - 0.5 * np.cos(2 * np.pi * i / (n - 1))
    w = 2 * np.pi * f / fs
    re = np.sum(win * x * np.cos(w * i))
    im = -np.sum(win * x * np.sin(w * i))
    return 2 * np.hypot(re, im) / np.sum(win)

def harmonics(y, f, fs, nh=9):
    y = y - np.mean(y)
    f0 = tone_amp(y, f, fs)
    hs = []
    for h in range(2, nh + 2):
        if h * f >= 0.45 * fs:
            break
        hs.append(tone_amp(y, h * f, fs))
    hs = np.array(hs)
    return f0, hs, np.sqrt(np.sum(hs ** 2)) / max(f0, 1e-12)

def run_tone(p, f, A, fs=96000.0, up=64, mode=0, settle=0.25, periods=None):
    if periods is None:
        periods = max(20, int(round(0.1 * f)))
    ns = int(settle * fs)
    nw = int(round(periods * fs / f))
    t = np.arange(ns + nw) / fs
    x = A * np.sin(2 * np.pi * f * t)
    y, H, B = simulate_chain(x, fs, up, pack_tr(p['it']), pack_tr(p['ot']), pack_amp(p['amp']), p['Kin'], mode)
    return x, y, H, B, ns
