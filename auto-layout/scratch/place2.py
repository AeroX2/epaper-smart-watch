"""Two-stage PCB placer: global clustering (HPWL) -> legalization (de-overlap).
Stage A: force-directed pull of each free part toward the centroid of the nets it shares
         (locked parts are fixed anchors). Minimizes wirelength, allows overlap.
Stage B: simultaneous damped overlap-removal, preserving clusters; + RF keepout + bounds.
"""
import pcbnew, math, random, time
random.seed(5)
P = r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
b = pcbnew.LoadBoard(P); NM=1e6; tomm=lambda v:v/NM
box=b.GetBoardEdgesBoundingBox()
bx0,by0=tomm(box.GetX())+0.6,tomm(box.GetY())+0.6
bx1,by1=tomm(box.GetRight())-0.6,tomm(box.GetBottom())-0.6
cx,cy=(bx0+bx1)/2,(by0+by1)/2
POWER={"GND","+3V0","+5V","/VSYS",""}; CLR=0.3; RF_REF="U3"; RF_KP=4.5

parts=[]
for fp in b.GetFootprints():
    pads=list(fp.Pads())
    if not pads: continue
    pos=fp.GetPosition(); ax,ay=tomm(pos.x),tomm(pos.y)
    th=math.radians(-fp.GetOrientationDegrees()); ct,st=math.cos(th),math.sin(th)
    pl=[]; xs=[]; ys=[]
    for pad in pads:
        pp=pad.GetPosition(); dx,dy=tomm(pp.x)-ax,tomm(pp.y)-ay
        lx=dx*ct-dy*st; ly=dx*st+dy*ct; pl.append((lx,ly,pad.GetNetname())); xs.append(lx); ys.append(ly)
    parts.append(dict(fp=fp,x=ax,y=ay,ori=0,pads=pl,
                      hw=(max(xs)-min(xs))/2+0.22,hh=(max(ys)-min(ys))/2+0.22,
                      locked=fp.IsLocked(),ref=fp.GetReference()))
free=[i for i,p in enumerate(parts) if not p['locked']]
ext=lambda p:(p['hw'],p['hh'])
def padpos(p):
    return [(p['x']+lx,p['y']+ly,net) for lx,ly,net in p['pads']]
net2=({})
for i,p in enumerate(parts):
    for lx,ly,net in p['pads']:
        if net in POWER: continue
        net2.setdefault(net,[]).append(i)
net2={k:v for k,v in net2.items() if 2<=len(v)<=8}
part2net={}
for net,l in net2.items():
    for i in l: part2net.setdefault(i,[]).append(net)
rf=next((( parts[i]['x'],parts[i]['y']) for i in range(len(parts)) if parts[i]['ref']==RF_REF and parts[i]['locked']),None)

def net_centroid(net):
    pts=[]
    for i in net2[net]:
        for lx,ly,nn in parts[i]['pads']:
            if nn==net: pts.append((parts[i]['x']+lx,parts[i]['y']+ly))
    return (sum(p[0] for p in pts)/len(pts), sum(p[1] for p in pts)/len(pts))
def hpwl():
    t=0
    for net,l in net2.items():
        xs=[];ys=[]
        for i in l:
            for lx,ly,nn in parts[i]['pads']:
                if nn==net: xs.append(parts[i]['x']+lx);ys.append(parts[i]['y']+ly)
        t+=(max(xs)-min(xs))+(max(ys)-min(ys))
    return t
def overlaps():
    c=0
    for a in free:
        for j in range(len(parts)):
            if j==a: continue
            aw,ah=ext(parts[a]);bw,bh=ext(parts[j])
            if (aw+bw)-abs(parts[a]['x']-parts[j]['x'])>1e-6 and (ah+bh)-abs(parts[a]['y']-parts[j]['y'])>1e-6: c+=1
    return c//2

