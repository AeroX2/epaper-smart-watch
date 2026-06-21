import pcbnew, os
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
D=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board2.dsn"
b=pcbnew.LoadBoard(P)
print("trying 1-arg ExportSpecctraDSN...",flush=True)
ok=pcbnew.ExportSpecctraDSN(D)
print("returned",ok,"exists",os.path.exists(D),flush=True)
