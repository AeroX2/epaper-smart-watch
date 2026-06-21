import pcbnew, math
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
b=pcbnew.LoadBoard(P); tomm=lambda v:v/1e6
raw=[]; edges=[]
for d in list(b.GetDrawings()):
    if d.GetLayerName()=="Edge.Cuts":
        edges.append(d)
        raw.append((tomm(d.GetStart().x),tomm(d.GetStart().y)))
        raw.append((tomm(d.GetEnd().x),tomm(d.GetEnd().y)))
        try:
            if d.GetShape()==pcbnew.SHAPE_T_ARC:
                m=d.GetArcMid(); raw.append((tomm(m.x),tomm(m.y)))
        except Exception: pass
uniq=[]
for p in raw:
    if all(abs(p[0]-q[0])>0.1 or abs(p[1]-q[1])>0.1 for q in uniq): uniq.append(p)
cx=sum(p[0] for p in uniq)/len(uniq); cy=sum(p[1] for p in uniq)/len(uniq)
uniq.sort(key=lambda p: math.atan2(p[1]-cy,p[0]-cx))
for d in edges: b.Remove(d)
n=len(uniq)
for i in range(n):
    x0,y0=uniq[i]; x1,y1=uniq[(i+1)%n]
    s=pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetLayer(pcbnew.Edge_Cuts)
    s.SetStart(pcbnew.VECTOR2I(int(x0*1e6),int(y0*1e6))); s.SetEnd(pcbnew.VECTOR2I(int(x1*1e6),int(y1*1e6)))
    s.SetWidth(pcbnew.FromMM(0.1)); b.Add(s)
b.Save(P)
ps=pcbnew.SHAPE_POLY_SET()
b.GetBoardPolygonOutlines(ps,False)
print("outline vertices:",n,"-> closed outlines now:",ps.OutlineCount(),"pts:",ps.Outline(0).PointCount() if ps.OutlineCount() else 0,flush=True)
