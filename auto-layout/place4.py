"""Hierarchical CLUSTER placer.

Idea (what the user asked for): build LOGICAL clusters. Each IC / module / key connector
is an "anchor"; every passive (R, C, L, D, Q, ...) is assigned to the anchor it belongs to
(by SCHEMATIC proximity first -> netlist shared-nets -> nearest on PCB). Each cluster's
members are then packed tightly AROUND their anchor, and whole clusters are dropped on the
board one at a time, following connectivity, so the board reads as neat functional blocks.

Properties carried over from place3: courtyard extents, top/bottom handled separately,
EDGE_MARGIN keep-back from the outline, EXT_MARGIN safety pad, RF keepout. Placement is
incremental against everything already placed, so the result has 0 overlaps by construction.
"""
import pcbnew, math, time
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
b=pcbnew.LoadBoard(P); NM=1e6; tomm=lambda v:v/NM
POWER={"GND","+3V0","+5V","/VSYS",""}; CLR=0.12; RF_REF="U3"; RF_KP=4.0
EDGE_MARGIN=0.5      # whole courtyard stays this far inside the board edge
EXT_MARGIN=0.12     # safety added to every courtyard half-extent
SCH=r"C:\Users\James\Documents\git\epaper-smart-watch\pcb\epaper-smart-watch.kicad_sch"

# ---- board outline polygon (Edge.Cuts endpoints, angle-sorted; outline isn't closed) ----
raw=[]
for d in b.GetDrawings():
    if d.GetLayerName()!="Edge.Cuts": continue
    raw+=[(tomm(d.GetStart().x),tomm(d.GetStart().y)),(tomm(d.GetEnd().x),tomm(d.GetEnd().y))]
    try:
        if d.GetShape()==pcbnew.SHAPE_T_ARC: m=d.GetArcMid(); raw.append((tomm(m.x),tomm(m.y)))
    except Exception: pass
uniq=[]
for p in raw:
    if all(abs(p[0]-q[0])>0.05 or abs(p[1]-q[1])>0.05 for q in uniq): uniq.append(p)
cxp=sum(p[0] for p in uniq)/len(uniq); cyp=sum(p[1] for p in uniq)/len(uniq)
uniq.sort(key=lambda p:math.atan2(p[1]-cyp,p[0]-cxp)); poly=list(uniq)
def pip(x,y):
    ins=False;n=len(poly);j=n-1
    for i in range(n):
        xi,yi=poly[i];xj,yj=poly[j]
        if ((yi>y)!=(yj>y)) and (x<(xj-xi)*(y-yi)/(yj-yi+1e-12)+xi):ins=not ins
        j=i
    return ins

# ---- extract footprints ----
parts=[]
for fp in b.GetFootprints():
    pads=list(fp.Pads())
    if not pads: continue
    pos=fp.GetPosition(); ax,ay=tomm(pos.x),tomm(pos.y)
    th=math.radians(-fp.GetOrientationDegrees()); ct,st=math.cos(th),math.sin(th)
    pl=[]
    for pad in pads:
        pp=pad.GetPosition(); dx,dy=tomm(pp.x)-ax,tomm(pp.y)-ay
        pl.append((dx*ct-dy*st,dx*st+dy*ct,pad.GetNetname()))
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
    hasth=any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH,pcbnew.PAD_ATTRIB_NPTH) for p in pads)
    sides={'T','B'} if hasth else ({'B'} if fp.IsFlipped() else {'T'})
    parts.append(dict(fp=fp,x=ax,y=ay,pads=pl,hw=hw,hh=hh,sides=sides,
                      locked=fp.IsLocked(),ref=fp.GetReference()))
ext=lambda p:(p['hw']+EXT_MARGIN,p['hh']+EXT_MARGIN)
free=[i for i in range(len(parts)) if not parts[i]['locked']]
def inside_full(p):
    hw,hh=ext(p); X,Y=p['x'],p['y']; m=EDGE_MARGIN
    return all(pip(X+sx*(hw+m),Y+sy*(hh+m)) for sx in (-1,1) for sy in (-1,1))
def clamp(p):
    if inside_full(p): return
    for _ in range(500):
        p['x']+=(cxp-p['x'])*0.05; p['y']+=(cyp-p['y'])*0.05
        if inside_full(p): return
