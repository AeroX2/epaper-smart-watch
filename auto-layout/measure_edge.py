import pcbnew, math
b=pcbnew.LoadBoard(r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb")
tomm=lambda v:v/1e6
# board polygon (same construction as placer)
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
cx=sum(p[0] for p in uniq)/len(uniq); cy=sum(p[1] for p in uniq)/len(uniq)
uniq.sort(key=lambda p:math.atan2(p[1]-cy,p[0]-cx)); poly=uniq
def pip(x,y):
    ins=False;n=len(poly);j=n-1
    for i in range(n):
        xi,yi=poly[i];xj,yj=poly[j]
        if ((yi>y)!=(yj>y)) and (x<(xj-xi)*(y-yi)/(yj-yi+1e-12)+xi):ins=not ins
        j=i
    return ins
def edge_dist(x,y):
    best=1e9;n=len(poly)
    for i in range(n):
        x1,y1=poly[i];x2,y2=poly[(i+1)%n]
        dx,dy=x2-x1,y2-y1;L2=dx*dx+dy*dy or 1
        t=max(0,min(1,((x-x1)*dx+(y-y1)*dy)/L2))
        best=min(best,math.hypot(x-(x1+t*dx),y-(y1+t*dy)))
    return best
viol=[]
for fp in b.GetFootprints():
    if fp.IsLocked() or not list(fp.Pads()): continue
    pts=[]
    try:
        cyd=fp.GetCourtyard(pcbnew.F_CrtYd)
        if cyd.OutlineCount()>=1:
            ol=cyd.Outline(0); pts=[(tomm(ol.CPoint(k).x),tomm(ol.CPoint(k).y)) for k in range(ol.PointCount())]
    except Exception: pass
    if not pts:
        for pad in fp.Pads():
            pb=pad.GetBoundingBox(); pts+=[(tomm(pb.GetX()),tomm(pb.GetY())),(tomm(pb.GetRight()),tomm(pb.GetBottom()))]
    if not pts: continue
    mc=min((edge_dist(x,y) if pip(x,y) else -edge_dist(x,y)) for x,y in pts)
    if mc<0.5: viol.append((fp.GetReference(),round(mc,2)))
viol.sort(key=lambda t:t[1])
print("parts with courtyard <0.5mm from edge (neg = pokes OUTSIDE):", len(viol))
for r,m in viol: print(f"  {r}: {m}mm")
