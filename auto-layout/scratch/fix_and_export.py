import pcbnew, os
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
D=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.dsn"
b=pcbnew.LoadBoard(P)
bb=b.GetBoardEdgesBoundingBox(); pad=pcbnew.FromMM(0.5)
x0,y0=bb.GetX()-pad,bb.GetY()-pad; x1,y1=bb.GetRight()+pad,bb.GetBottom()+pad
for d in list(b.GetDrawings()):
    if d.GetLayerName()=="Edge.Cuts": b.Remove(d)
r=pcbnew.PCB_SHAPE(b); r.SetShape(pcbnew.SHAPE_T_RECT); r.SetLayer(pcbnew.Edge_Cuts)
r.SetStart(pcbnew.VECTOR2I(int(x0),int(y0))); r.SetEnd(pcbnew.VECTOR2I(int(x1),int(y1)))
r.SetWidth(pcbnew.FromMM(0.1)); b.Add(r); b.Save(P)
ps=pcbnew.SHAPE_POLY_SET(); b.GetBoardPolygonOutlines(ps,False)
print("closed outlines:",ps.OutlineCount(),flush=True)
print("exporting DSN...",flush=True)
ok=pcbnew.ExportSpecctraDSN(b,D)
print("DSN ok:",ok,"exists:",os.path.exists(D),"size:",(os.path.getsize(D) if os.path.exists(D) else 0),flush=True)