rf=next(((parts[i]['x'],parts[i]['y']) for i in range(len(parts)) if parts[i]['ref']==RF_REF and parts[i]['locked']),None)
def signets(i): return set(n for _,_,n in parts[i]['pads'] if n not in POWER)

# ---- schematic symbol positions (designer's logical grouping) ----
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
def schd(i,a):
    if parts[i]['sx'] is None or parts[a]['sx'] is None: return 1e9
    return math.hypot(parts[i]['sx']-parts[a]['sx'],parts[i]['sy']-parts[a]['sy'])

# ---- anchors = active ICs/modules (primary) + key connectors (secondary) ----
def is_ic(r): return r.startswith('IC') or r.startswith('AC') or (len(r)>1 and r[0]=='U' and r[1:].isdigit())
CONN_REFS={'J6','P1','SWD1','J5','M1','BZ1'}
anchors=[i for i in range(len(parts)) if is_ic(parts[i]['ref']) or parts[i]['ref'] in CONN_REFS]
ic_anchors=[a for a in anchors if is_ic(parts[a]['ref'])]
aset=set(anchors)

# ---- assign every non-anchor to the anchor it ELECTRICALLY belongs to ----
#   1) shares a signal net with anchor(s) -> the most-shared one (ties: nearest in schematic).
#      (a series R/cap on a display line nets with its connector; a signal cap nets with its IC)
#   2) pure power/GND part (decoupling) -> nearest IC in the schematic, never a bare connector
#   3) last resort -> nearest anchor on the PCB
cluster={a:[] for a in anchors}
for i in range(len(parts)):
    if i in aset or parts[i]['locked']: continue
    si=signets(i); cand=None
    # prefer the IC the part nets with (a hub connector like J6 shares a net with almost
    # everything, so only fall to a connector when the part touches NO IC at all)
    sh_ic=[(len(si&signets(a)),a) for a in ic_anchors if si&signets(a)]
    sh_cn=[(len(si&signets(a)),a) for a in anchors if a not in ic_anchors and si&signets(a)]
    for sh in (sh_ic,sh_cn):
        if sh:
            mx=max(s for s,_ in sh); ties=[a for s,a in sh if s==mx]
            cand=min(ties,key=lambda a:schd(i,a)); break
    if cand is None and parts[i]['sx'] is not None and ic_anchors:
        cand=min(ic_anchors,key=lambda a:schd(i,a))
    if cand is None:
        cand=min(anchors,key=lambda a:math.hypot(parts[i]['x']-parts[a]['x'],parts[i]['y']-parts[a]['y']))
    cluster[cand].append(i)
for a in cluster: cluster[a].sort(key=lambda i:schd(i,a))   # closest-in-schematic packs innermost

# ---- clearance test: side-aware courtyards + CLR, inside outline, outside RF keepout ----
def clears(i,others,clr=CLR):
    aw,ah=ext(parts[i]); X,Y=parts[i]['x'],parts[i]['y']; sa=parts[i]['sides']
    if not inside_full(parts[i]): return False
    if rf and math.hypot(X-rf[0],Y-rf[1])<RF_KP: return False
    for j in others:
        if i==j or not (sa & parts[j]['sides']): continue
        bw,bh=ext(parts[j])
        if (aw+bw+clr)-abs(X-parts[j]['x'])>0 and (ah+bh+clr)-abs(Y-parts[j]['y'])>0: return False
    return True
pbx0=min(p[0] for p in poly); pbx1=max(p[0] for p in poly); pby0=min(p[1] for p in poly); pby1=max(p[1] for p in poly)
placed=set(i for i in range(len(parts)) if parts[i]['locked'])
unplaced=[]
def place_at(i,tx,ty,clr=CLR):
    """nearest clear spot to (tx,ty): tight spiral, then full-board grid fallback"""
    for k in range(0,440):
        r=k*0.3
        if r==0:
            parts[i]['x'],parts[i]['y']=tx,ty
            if clears(i,placed,clr): placed.add(i); return True
            continue
        for ang in range(0,360,10):
            parts[i]['x']=tx+r*math.cos(math.radians(ang)); parts[i]['y']=ty+r*math.sin(math.radians(ang))
            if clears(i,placed,clr): placed.add(i); return True
    gy=pby0
    while gy<=pby1:
        gx=pbx0
        while gx<=pbx1:
            parts[i]['x']=gx; parts[i]['y']=gy
            if clears(i,placed,clr): placed.add(i); return True
            gx+=0.3
        gy+=0.3
    if i not in unplaced: unplaced.append(i)
    return False
