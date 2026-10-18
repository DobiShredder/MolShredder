#!/usr/bin/env python3
"""Regenerates the synthetic map fixtures in this directory (SEQ-319 S2).

Run from this directory; the output is deterministic. The files are original
synthetic data (an analytic density function), not derived from any
third-party file, and their expected reading was cross-checked with
Open-Source PyMOL 3.1.0.
"""
import os
import struct, math, random
random.seed(3)
def val(i,j,k): return round(math.sin(0.7*i)+0.5*math.cos(0.4*j)+0.1*k+0.3*i*j/10,4)

# ---- XPLOR (orthogonal and triclinic)
def xplor(path, cell, div, mn, mx):
    na=[mx[i]-mn[i]+1 for i in range(3)]
    L=["","       2 !NTITLE"," REMARKS synthetic map"," REMARKS second line"]
    L.append("".join("%8d"%v for v in (div[0],mn[0],mx[0],div[1],mn[1],mx[1],div[2],mn[2],mx[2])))
    L.append("".join("%12.5E"%v for v in cell))
    L.append("ZYX")
    for k in range(na[2]):
        L.append("%8d"%k)
        vals=[val(i,j,k) for j in range(na[1]) for i in range(na[0])]
        for s in range(0,len(vals),6): L.append("".join("%12.5E"%v for v in vals[s:s+6]))
    open(path,"w").write("\n".join(L)+"\n")
xplor("ortho.xplor",(20.0,24.0,28.0,90.0,90.0,90.0),(10,12,14),(-1,0,2),(4,5,8))
xplor("tri.xplor",(20.0,24.0,28.0,80.0,85.0,100.0),(10,12,14),(-1,0,2),(4,5,8))

# ---- BRIX ASCII + DSN6 binary
def bricks(size, prod, plus):
    bx,by,bz=[(s-1)//8+1 for s in size]
    out=bytearray()
    for cz in range(bz):
        for cy in range(by):
            for cx in range(bx):
                for c in range(8):
                    for b in range(8):
                        for a in range(8):
                            x,y,z=cx*8+a,cy*8+b,cz*8+c
                            v=int(max(0,min(255,round(100+60*math.sin(0.5*x)+30*math.cos(0.3*y)+5*z)))) if x<size[0] and y<size[1] and z<size[2] else 0
                            out.append(v)
    return bytes(out)
size=(11,10,9); mn=(-2,3,1)
body=bricks(size,1.0,0.0)
hdr=(":-)  origin %d %d %d  extent %d %d %d  grid %d %d %d  cell %.3f %.3f %.3f %.2f %.2f %.2f  prod %.5f  plus %.5f  sigma %.5f\n"%(mn+size+(20,30,24)+(40.0,60.0,48.0,90.0,90.0,90.0)+(0.5,10.0,3.0))).encode()
open("ascii.brix","wb").write(hdr.ljust(512,b" ")+body)
# DSN6 binary: header 256 shorts (big-endian as O writes), check word index 18 = 100
words=[0]*256
vals=list(mn)+list(size)+[20,30,24]+[int(40*10),int(60*10),int(48*10),int(90*10),int(90*10),int(90*10)]+[int(0.5*100),10,10]
for i,v in enumerate(vals): words[i]=v
words[18]=100
hdrbin=struct.pack(">256h",*words)
# bricks in DSN6 are byte-pair swapped on disk
sw=bytearray(body)
for i in range(0,len(sw)-1,2): sw[i],sw[i+1]=sw[i+1],sw[i]
open("binary.dsn6","wb").write(hdrbin+bytes(sw))

# ---- PHI (Fortran unformatted, little endian)
def rec(b): return struct.pack("<i",len(b))+b+struct.pack("<i",len(b))
dim=5
data=b"".join(struct.pack("<f",val(i,j,k)) for k in range(dim) for j in range(dim) for i in range(dim))
phi=rec(b"now starting phimap  ")[:0]
phi=rec(b"uplbl".ljust(20))+rec(("potential".ljust(10)+"synthetic phi map".ljust(60)).encode())+rec(data)+rec(b"botlbl".ljust(16))+rec(struct.pack("<ffff",2.0,1.5,-2.0,3.25))
open("little.phi","wb").write(phi)

# ---- GRD ASCII
def grd(path, cell, brick, ranges, fast=1):
    L=["GRD title","(1p,e12.5)"]
    L.append(" ".join("%g"%v for v in cell))
    L.append(" ".join(str(v) for v in brick))
    L.append("%d "%fast+" ".join(str(v) for v in ranges))
    nx,ny,nz=[b+1 for b in brick]
    for k in range(nz):
        for j in range(ny):
            for i in range(nx): L.append("%12.5E"%val(i,j,k))
    open(path,"w").write("\n".join(L)+"\n")
grd("ortho.grd",(20.0,24.0,28.0,90.0,90.0,90.0),(5,4,3),(-2,8,0,12,2,14))

# ---- FLD (AVS) little-endian floats
nx,ny,nz=5,4,3
hdr="# AVS field file\nndim=3\ndim1=%d\ndim2=%d\ndim3=%d\nnspace=3\nfield=uniform\nveclen=1\ndata=float\nmin_ext=-1.5 2.0 0.5\nmax_ext=2.5 5.0 3.5\n\x0c\x0c"%(nx,ny,nz)
open("le.fld","wb").write(hdr.encode("latin1")+b"".join(struct.pack("<f",val(i,j,k)) for k in range(nz) for j in range(ny) for i in range(nx)))

# ---- ACNT
nx,ny,nz=4,3,5
L=["ACNT header",
   "%g %g %d"%(2.0,0.5,ny),   # y first
   "%g %g %d"%(-1.0,0.25,nx), # x
   "%g %g %d"%(3.0,1.0,nz),   # z
   "90 90 90"]
for c in range(nz):
    for a in range(nx):
        for b in range(ny): L.append("%g"%val(a,b,c))
open("t.acnt","w").write("\n".join(L)+"\n")
