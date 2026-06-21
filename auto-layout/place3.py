"""Polygon-aware two-stage placer: cluster (HPWL) -> legalize, all bounded by the
REAL board outline (point-in-polygon), not the bounding box. + RF keepout."""
import pcbnew, math, random, time
random.seed(5)
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
b=pcbnew.LoadBoard(P); NM=1e6; tomm=lambda v:v/NM
POWER={"GND","+3V0","+5V","/VSYS",""}; CLR=0.12; RF_REF="U3"; RF_KP=4.0
EDGE_MARGIN=0.5      # keep whole courtyard this far inside the board edge
EXT_MARGIN=0.12     # safety added to every courtyard half-extent (over-clear)
R_REP=9.0; K_REP=0.3    # repulsion radius / strength -> spread parts to fill the board

# ---- board outline polygon, built from Edge.Cuts shape endpoints (outline isn't a closed
#      contour, so GetBoardPolygonOutlines fails) — angle-sorted + inset 1mm ----
raw=[]
for d in b.GetDrawings():
    if d.GetLayerName()!="Edge.Cuts": continue
    raw.append((tomm(d.GetStart().x),tomm(d.GetStart().y)))
    raw.append((tomm(d.GetEnd().x),tomm(d.GetEnd().y)))
    try:
        if d.GetShape()==pcbnew.SHAPE_T_ARC:
            m=d.GetArcMid(); raw.append((tomm(m.x),tomm(m.y)))
    except Exception: pass
uniq=[]
for p in raw:
    if all(abs(p[0]-q[0])>0.05 or abs(p[1]-q[1])>0.05 for q in uniq): uniq.append(p)
cxp=sum(p[0] for p in uniq)/len(uniq); cyp=sum(p[1] for p in uniq)/len(uniq)
uniq.sort(key=lambda p: math.atan2(p[1]-cyp,p[0]-cxp))
poly=list(uniq)   # actual board edge; per-part EDGE_MARGIN handles the keep-back distance
def pip(x,y):
    inside=False; n=len(poly); j=n-1
    for i in range(n):
        xi,yi=poly[i]; xj,yj=poly[j]
        if ((yi>y)!=(yj>y)) and (x < (xj-xi)*(y-yi)/(yj-yi+1e-12)+xi): inside=not inside
        j=i
    return inside
def inside_full(p):
    # whole courtyard (+EDGE_MARGIN) must sit inside the board outline, not just the centre
    hw,hh=ext(p); X,Y=p['x'],p['y']; m=EDGE_MARGIN
    return all(pip(X+sx*(hw+m),Y+sy*(hh+m)) for sx in (-1,1) for sy in (-1,1))
def clamp(p):
    if inside_full(p): return
    for _ in range(500):
        p['x']+=(cxp-p['x'])*0.05; p['y']+=(cyp-p['y'])*0.05
        if inside_full(p): return

# ---- extract ----
parts=[]
for fp in b.GetFootprints():
    pads=list(fp.Pads())
    if not pads: continue
    pos=fp.GetPosition(); ax,ay=tomm(pos.x),tomm(pos.y)
    th=math.radians(-fp.GetOrientationDegrees()); ct,st=math.cos(th),math.sin(th)
    pl=[]
    for pad in pads:
        pp=pad.GetPosition(); dx,dy=tomm(pp.x)-ax,tomm(pp.y)-ay
        lx=dx*ct-dy*st; ly=dx*st+dy*ct; pl.append((lx,ly,pad.GetNetname()))
    # half-extents from the COURTYARD (proper keep-out), not pads
    try: cy=fp.GetCourtyard(pcbnew.F_CrtYd); ncy=cy.OutlineCount()
    except Exception: ncy=0
    if ncy>=1:
        ol=cy.Outline(0); cxs=[tomm(ol.CPoint(k).x) for k in range(ol.PointCount())]
        cys=[tomm(ol.CPoint(k).y) for k in range(ol.PointCount())]
        hw=(max(cxs)-min(cxs))/2; hh=(max(cys)-min(cys))/2
    else:
        pxs=[];pys=[]
        for pad in pads:
            pb=pad.GetBoundingBox(); pxs+=[pb.GetX(),pb.GetRight()]; pys+=[pb.GetY(),pb.GetBottom()]
        hw=tomm(max(pxs)-min(pxs))/2+0.3; hh=tomm(max(pys)-min(pys))/2+0.3
    th=any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH,pcbnew.PAD_ATTRIB_NPTH) for p in pads)
    sides={'T','B'} if th else ({'B'} if fp.IsFlipped() else {'T'})  # through-hole occupies both faces
    parts.append(dict(fp=fp,x=ax,y=ay,pads=pl,hw=hw,hh=hh,sides=sides,
                      locked=fp.IsLocked(),ref=fp.GetReference()))
