import pcbnew
b=pcbnew.LoadBoard(r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb")
top=[];bot=[];th=[]
for fp in b.GetFootprints():
    if not list(fp.Pads()): continue
    r=fp.GetReference()
    side="bottom" if fp.IsFlipped() else "top"
    has_th=any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH,pcbnew.PAD_ATTRIB_NPTH) for p in fp.Pads())
    (bot if side=="bottom" else top).append(r)
    if has_th: th.append(r)
print(f"TOP side ({len(top)}):", ", ".join(sorted(top)))
print(f"\nBOTTOM side ({len(bot)}):", ", ".join(sorted(bot)))
print(f"\nthrough-hole (both sides) ({len(th)}):", ", ".join(sorted(th)))
