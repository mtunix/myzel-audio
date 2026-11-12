import numpy as np, importlib.util, sys
spec=importlib.util.spec_from_file_location('t','tape_losses.py'); t=importlib.util.module_from_spec(spec); spec.loader.exec_module(t)
spec2=importlib.util.spec_from_file_location('s','fit_shelves.py')
def shelves(x, p):
    p=np.abs(p); h=np.ones_like(x)
    n=len(p)//2
    for i in range(n):
        pole, zero = p[2*i], p[2*i]+p[2*i+1]
        h=h*np.sqrt((1+(x/zero)**2)/(1+(x/pole)**2))
    if len(p)%2: h=h/np.sqrt(1+(x/p[-1])**2)
    return h
x=np.geomspace(1e-4,0.85,3000)
tg=20*np.log10(t.gap(x)); m=tg>=-12
n=int(sys.argv[1]); rng=np.random.default_rng(int(sys.argv[2])); best=None
for a in range(80):
    p0=np.geomspace(0.2,2.0,n)*np.exp(rng.normal(0,0.5,n))
    f=lambda q: np.max(np.abs(20*np.log10(shelves(x[m],np.abs(q)))-tg[m])) + 4*max(0,np.max(20*np.log10(shelves(x[~m],np.abs(q)))+12))
    q,v=t.nelder_mead(f,list(p0),iters=4000)
    for _ in range(3): q,v=t.nelder_mead(f,list(np.abs(q)),step=0.05,iters=4000)
    if best is None or v<best[1]: best=(np.abs(q),v)
print('gap', n, ', '.join('%.6f'%v for v in best[0]), 'minimax', round(best[1],4))
