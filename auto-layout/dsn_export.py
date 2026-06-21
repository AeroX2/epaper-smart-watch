"""From-scratch Specctra DSN exporter (headless; pcbnew's ExportSpecctraDSN hangs).
Convention-safe: components placed at rotation 0; each pad's real (rotated) geometry
is baked into its padstack polygon. Y is flipped (KiCad Y-down -> Specctra Y-up).
Units: 1/10 um (resolution um 10)."""
import pcbnew, math
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
OUT=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.dsn"
b=pcbnew.LoadBoard(P)
SC=lambda nm:int(round(nm/100.0))          # nm -> 1/10 um
LAYERS=[(pcbnew.F_Cu,"F.Cu"),(pcbnew.In1_Cu,"In1.Cu"),(pcbnew.In2_Cu,"In2.Cu"),(pcbnew.B_Cu,"B.Cu")]
lname={lid:nm for lid,nm in LAYERS}

# ---- boundary: convex hull of Edge.Cuts endpoints (simple, contains all parts) ----
pts=[]
for d in b.GetDrawings():
    if d.GetLayerName()=="Edge.Cuts":
        pts.append((d.GetStart().x,d.GetStart().y)); pts.append((d.GetEnd().x,d.GetEnd().y))
        try:
            if d.GetShape()==pcbnew.SHAPE_T_ARC: m=d.GetArcMid(); pts.append((m.x,m.y))
        except Exception: pass
def hull(P):
    P=sorted(set(P))
    if len(P)<3: return P
    def cross(o,a,c): return (a[0]-o[0])*(c[1]-o[1])-(a[1]-o[1])*(c[0]-o[0])
    lo=[]
    for p in P:
        while len(lo)>=2 and cross(lo[-2],lo[-1],p)<=0: lo.pop()
        lo.append(p)
    up=[]
    for p in reversed(P):
        while len(up)>=2 and cross(up[-2],up[-1],p)<=0: up.pop()
        up.append(p)
    return lo[:-1]+up[:-1]
H=hull(pts)
# slight outward pad so pads near edge aren't clipped
cx=sum(p[0] for p in H)/len(H); cy=sum(p[1] for p in H)/len(H)
H=[(x+(x-cx)*0.0, y+(y-cy)*0.0) for x,y in H]

# ---- pads / padstacks / images / nets ----
def pad_corners(pad):
    """return ('circle',dia,[]) or ('poly',0,[(x,y)...]) relative to pad center, Y-flipped, in 1/10um"""
    sz=pad.GetSize(); sh=pad.GetShape()
    if sh==pcbnew.PAD_SHAPE_CIRCLE:
        return ('circle', SC(sz.x), [])
    ang=pad.GetOrientation().AsDegrees() if hasattr(pad.GetOrientation(),'AsDegrees') else pad.GetOrientationDegrees()
    r=math.radians(ang); hw,hh=sz.x/2.0,sz.y/2.0
    cs=[]
    for sx,sy in [(-1,-1),(1,-1),(1,1),(-1,1)]:
        x=sx*hw*math.cos(r)-sy*hh*math.sin(r); y=sx*hw*math.sin(r)+sy*hh*math.cos(r)
        cs.append((SC(x),SC(-y)))   # Y-flip
    return ('poly',0,cs)
def pad_layers(pad):
    ls=pad.GetLayerSet(); out=[]
    for lid,nm in LAYERS:
        if ls.Contains(lid): out.append(nm)
    return out or ["F.Cu"]

padstacks={}   # key -> (name, shape, layers)
def padstack_for(pad):
    kind,dia,cs=pad_corners(pad); lay=tuple(pad_layers(pad))
    key=(kind,dia,tuple(cs),lay)
    if key not in padstacks:
        nm="ps%d"%(len(padstacks)+1); padstacks[key]=(nm,(kind,dia,cs),lay)
    return padstacks[key][0]

comps=[]      # (ref, image_name, place_x, place_y, side)
images={}     # image_name -> list of (padstack, pinid, x, y)
nets={}       # net -> list of "ref-pin"
for fp in b.GetFootprints():
    ref=fp.GetReference(); pads=list(fp.Pads())
    if not pads: continue
    cpos=fp.GetPosition(); side="back" if fp.IsFlipped() else "front"
    img="img_"+ref
    pinlist=[]
    seen=set()
    for i,pad in enumerate(pads):
        pn=pad.GetName() or str(i+1)
        if pn in seen: pn=pn+"_"+str(i+1)
        seen.add(pn)
        ps=padstack_for(pad)
        pp=pad.GetPosition()
        x=SC(pp.x-cpos.x); y=SC(-(pp.y-cpos.y))
        pinlist.append((ps,pn,x,y))
        nn=pad.GetNetname()
        if nn: nets.setdefault(nn,[]).append("%s-%s"%(ref,pn))
    images[img]=pinlist
    comps.append((ref,img,SC(cpos.x),SC(-cpos.y),side))

# ---- via padstack (round, all layers) ----
VIA="via_600_300"
W=2000; CLR=2000   # 0.2mm in 1/10um

# ---- emit ----
o=[]
o.append('(pcb board.dsn')
o.append('  (parser (string_quote ") (space_in_quoted_tokens on) (host_cad "KiCad") (host_version "10.0"))')
o.append('  (resolution um 10)')
o.append('  (unit um)')
o.append('  (structure')
for _,nm in LAYERS:
    o.append('    (layer %s (type signal))'%nm)
bnd='    (boundary (path pcb 0'
for x,y in H: bnd+=' %d %d'%(SC(x),SC(-y))
bnd+=' %d %d))'%(SC(H[0][0]),SC(-H[0][1]))
o.append(bnd)
o.append('    (via "%s")'%VIA)
o.append('    (rule (width %d) (clearance %d))'%(W,CLR))
o.append('  )')
o.append('  (placement')
for ref,img,x,y,side in comps:
    o.append('    (component %s (place %s %d %d %s 0))'%(img,ref,x,y,side))
o.append('  )')
o.append('  (library')
for img,pins in images.items():
    o.append('    (image %s'%img)
    for ps,pn,x,y in pins:
        o.append('      (pin %s %s %d %d)'%(ps,pn,x,y))
    o.append('    )')
for key,(nm,(kind,dia,cs),lay) in padstacks.items():
    o.append('    (padstack %s'%nm)
    for L in lay:
        if kind=='circle':
            o.append('      (shape (circle %s %d))'%(L,dia))
        else:
            pl='      (shape (polygon %s 0'%L
            for x,y in cs: pl+=' %d %d'%(x,y)
            pl+='))'; o.append(pl)
    o.append('      (attach off)')
    o.append('    )')
# via padstack
o.append('    (padstack %s'%VIA)
for _,L in LAYERS:
    o.append('      (shape (circle %s 6000))'%L)
o.append('      (attach off)')
o.append('    )')
o.append('  )')
o.append('  (network')
for nn,pins in nets.items():
    o.append('    (net "%s" (pins %s))'%(nn," ".join(pins)))
allnets=" ".join('"%s"'%n for n in nets)
o.append('    (class kicad_default "" %s'%allnets)
o.append('      (circuit (use_via %s))'%VIA)
o.append('      (rule (width %d) (clearance %d))'%(W,CLR))
o.append('    )')
o.append('  )')
o.append('  (wiring)')
o.append(')')
open(OUT,'w').write("\n".join(o))
print("DSN written:",OUT)
print("components",len(comps),"padstacks",len(padstacks),"nets",len(nets),"boundary pts",len(H))