free=[i for i,p in enumerate(parts) if not p['locked']]
ext=lambda p:(p['hw']+EXT_MARGIN,p['hh']+EXT_MARGIN)   # safety pad so real courtyards never touch
net2={}
for i,p in enumerate(parts):
    for lx,ly,net in p['pads']:
        if net in POWER: continue
        net2.setdefault(net,[]).append(i)
net2={k:v for k,v in net2.items() if 2<=len(v)<=8}
part2net={}
for net,l in net2.items():
    for i in l: part2net.setdefault(i,[]).append(net)
rf=next(((parts[i]['x'],parts[i]['y']) for i in range(len(parts)) if parts[i]['ref']==RF_REF and parts[i]['locked']),None)

# ---- schematic symbol positions: parts drawn near each other (e.g. a decoupling cap next
#      to its IC) should sit near each other on the PCB, even when the netlist can't say so
#      (both cap pins are on big power/GND nets that we exclude from clustering). ----
SCH=r"C:\Users\James\Documents\git\epaper-smart-watch\pcb\epaper-smart-watch.kicad_sch"
def _tok(s):
    o=[];i=0;n=len(s)
    while i<n:
        c=s[i]
        if c.isspace():i+=1;continue
        if c in '()':o.append(c);i+=1;continue
        if c=='"':
            j=i+1
            while j<n and s[j]!='"':
                if s[j]=='\\':j+=1
                j+=1
            o.append(s[i:j+1]);i=j+1;continue
        j=i
        while j<n and (not s[j].isspace()) and s[j] not in '()"':j+=1
        o.append(s[i:j]);i=j
    return o
def _parse(tk):
    p=[0]
    def rd():
        t=tk[p[0]];p[0]+=1
        if t=='(':
            l=[]
            while tk[p[0]]!=')':l.append(rd())
            p[0]+=1;return l
        return t
    return rd()
schpos={}
try:
    tr=_parse(_tok(open(SCH,encoding='utf-8').read()))
    for el in tr:
        if isinstance(el,list) and el and el[0]=='symbol':
            at=ref=None
            for c in el:
                if isinstance(c,list) and c and c[0]=='at': at=(float(c[1]),float(c[2]))
                if isinstance(c,list) and len(c)>=3 and c[0]=='property' and c[1].strip('"')=='Reference': ref=c[2].strip('"')
            if at and ref: schpos[ref]=at
except Exception as e:
    print("sch parse failed:",e)
for p in parts: p['sx'],p['sy']=schpos.get(p['ref'],(None,None))
SCH_R=22.0; SCH_K=9.0; SCH_D0=2.0     # schematic neighbourhood; closer = stronger pull
buddies={}
for i in range(len(parts)):
    if parts[i]['sx'] is None: continue
    for j in range(len(parts)):
        if j==i or parts[j]['sx'] is None: continue
        d=math.hypot(parts[i]['sx']-parts[j]['sx'],parts[i]['sy']-parts[j]['sy'])
        if d<SCH_R: buddies.setdefault(i,[]).append((j,SCH_K/(d+SCH_D0)))
print("schematic positions:",len(schpos),"refs; buddy lists:",len(buddies))
def net_centroid(net):
    pts=[]
    for i in net2[net]:
        for lx,ly,nn in parts[i]['pads']:
            if nn==net: pts.append((parts[i]['x']+lx,parts[i]['y']+ly))
    return (sum(q[0] for q in pts)/len(pts),sum(q[1] for q in pts)/len(pts))
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
        sa=parts[a]['sides']
        for j in range(len(parts)):
            if j==a or not (sa & parts[j]['sides']):continue
            aw,ah=ext(parts[a]);bw,bh=ext(parts[j])
            if (aw+bw)-abs(parts[a]['x']-parts[j]['x'])>1e-6 and (ah+bh)-abs(parts[a]['y']-parts[j]['y'])>1e-6:c+=1
    return c//2

