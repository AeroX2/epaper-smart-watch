"""Simulated-annealing PCB placer (first-cut engine).
Cost = w_hpwl*HPWL(signal nets) + w_ov*overlap_area + w_bnd*out_of_bounds + w_kp*RF_keepout
pcbnew only for I/O; SA runs on plain Python arrays for speed.
"""
import pcbnew, math, random
random.seed(3)
P = r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
b = pcbnew.LoadBoard(P)
NM = 1e6                     # nm per mm
tomm = lambda v: v/NM
box = b.GetBoardEdgesBoundingBox()
BX0, BY0 = tomm(box.GetX()), tomm(box.GetY())
BX1, BY1 = tomm(box.GetRight()), tomm(box.GetBottom())
MARGIN = 0.6
bx0,by0,bx1,by1 = BX0+MARGIN, BY0+MARGIN, BX1-MARGIN, BY1-MARGIN

POWER = {"GND","+3V0","+5V","/VSYS",""}
CLR = 0.3                    # part-to-part clearance (mm)
RF_REF = "U3"               # antenna keepout around the module
RF_KEEPOUT = 4.0            # mm radius to discourage parts near U3

# ---- extract geometry ----
parts=[]      # dict per footprint
fixed_idx=[]; free_idx=[]
for fp in b.GetFootprints():
    pads=list(fp.Pads())
    if not pads: continue
    pos=fp.GetPosition(); ax,ay=tomm(pos.x),tomm(pos.y)
    ori=fp.GetOrientationDegrees()
    th=math.radians(ori)
    ct,st=math.cos(-th),math.sin(-th)   # un-rotate to local frame
    padlist=[]
    xs=[];ys=[]
    for pad in pads:
        pp=pad.GetPosition(); gx,gy=tomm(pp.x),tomm(pp.y)
        dx,dy=gx-ax,gy-ay
        lx=dx*ct-dy*st; ly=dx*st+dy*ct      # local (unrotated) offset
        padlist.append((lx,ly,pad.GetNetname()))
        xs.append(lx);ys.append(ly)
    # local pad bbox half-extents (+ small body pad)
    hw=(max(xs)-min(xs))/2+0.25; hh=(max(ys)-min(ys))/2+0.25
    parts.append(dict(fp=fp,x=ax,y=ay,ori=int(round(ori/90))%4*90,
                      pads=padlist,hw=hw,hh=hh,locked=fp.IsLocked(),
                      ref=fp.GetReference()))
for i,p in enumerate(parts):
    (fixed_idx if p['locked'] else free_idx).append(i)

def rot(lx,ly,ori):
    if ori==0:  return lx,ly
    if ori==90: return -ly,lx
    if ori==180:return -lx,-ly
    return ly,-lx
def ext(p):
    return (p['hw'],p['hh']) if p['ori']%180==0 else (p['hh'],p['hw'])
def padpos(p):
    out=[]
    for lx,ly,net in p['pads']:
        rx,ry=rot(lx,ly,p['ori']); out.append((p['x']+rx,p['y']+ry,net))
    return out

# ---- nets (signal only) ----
net2pads={}   # net -> list of (part_idx, lx, ly)
for i,p in enumerate(parts):
    for lx,ly,net in p['pads']:
        if net in POWER: continue
        net2pads.setdefault(net,[]).append((i,lx,ly))
net2pads={k:v for k,v in net2pads.items() if 2<=len(v)<=8}
part2nets={}
for net,lst in net2pads.items():
    for (i,_,_) in lst: part2nets.setdefault(i,set()).add(net)

rf_center=None
for i in fixed_idx:
    if parts[i]['ref']==RF_REF: rf_center=(parts[i]['x'],parts[i]['y'])

# ---- cost terms ----
def net_hpwl(net):
    xs=[];ys=[]
    for (i,lx,ly) in net2pads[net]:
        rx,ry=rot(lx,ly,parts[i]['ori']); xs.append(parts[i]['x']+rx); ys.append(parts[i]['y']+ry)
    return (max(xs)-min(xs))+(max(ys)-min(ys))
def pair_overlap(i,j):
    a,bb=parts[i],parts[j]; aw,ah=ext(a); bw,bh=ext(bb)
    ox=(aw+bw+CLR)-abs(a['x']-bb['x']); oy=(ah+bh+CLR)-abs(a['y']-bb['y'])
    return ox*oy if (ox>0 and oy>0) else 0.0
def part_overlap(i):
    s=0.0
    for j in range(len(parts)):
        if j!=i: s+=pair_overlap(i,j)
    return s
def part_bounds(i):
    p=parts[i];w,h=ext(p)
    dx=max(0,(bx0)-(p['x']-w))+max(0,(p['x']+w)-bx1)
    dy=max(0,(by0)-(p['y']-h))+max(0,(p['y']+h)-by1)
    return dx*dx+dy*dy