# init: cluster seed near board center
for i in free: parts[i]['x']=cx+random.uniform(-3,3); parts[i]['y']=cy+random.uniform(-3,3)
h_rand=hpwl()
t0=time.time()
# ---- Stage A: force-directed clustering ----
for it in range(500):
    step=0.18*(1-it/500)+0.03
    cents={net:net_centroid(net) for net in net2}
    for i in free:
        nets=part2net.get(i,())
        if not nets: continue
        tx=sum(cents[n][0] for n in nets)/len(nets); ty=sum(cents[n][1] for n in nets)/len(nets)
        parts[i]['x']+=(tx-parts[i]['x'])*step; parts[i]['y']+=(ty-parts[i]['y'])*step
        parts[i]['x']=min(max(parts[i]['x'],bx0),bx1); parts[i]['y']=min(max(parts[i]['y'],by0),by1)
h_clustered=hpwl()
# ---- Stage B: simultaneous damped legalization ----
for it in range(6000):
    disp=[[0.0,0.0] for _ in free]; idx={a:k for k,a in enumerate(free)}; maxd=0
    for k,a in enumerate(free):
        aw,ah=ext(parts[a])
        # RF keepout
        if rf:
            d=math.hypot(parts[a]['x']-rf[0],parts[a]['y']-rf[1])
            if d<RF_KP and d>1e-6:
                push=(RF_KP-d); disp[k][0]+=(parts[a]['x']-rf[0])/d*push*0.5; disp[k][1]+=(parts[a]['y']-rf[1])/d*push*0.5
        for j in range(len(parts)):
            if j==a: continue
            bw,bh=ext(parts[j]); dx=parts[a]['x']-parts[j]['x']; dy=parts[a]['y']-parts[j]['y']
            ox=(aw+bw+CLR)-abs(dx); oy=(ah+bh+CLR)-abs(dy)
            if ox>0 and oy>0:
                maxd=max(maxd,min(ox,oy))
                frac=1.0 if parts[j]['locked'] else 0.5
                if ox<oy:
                    s=ox if dx>=0 else -ox; disp[k][0]+=s*frac
                else:
                    s=oy if dy>=0 else -oy; disp[k][1]+=s*frac
    for k,a in enumerate(free):
        jx=random.uniform(-0.01,0.01) if maxd>0.1 else 0
        parts[a]['x']=min(max(parts[a]['x']+disp[k][0]*0.6+jx,bx0),bx1)
        parts[a]['y']=min(max(parts[a]['y']+disp[k][1]*0.6,by0),by1)
    if maxd<0.02: break

# ---- Stage C: spiral-search relocation for residual overlaps ----
def ov_any(a):
    aw,ah=ext(parts[a])
    for j in range(len(parts)):
        if j==a: continue
        bw,bh=ext(parts[j])
        if (aw+bw+CLR)-abs(parts[a]['x']-parts[j]['x'])>0 and (ah+bh+CLR)-abs(parts[a]['y']-parts[j]['y'])>0: return True
    return False
def find_spot(a):
    ox,oy=parts[a]['x'],parts[a]['y']
    for r in [i*0.4 for i in range(1,140)]:
        for ang in range(0,360,15):
            nx=ox+r*math.cos(math.radians(ang)); ny=oy+r*math.sin(math.radians(ang))
            if nx<bx0 or nx>bx1 or ny<by0 or ny>by1: continue
            parts[a]['x'],parts[a]['y']=nx,ny
            if not ov_any(a): return True
    parts[a]['x'],parts[a]['y']=ox,oy; return False
for _ in range(6):
    rem=[a for a in free if ov_any(a)]
    if not rem: break
    for a in rem: find_spot(a)
h_final=hpwl()
for i in free:
    parts[i]['fp'].SetPosition(pcbnew.VECTOR2I(int(parts[i]['x']*NM),int(parts[i]['y']*NM)))
b.Save(P)
print(f"free={len(free)}  HPWL: random {round(h_rand)} -> clustered {round(h_clustered)} -> legalized {round(h_final)} mm")
print(f"overlaps left: {overlaps()}   legalize iters: {it}   time {round(time.time()-t0,1)}s")
