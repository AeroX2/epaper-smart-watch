"""Extract every footprint's courtyard (global) and report overlapping pairs.
Uses broad-phase AABB then convex-polygon SAT. Footprints without a courtyard
fall back to their full bounding box. Usage: python check_courtyards.py [board.kicad_pcb]"""
import pcbnew, math, sys
P=sys.argv[1] if len(sys.argv)>1 else r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
b=pcbnew.LoadBoard(P); tomm=lambda v:v/1e6

def hull(pts):
    pts=sorted(set(pts))
    if len(pts)<3: return pts
    cr=lambda o,a,c:(a[0]-o[0])*(c[1]-o[1])-(a[1]-o[1])*(c[0]-o[0])
    lo=[]
    for p in pts:
        while len(lo)>=2 and cr(lo[-2],lo[-1],p)<=0: lo.pop()
        lo.append(p)
    up=[]
    for p in reversed(pts):
        while len(up)>=2 and cr(up[-2],up[-1],p)<=0: up.pop()
        up.append(p)
    return lo[:-1]+up[:-1]

parts=[]
for fp in b.GetFootprints():
    if not list(fp.Pads()): continue
    poly=None; src="crtyd"
    try:
        cy=fp.GetCourtyard(pcbnew.F_CrtYd)
        if cy.OutlineCount()>=1:
            ol=cy.Outline(0); poly=[(tomm(ol.CPoint(k).x),tomm(ol.CPoint(k).y)) for k in range(ol.PointCount())]
    except Exception: pass
    if not poly:
        pxs=[];pys=[]
        for pad in fp.Pads():
            pb=pad.GetBoundingBox(); pxs+=[tomm(pb.GetX())-0.3,tomm(pb.GetRight())+0.3]; pys+=[tomm(pb.GetY())-0.3,tomm(pb.GetBottom())+0.3]
        if not pxs: continue
        x0,y0,x1,y1=min(pxs),min(pys),max(pxs),max(pys)
        poly=[(x0,y0),(x1,y0),(x1,y1),(x0,y1)]; src="padbb"
    poly=hull(poly)
    xs=[p[0] for p in poly]; ys=[p[1] for p in poly]
    th=any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH,pcbnew.PAD_ATTRIB_NPTH) for p in fp.Pads())
    sides={'T','B'} if th else ({'B'} if fp.IsFlipped() else {'T'})
    parts.append(dict(ref=fp.GetReference(),poly=poly,bb=(min(xs),min(ys),max(xs),max(ys)),src=src,sides=sides))

def sat_overlap(p1,p2):
    """return overlap depth (mm) if convex polys intersect, else 0"""
    mind=1e18
    for poly in (p1,p2):
        for i in range(len(poly)):
            x1,y1=poly[i]; x2,y2=poly[(i+1)%len(poly)]
            nx,ny=-(y2-y1),(x2-x1); L=math.hypot(nx,ny) or 1; ax=(nx/L,ny/L)
            a=[q[0]*ax[0]+q[1]*ax[1] for q in p1]; bb=[q[0]*ax[0]+q[1]*ax[1] for q in p2]
            ov=min(max(a),max(bb))-max(min(a),min(bb))
            if ov<=0: return 0.0
            mind=min(mind,ov)
    return mind

pairs=[]
n=len(parts)
for i in range(n):
    a=parts[i]
    for j in range(i+1,n):
        c=parts[j]
        if not (a['sides'] & c['sides']): continue   # opposite faces -> can't collide
        # broad phase AABB
        if a['bb'][2]<c['bb'][0] or c['bb'][2]<a['bb'][0] or a['bb'][3]<c['bb'][1] or c['bb'][3]<a['bb'][1]: continue
        d=sat_overlap(a['poly'],c['poly'])
        if d>0.001: pairs.append((a['ref'],c['ref'],round(d,3),a['src'],c['src']))
pairs.sort(key=lambda x:-x[2])
print(f"checked {n} footprints; COURTYARD-OVERLAP PAIRS: {len(pairs)}")
for r1,r2,d,s1,s2 in pairs[:40]:
    print(f"  {r1}<->{r2}  depth {d}mm  [{s1}/{s2}]")