def part_keepout(i):
    if rf_center is None: return 0.0
    p=parts[i]; d=math.hypot(p['x']-rf_center[0],p['y']-rf_center[1])
    return max(0.0,RF_KEEPOUT-d)**2

W_HPWL=1.0; W_OV=120.0; W_BND=200.0; W_KP=3.0
def total_cost():
    h=sum(net_hpwl(n) for n in net2pads)
    ov=0.0
    for a in range(len(parts)):
        for c in range(a+1,len(parts)):
            if parts[a]['locked'] and parts[c]['locked']: continue
            ov+=pair_overlap(a,c)
    bn=sum(part_bounds(i) for i in free_idx); kp=sum(part_keepout(i) for i in free_idx)
    return W_HPWL*h+W_OV*ov+W_BND*bn+W_KP*kp
def local_cost(i):
    h=sum(net_hpwl(n) for n in part2nets.get(i,()))
    return W_HPWL*h+W_OV*part_overlap(i)+W_BND*part_bounds(i)+W_KP*part_keepout(i)

# ---- init: scatter free parts across the board ----
for i in free_idx:
    parts[i]['x']=random.uniform(bx0+2,bx1-2); parts[i]['y']=random.uniform(by0+2,by1-2)

def metrics():
    h=sum(net_hpwl(n) for n in net2pads)
    ovp=0
    for a in free_idx:
        for c in range(len(parts)):
            if c<=a and not parts[c]['locked']: continue
            if c==a: continue
            if pair_overlap(a,c)>1e-6: ovp+=1
    return round(h,1),ovp
h0,ov0=metrics()

# ---- simulated annealing ----
T=8.0; Tmin=0.02; alpha=0.9995; moves_per_T=len(free_idx)
import time; t0=time.time()
steps=0
while T>Tmin:
    for _ in range(moves_per_T):
        i=random.choice(free_idx); p=parts[i]
        old=(p['x'],p['y'],p['ori']); c_old=local_cost(i)
        r=random.random()
        if r<0.78:
            rng=max(0.4,(bx1-bx0)*0.5*(T/8.0))
            p['x']=min(max(p['x']+random.uniform(-rng,rng),bx0),bx1)
            p['y']=min(max(p['y']+random.uniform(-rng,rng),by0),by1)
        elif r<0.9:
            p['ori']=random.choice([0,90,180,270])
        else:
            j=random.choice(free_idx)
            if j!=i:
                parts[i]['x'],parts[j]['x']=parts[j]['x'],parts[i]['x']
                parts[i]['y'],parts[j]['y']=parts[j]['y'],parts[i]['y']
        c_new=local_cost(i)
        d=c_new-c_old
        if d>0 and random.random()>math.exp(-d/T):
            p['x'],p['y'],p['ori']=old      # reject (swap reverts via x/y restore of i; j unchanged net effect handled by cost)
        steps+=1
    T*=alpha
# ---- legalize: greedy de-overlap (free parts only) ----
for _ in range(4000):
    moved=False
    for i in free_idx:
        for j in range(len(parts)):
            if j==i: continue
            o=pair_overlap(i,j)
            if o>1e-6:
                a,bb=parts[i],parts[j]; aw,ah=ext(a);bw,bh=ext(bb)
                ox=(aw+bw+CLR)-abs(a['x']-bb['x']); oy=(ah+bh+CLR)-abs(a['y']-bb['y'])
                if ox<oy:
                    s=ox if a['x']>=bb['x'] else -ox
                    a['x']=min(max(a['x']+(s*0.5 if not bb['locked'] else s),bx0),bx1)
                    if not bb['locked']: bb['x']=min(max(bb['x']-s*0.5,bx0),bx1)
                else:
                    s=oy if a['y']>=bb['y'] else -oy
                    a['y']=min(max(a['y']+(s*0.5 if not bb['locked'] else s),by0),by1)
                    if not bb['locked']: bb['y']=min(max(bb['y']-s*0.5,by0),by1)
                moved=True
    if not moved: break
h1,ov1=metrics()

# ---- write back ----
for i in free_idx:
    p=parts[i]
    p['fp'].SetPosition(pcbnew.VECTOR2I(int(p['x']*NM),int(p['y']*NM)))
    p['fp'].SetOrientationDegrees(p['ori'])
b.Save(P)
print(f"SA placer: {len(free_idx)} free, {len(fixed_idx)} locked")
print(f"  HPWL  : {h0}  ->  {h1} mm   ({round(100*(h0-h1)/h0)}% shorter)")
print(f"  overlaps: {ov0} -> {ov1} pairs")
print(f"  runtime: {round(time.time()-t0,1)}s, {steps} SA moves")
