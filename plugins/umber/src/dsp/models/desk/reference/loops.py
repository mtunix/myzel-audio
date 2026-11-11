import numpy as np, ref, subprocess
p=ref.make_params()
def feats(H,B,per):
    H=H[-per:]; B=B[-per:]
    Hc=Br=None
    for i in range(1,per):
        if H[i-1]<0<=H[i]: Br=B[i-1]+(B[i]-B[i-1])*(-H[i-1])/(H[i]-H[i-1])
        if B[i-1]<0<=B[i]: Hc=H[i-1]+(H[i]-H[i-1])*(-B[i-1])/(B[i]-B[i-1])
    area=abs(np.sum(H*np.gradient(B)))  # J/m^3 per cycle
    return Hc,Br,np.max(np.abs(B)),area
rate=96000; f=20
for mode,name,scale in ((1,'IT',2.5),(2,'OT',15.0)):
    tr=p['it'] if mode==1 else p['ot']
    for frac in (0.25,0.6,3.0):
        V=frac*tr['lamSat']*2*np.pi*f
        A=V/scale
        sec=1.0; n=int(sec*rate); per=int(rate/f)
        raw=subprocess.run(['./dump',str(rate),str(f),str(A),str(sec),str(mode)],capture_output=True).stdout
        d=np.frombuffer(raw).reshape(-1,3)
        x=A*np.sin(2*np.pi*f*np.arange(n)/rate)
        q=dict(p); q['Kin']=scale
        y,H,B=ref.simulate_chain(x,rate,42,ref.pack_tr(q['it']),ref.pack_tr(q['ot']),ref.pack_amp(q['amp']),scale,mode)
        c=feats(d[:,1],d[:,2],per); r=feats(H,B,per)
        print(f"{name} {f} Hz, flux {frac:4.2f} x sat: Hc {c[0]:8.3f}/{r[0]:8.3f} A/m  Br {c[1]:7.4f}/{r[1]:7.4f} T  Bpk {c[2]:6.3f}/{r[2]:6.3f} T  loss {c[3]:9.3f}/{r[3]:9.3f} J/m3/cycle  (C++/ref)",flush=True)
