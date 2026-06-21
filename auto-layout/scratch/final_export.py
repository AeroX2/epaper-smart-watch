import pcbnew, os
print("DOC:", (pcbnew.ExportSpecctraDSN.__doc__ or "")[:200], flush=True)
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
D=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.dsn"
b=pcbnew.LoadBoard(P)
ps=pcbnew.SHAPE_POLY_SET(); b.GetBoardPolygonOutlines(ps,True)
print("outline(infer=True) outlines",ps.OutlineCount(),"pts",ps.Outline(0).PointCount() if ps.OutlineCount() else 0,flush=True)
print("exporting DSN...",flush=True)
ok=pcbnew.ExportSpecctraDSN(b,D)
print("returned",ok,"exists",os.path.exists(D),"size",(os.path.getsize(D) if os.path.exists(D) else 0),flush=True)