for i in free: parts[i]['x']=cxp+random.uniform(-3,3); parts[i]['y']=cyp+random.uniform(-3,3)
h_rand=hpwl(); t0=time.time()
# Stage A: cluster
NET_W=1.5   # weight per shared signal net, vs schematic-proximity buddy weights
for it in range(450):
    step=0.18*(1-it/450)+0.03; cents={n:net_centroid(n) for n in net2}
    for i in free:
        tx=ty=wsum=0.0
        for n in part2net.get(i,()):              # netlist connectivity
            cx,cy=cents[n]; tx+=cx*NET_W; ty+=cy*NET_W; wsum+=NET_W
        for j,w in buddies.get(i,()):              # schematic proximity (incl. locked anchors)
            tx+=parts[j]['x']*w; ty+=parts[j]['y']*w; wsum+=w
        if wsum>0:
            tx/=wsum; ty/=wsum
            parts[i]['x']+=(tx-parts[i]['x'])*step; parts[i]['y']+=(ty-parts[i]['y'])*step
        # repulsion: push apart from nearby same-side parts so the layout spreads to fill the
        # board (routing room) instead of collapsing onto one tight blob
        rx=ry=0.0; xi=parts[i]['x']; yi=parts[i]['y']; si=parts[i]['sides']
        for j in range(len(parts)):
            if j==i or not (si & parts[j]['sides']): continue
            dx=xi-parts[j]['x']; dy=yi-parts[j]['y']; d2=dx*dx+dy*dy
            if 1e-4<d2<R_REP*R_REP:
                d=math.sqrt(d2); f=K_REP*(1-d/R_REP)
                rx+=dx/d*f; ry+=dy/d*f
        parts[i]['x']+=rx*step; parts[i]['y']+=ry*step
        clamp(parts[i])
h_clu=hpwl()
# Stage B: deterministic incremental legalization (courtyard extents).
# Place each free part (largest first) at the nearest spot clearing all already-placed
# courtyards + CLR, inside the outline, outside the RF keepout. Zero overlaps by construction.
def clears(a,others):
    aw,ah=ext(parts[a]); X,Y=parts[a]['x'],parts[a]['y']; sa=parts[a]['sides']
    if not inside_full(parts[a]): return False          # whole body + margin inside the edge
    if rf and math.hypot(X-rf[0],Y-rf[1])<RF_KP: return False
    for j in others:
        if not (sa & parts[j]['sides']): continue          # opposite faces -> can't collide
        bw,bh=ext(parts[j])
        if (aw+bw+CLR)-abs(X-parts[j]['x'])>0 and (ah+bh+CLR)-abs(Y-parts[j]['y'])>0: return False
    return True
placed=[i for i in range(len(parts)) if parts[i]['locked']]
order=sorted(free,key=lambda i:-(parts[i]['hw']*parts[i]['hh']))   # big first
pbx0=min(p[0] for p in poly); pbx1=max(p[0] for p in poly); pby0=min(p[1] for p in poly); pby1=max(p[1] for p in poly)
unplaced=0
for a in order:
    x0,y0=parts[a]['x'],parts[a]['y']                      # clustered position (near its IC via sch weighting)
    if clears(a,placed): placed.append(a); continue
    done=False
    for r in [k*0.3 for k in range(1,340)]:                # nearest clear spot to the clustered position
        for ang in range(0,360,12):
            parts[a]['x']=x0+r*math.cos(math.radians(ang)); parts[a]['y']=y0+r*math.sin(math.radians(ang))
            if clears(a,placed): done=True; break
        if done: break
    if not done:                                           # full-board grid fallback
        gy=pby0
        while gy<=pby1 and not done:
            gx=pbx0
            while gx<=pbx1:
                parts[a]['x']=gx; parts[a]['y']=gy
                if clears(a,placed): done=True; break
                gx+=0.4
            gy+=0.4
    if not done: parts[a]['x'],parts[a]['y']=x0,y0; unplaced+=1
    placed.append(a)
def ov_any(a):
    aw,ah=ext(parts[a]); sa=parts[a]['sides']
    for j in range(len(parts)):
        if j==a or not (sa & parts[j]['sides']):continue
        bw,bh=ext(parts[j])
        if (aw+bw)-abs(parts[a]['x']-parts[j]['x'])>1e-6 and (ah+bh)-abs(parts[a]['y']-parts[j]['y'])>1e-6:return True
    return False
h_fin=hpwl()
print("unplaced (no clear spot found):",unplaced)
outside=sum(1 for i in free if not pip(parts[i]['x'],parts[i]['y']))
for i in free: parts[i]['fp'].SetPosition(pcbnew.VECTOR2I(int(parts[i]['x']*NM),int(parts[i]['y']*NM)))
b.Save(P)
print(f"free={len(free)} HPWL: rand {round(h_rand)} -> clustered {round(h_clu)} -> final {round(h_fin)} mm")
print(f"overlaps: {overlaps()}   parts outside outline: {outside}   time {round(time.time()-t0,1)}s")
