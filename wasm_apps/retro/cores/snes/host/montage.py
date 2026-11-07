import sys, glob
files = sys.argv[3:]
cols = int(sys.argv[2]); out = sys.argv[1]
imgs=[]
for f in files:
    d=open(f,'rb').read().split(b'\n',3)
    imgs.append(d[3])
W,H=256,224
rows=(len(imgs)+cols-1)//cols
buf=bytearray(W*cols*H*rows*3)
for i,im in enumerate(imgs):
    cx,cy=(i%cols)*W,(i//cols)*H
    for y in range(H):
        o=((cy+y)*W*cols+cx)*3
        buf[o:o+W*3]=im[y*W*3:(y+1)*W*3]
open(out,'wb').write(b'P6\n%d %d\n255\n'%(W*cols,H*rows)+bytes(buf))
