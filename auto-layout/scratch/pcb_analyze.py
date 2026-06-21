import pcbnew
b = pcbnew.LoadBoard(r"C:\Users\James\Documents\git\epaper-smart-watch\pcb\epaper-smart-watch.kicad_pcb")
# board outline bbox from Edge.Cuts
box = b.GetBoardEdgesBoundingBox()
mm = lambda v: round(pcbnew.ToMM(v),2)
print(f"Board outline bbox: {mm(box.GetWidth())} x {mm(box.GetHeight())} mm  origin=({mm(box.GetX())},{mm(box.GetY())})")
locked=[]; free=[]
for fp in b.GetFootprints():
    ref=fp.GetReference()
    (locked if fp.IsLocked() else free).append(ref)
print(f"\nLOCKED ({len(locked)}): "+", ".join(sorted(locked)))
print(f"\nFREE to place ({len(free)}): "+", ".join(sorted(free)))
# layers / copper count
print(f"\nCopper layers: {b.GetCopperLayerCount()}   nets: {b.GetNetCount()}   tracks: {len(b.GetTracks())}")
