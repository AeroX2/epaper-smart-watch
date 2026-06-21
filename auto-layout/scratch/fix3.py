import pcbnew
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
b=pcbnew.LoadBoard(P)
bb=b.GetBoardEdgesBoundingBox(); pad=pcbnew.FromMM(0.5)
x0,y0,x1,y1=bb.GetX()-pad,bb.GetY()-pad,bb.GetRight()+pad,bb.GetBottom()+pad
nb=nf=0
for d in list(b.GetDrawings()):
    if d.GetLayerName()=="Edge.Cuts": b.Remove(d); nb+=1
for fp in b.GetFootprints():
    for it in list(fp.GraphicalItems()):
        try:
            if it.GetLayerName()=="Edge.Cuts": fp.Remove(it); nf+=1
        except Exception: pass
r=pcbnew.PCB_SHAPE(b); r.SetShape(pcbnew.SHAPE_T_RECT); r.SetLayer(pcbnew.Edge_Cuts)
r.SetStart(pcbnew.VECTOR2I(int(x0),int(y0))); r.SetEnd(pcbnew.VECTOR2I(int(x1),int(y1)))
r.SetWidth(pcbnew.FromMM(0.1)); b.Add(r); b.Save(P)
ps=pcbnew.SHAPE_POLY_SET()
b.GetBoardPolygonOutlines(ps,False)
print(f"removed board-edge={nb} fp-edge={nf}; closed outlines now={ps.OutlineCount()}",flush=True)
