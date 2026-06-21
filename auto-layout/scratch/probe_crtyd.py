import pcbnew
b=pcbnew.LoadBoard(r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb")
tomm=lambda v:v/1e6
have=miss=0; samples=[]
for fp in b.GetFootprints():
    ref=fp.GetReference()
    try:
        cy=fp.GetCourtyard(pcbnew.F_CrtYd)
        n=cy.OutlineCount()
    except Exception as e:
        n=0
    if n>=1:
        have+=1
        ol=cy.Outline(0); pc=ol.PointCount()
        xs=[tomm(ol.CPoint(k).x) for k in range(pc)]; ys=[tomm(ol.CPoint(k).y) for k in range(pc)]
        if len(samples)<6: samples.append((ref,pc,round(max(xs)-min(xs),2),round(max(ys)-min(ys),2)))
    else:
        miss+=1
        if len([s for s in samples if s[0].startswith('!')])<6: samples.append(('!'+ref,0,0,0))
print("footprints with courtyard:",have," without:",miss)
for s in samples: print("  ",s)
