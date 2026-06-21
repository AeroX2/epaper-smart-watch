import pcbnew, math, random
random.seed(7)
P = r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
b = pcbnew.LoadBoard(P)
FROMMM = pcbnew.FromMM; TOMM = pcbnew.ToMM
box = b.GetBoardEdgesBoundingBox()
M = FROMMM(1.0)
x0,y0,x1,y1 = box.GetX()+M, box.GetY()+M, box.GetRight()-M, box.GetBottom()-M
CLR = FROMMM(0.3)

POWER = {"GND","+3V0","+5V","/VSYS",""}
def is_power(n): return n in POWER

free, fixed = [], []
for fp in b.GetFootprints():
    if len(fp.Pads())==0: continue
    (fixed if fp.IsLocked() else free).append(fp)
allfp = free+fixed
freeset=set(id(f) for f in free)

def half(fp):
    bb=fp.GetBoundingBox(False,False); return bb.GetWidth()/2.0, bb.GetHeight()/2.0

# signal-net connectivity (skip power + huge nets)
netmap={}
for i,fp in enumerate(allfp):
    for pad in fp.Pads():
        nc=pad.GetNetname()
        if is_power(nc): continue
        netmap.setdefault(nc,[]).append(i)
netmap={k:v for k,v in netmap.items() if 2<=len(v)<=8}

# initial grid scatter
n=len(free); aspect=(x1-x0)/(y1-y0)
cols=max(1,int(math.ceil(math.sqrt(n*aspect)))); rows=int(math.ceil(n/cols))
gx=(x1-x0)/cols; gy=(y1-y0)/rows
for k,fp in enumerate(free):
    r,c=divmod(k,cols)
    fp.SetPosition(pcbnew.VECTOR2I(int(x0+gx*(c+0.5)),int(y0+gy*(r+0.5))))

def clamp(fp):
    hw,hh=half(fp);p=fp.GetPosition()
    fp.SetPosition(pcbnew.VECTOR2I(min(max(p.x,int(x0+hw)),int(x1-hw)),
                                   min(max(p.y,int(y0+hh)),int(y1-hh))))

def overlaps(extra=0):
    cnt=0
    for i,fp in enumerate(free):
        hw,hh=half(fp);p=fp.GetPosition()
        for other in allfp:
            if other is fp:continue
            ow,oh=half(other);q=other.GetPosition()
            if (hw+ow+extra)-abs(p.x-q.x)>0 and (hh+oh+extra)-abs(p.y-q.y)>0:cnt+=1
    return cnt//2

# Phase 1: cluster by connectivity
for it in range(150):
    pos={i:allfp[i].GetPosition() for i in range(len(allfp))}
    for net,idxs in netmap.items():
        cx=sum(pos[i].x for i in idxs)/len(idxs); cy=sum(pos[i].y for i in idxs)/len(idxs)
        for i in idxs:
            if id(allfp[i]) not in freeset:continue
            p=allfp[i].GetPosition()
            allfp[i].SetPosition(pcbnew.VECTOR2I(int(p.x+(cx-p.x)*0.10),int(p.y+(cy-p.y)*0.10)))
    for fp in free: clamp(fp)

# Phase 2: de-overlap only, until clean
for it in range(6000):
    moved=0
    for fp in free:
        hw,hh=half(fp);p=fp.GetPosition()
        for other in allfp:
            if other is fp:continue
            ow,oh=half(other);q=other.GetPosition()
            dx=p.x-q.x; dy=p.y-q.y
            ox=(hw+ow+CLR)-abs(dx); oy=(hh+oh+CLR)-abs(dy)
            if ox>0 and oy>0:
                moved+=1
                if dx==0:dx=random.randint(-1000,1000)
                if dy==0:dy=random.randint(-1000,1000)
                if ox<oy:
                    s=ox if dx>0 else -ox
                    if id(other) in freeset:
                        fp.SetPosition(pcbnew.VECTOR2I(int(p.x+s*0.5),p.y));other.SetPosition(pcbnew.VECTOR2I(int(q.x-s*0.5),q.y))
                    else: fp.SetPosition(pcbnew.VECTOR2I(int(p.x+s),p.y))
                else:
                    s=oy if dy>0 else -oy
                    if id(other) in freeset:
                        fp.SetPosition(pcbnew.VECTOR2I(p.x,int(p.y+s*0.5)));other.SetPosition(pcbnew.VECTOR2I(q.x,int(q.y-s*0.5)))
                    else: fp.SetPosition(pcbnew.VECTOR2I(p.x,int(p.y+s)))
                p=fp.GetPosition()
        clamp(fp)
    if moved==0: break

b.Save(P)
print(f"placed {len(free)} free / {len(fixed)} fixed; phase2 stop iter≈{it}; true overlaps left: {overlaps(0)}")