def conn_centroid(idxs):
    nets=set()
    for i in idxs: nets|=signets(i)
    pts=[(parts[j]['x'],parts[j]['y']) for j in placed if signets(j)&nets]
    if not pts: return None
    return (sum(p[0] for p in pts)/len(pts),sum(p[1] for p in pts)/len(pts))

t0=time.time()
member_of={i:a for a in anchors for i in cluster[a]}
# ---- placement ----
# A) FIT: biggest parts first (anchors, then passives), each at the nearest clear spot to its
#    anchor. Biggest-first stops the (very full) board fragmenting, so everything actually lands.
for a in sorted((a for a in anchors if not parts[a]['locked']),key=lambda a:-(parts[a]['hw']*parts[a]['hh'])):
    place_at(a, *(conn_centroid([a]+cluster[a]) or (cxp,cyp)))
for i in sorted(member_of,key=lambda i:-(parts[i]['hw']*parts[i]['hh'])):
    place_at(i, parts[member_of[i]]['x'], parts[member_of[i]]['y'])
# B) PULL-IN: slide each passive to the nearest clear spot to its OWN anchor (only if nearer),
#    filling the gaps right beside each IC.
def d2a(i): an=member_of[i]; return math.hypot(parts[i]['x']-parts[an]['x'],parts[i]['y']-parts[an]['y'])
def pull_in(i):
    a=member_of[i]; ax,ay=parts[a]['x'],parts[a]['y']; cur=d2a(i); others=placed-{i}
    for k in range(1,260):
        r=k*0.2
        if r>=cur: return False
        for ang in range(0,360,12):
            parts[i]['x']=ax+r*math.cos(math.radians(ang)); parts[i]['y']=ay+r*math.sin(math.radians(ang))
            if clears(i,others): return True
    return False
# C) SWAP: exchange two SAME-SIZE passives that belong to different ICs when the trade brings both
#    closer to their own anchor. A swap needs no free space, so it can tighten a packed board where
#    pull-in alone is stuck.
def sz(i): return (round(parts[i]['hw'],2),round(parts[i]['hh'],2),frozenset(parts[i]['sides']))
mem=list(member_of)
for _ in range(8):
    moved=0
    for i in sorted(mem,key=lambda i:-d2a(i)):
        ox,oy=parts[i]['x'],parts[i]['y']
        if pull_in(i): moved+=1
        else: parts[i]['x'],parts[i]['y']=ox,oy
    for ii in range(len(mem)):
        i=mem[ii]; ai=member_of[i]
        for j in mem[ii+1:]:
            aj=member_of[j]
            if ai==aj or sz(i)!=sz(j): continue
            ni=math.hypot(parts[j]['x']-parts[ai]['x'],parts[j]['y']-parts[ai]['y'])
            nj=math.hypot(parts[i]['x']-parts[aj]['x'],parts[i]['y']-parts[aj]['y'])
            if ni+nj < d2a(i)+d2a(j)-0.5:
                parts[i]['x'],parts[j]['x']=parts[j]['x'],parts[i]['x']
                parts[i]['y'],parts[j]['y']=parts[j]['y'],parts[i]['y']; moved+=1
    if not moved: break

# ---- write back ----
for i in range(len(parts)):
    if not parts[i]['locked']:
        parts[i]['fp'].SetPosition(pcbnew.VECTOR2I(int(parts[i]['x']*NM),int(parts[i]['y']*NM)))
b.Save(P)

# ---- report ----
for i in unplaced:
    print(f"  UNPLACED {parts[i]['ref']} hw={round(parts[i]['hw'],1)} hh={round(parts[i]['hh'],1)} sides={parts[i]['sides']}")
coh=[math.hypot(parts[i]['x']-parts[a]['x'],parts[i]['y']-parts[a]['y']) for a in anchors for i in cluster[a]]
nmem=sum(len(v) for v in cluster.values())
print(f"clusters: {len(anchors)}   members: {nmem}   unplaced: {len(unplaced)} {[parts[i]['ref'] for i in unplaced]}")
if coh: print(f"member->anchor distance: avg {round(sum(coh)/len(coh),1)}mm  max {round(max(coh),1)}mm")
print("cluster sizes:", {parts[a]['ref']:len(cluster[a]) for a in anchors})
print(f"time {round(time.time()-t0,1)}s")
