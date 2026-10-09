import numpy as np, sys
from scipy.linalg import expm
def Ht(N):
    nc=N*N; d=1<<nc; H=np.zeros(d)
    for k in range(d):
        c=[(i//N,i%N) for i in range(nc) if k>>i&1]
        e=sum(1 for x in range(len(c)) for y in range(x) if c[x][0]==c[y][0] or c[x][1]==c[y][1] or abs(c[x][0]-c[y][0])==abs(c[x][1]-c[y][1]))
        H[k]=e-0.5*len(c)
    return H
def run(N,T,steps):
    nc=N*N; d=1<<nc; H=Ht(N); g=np.where(H==H.min())[0]
    phi=np.full(d,1/np.sqrt(d),complex); phi*= np.array([(-1)**bin(k).count('1') for k in range(d)])
    dt=T/steps; mx=0
    for j in range(steps):
        s=(j+.5)/steps; a=s;b=1-s; th=dt*b
        ph=np.exp(-1j*dt/2*a*H)
        phi=ph*phi
        v=phi.reshape([2]*nc)
        for ax in range(nc):
            f=np.flip(v,ax); v=np.cos(th)*v-1j*np.sin(th)*f
        phi=ph*v.reshape(-1)
        mx=max(mx,abs(np.linalg.norm(phi)-1))
    p=abs(phi)**2
    E=np.real(np.vdot(phi,H*phi)) # at T H=Ht
    return len(g),H.min(),p[g].sum(),E,mx
N,T,steps=int(sys.argv[1]),float(sys.argv[2]),int(sys.argv[3])
print(N,T,steps,run(N,T,steps))
