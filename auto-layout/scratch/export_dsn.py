import pcbnew, os, sys
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
D=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.dsn"
print("loading...",flush=True)
b=pcbnew.LoadBoard(P)
print("loaded, exporting DSN...",flush=True)
try:
    r=pcbnew.ExportSpecctraDSN(b,D)
except TypeError as e:
    print("sig1 failed:",e,flush=True); r=pcbnew.ExportSpecctraDSN(D)
print("done:",r,"exists:",os.path.exists(D),"size:",(os.path.getsize(D) if os.path.exists(D) else 0),flush=True)
