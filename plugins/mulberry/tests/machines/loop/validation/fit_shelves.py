import numpy as np, importlib.util, sys
spec=importlib.util.spec_from_file_location('t','tape_losses.py'); t=importlib.util.module_from_spec(spec); spec.loader.exec_module(t)
def shelves(x, p):
    p=np.abs(p); h=np.ones_like(x)
    n=len(p)//2
    for i in range(n):
        pole, zero = p[2*i], p[2*i]+p[2*i+1]
        h=h*np.sqrt((1+(x/zero)**2)/(1+(x/pole)**2))
    if len(p)%2: h=h/np.sqrt(1+(x/p[-1])**2)
    return h
def fit(closed, xmax, nparams, seed):
    x=np.concatenate([np.geomspace(1e-4, xmax, 3000)])
    tg=20*np.log10(closed(x))
    rng=np.random.default_rng(seed); best=None
    for a in range(60):
        p0=np.geomspace(0.01, xmax*2, nparams)*np.exp(rng.normal(0,0.4,nparams))
        f=lambda q: np.max(np.abs(20*np.log10(shelves(x,np.abs(q)))-tg))
        q,v=t.nelder_mead(f,list(p0),iters=4000)
        for _ in range(3): q,v=t.nelder_mead(f,list(np.abs(q)),step=0.05,iters=4000)
        if best is None or v<best[1]: best=(np.abs(q),v)
    return best
which=sys.argv[1]
if which=='spacing': r=fit(t.spacing, 0.5, int(sys.argv[2]) if len(sys.argv)>2 else 7, 1)
else: r=fit(t.thickness, 0.7, 5, 2)
print(which, ', '.join('%.6f'%v for v in r[0]), 'minimax dB', round(r[1],4))
