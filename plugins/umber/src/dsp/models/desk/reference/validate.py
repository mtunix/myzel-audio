import numpy as np, ref, subprocess, sys
p = ref.make_params()
def cpp(rate,f,A,sec,mode):
    raw = subprocess.run(['./dump',str(rate),str(f),str(A),str(sec),str(mode)],capture_output=True).stdout
    d = np.frombuffer(raw,dtype=np.float64).reshape(-1,3)
    return d[:,0],d[:,1],d[:,2]
def refrun(rate,f,A,sec,mode,up):
    n=int(sec*rate); x=A*np.sin(2*np.pi*f*np.arange(n)/rate)
    q=dict(p)
    if mode==2: q=dict(p); q['Kin']=15.0
    return ref.simulate_chain(x,rate,up,ref.pack_tr(q['it']),ref.pack_tr(q['ot']),ref.pack_amp(q['amp']),q['Kin'],mode)
rows=[]
cases=[(m,f,A) for m in (0,) for f in (30,50,100,1000) for A in (0.063,0.25,1.0,4.0)] + [(1,50,4.0),(1,50,16.0),(1,20,8.0),(2,50,0.71),(2,50,1.0)]
for rate in (88200,192000):
    up = int(np.ceil(4e6/rate))
    for mode,f,A in cases:
        sec = 0.6 if f>=50 else 0.8
        yc,Hc,Bc = cpp(rate,f,A,sec,mode)
        yr,Hr,Br = refrun(rate,f,A,sec,mode,up)
        ns=int(0.4*rate)
        f0c,hc,tc = ref.harmonics(yc[ns:],f,rate); f0r,hr,tr = ref.harmonics(yr[ns:],f,rate)
        err = np.max(np.abs(yc[ns:]-yr[ns:]))/np.max(np.abs(yr[ns:]))
        h3c = 20*np.log10(max(hc[1],1e-12)/f0c); h3r=20*np.log10(max(hr[1],1e-12)/f0r)
        print(f"rate {rate:6d} mode {mode} f {f:5g} A {A:6g}: THD cpp {tc*100:8.3f}% ref {tr*100:8.3f}%  H3 {h3c:7.2f}/{h3r:7.2f} dB  max|err|/peak {err:.2e}  Bpk {np.max(np.abs(Bc[ns:])):.3f}/{np.max(np.abs(Br[ns:])):.3f}")
        sys.stdout.flush()
