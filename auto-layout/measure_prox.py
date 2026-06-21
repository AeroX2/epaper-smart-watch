import pcbnew, math
b=pcbnew.LoadBoard(r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb")
tomm=lambda v:v/1e6
pcb={fp.GetReference():(tomm(fp.GetPosition().x),tomm(fp.GetPosition().y)) for fp in b.GetFootprints()}
# schematic positions
import re
def tok(s):
    o=[];i=0;n=len(s)
    while i<n:
        c=s[i]
        if c.isspace():i+=1;continue
        if c in '()':o.append(c);i+=1;continue
        if c=='"':
            j=i+1
            while j<n and s[j]!='"': j+=1
            o.append(s[i:j+1]);i=j+1;continue
        j=i
        while j<n and not s[j].isspace() and s[j] not in '()"':j+=1
        o.append(s[i:j]);i=j
    return o
def parse(tk):
    p=[0]
    def rd():
        t=tk[p[0]];p[0]+=1
        if t=='(':
            l=[]
            while tk[p[0]]!=')':l.append(rd())
            p[0]+=1;return l
        return t
    return rd()
tr=parse(tok(open(r"C:\Users\James\Documents\git\epaper-smart-watch\pcb\epaper-smart-watch.kicad_sch",encoding='utf-8').read()))
sch={}
for el in tr:
    if isinstance(el,list) and el and el[0]=='symbol':
        at=ref=None
        for c in el:
            if isinstance(c,list) and c and c[0]=='at': at=(float(c[1]),float(c[2]))
            if isinstance(c,list) and len(c)>=3 and c[0]=='property' and c[1].strip('"')=='Reference': ref=c[2].strip('"')
        if at and ref: sch[ref]=at
# pairs very close in schematic (<=5mm) -> should be close on PCB
pairs=[]
refs=[r for r in sch if r in pcb]
for i in range(len(refs)):
    for j in range(i+1,len(refs)):
        a,c=refs[i],refs[j]
        sd=math.hypot(sch[a][0]-sch[c][0],sch[a][1]-sch[c][1])
        if sd<=5.0:
            pd=math.hypot(pcb[a][0]-pcb[c][0],pcb[a][1]-pcb[c][1])
            pairs.append((a,c,round(sd,1),round(pd,1)))
pairs.sort(key=lambda x:-x[3])
print(f"schematic-adjacent pairs (<=5mm): {len(pairs)}")
print(f"  PCB distance: avg {round(sum(p[3] for p in pairs)/len(pairs),1)}mm  max {max(p[3] for p in pairs)}mm")
print("  worst (far on PCB despite close in schematic):")
for a,c,sd,pd in pairs[:8]: print(f"    {a}<->{c}  sch {sd}mm -> pcb {pd}mm")
print("  examples kept close:")
for a,c,sd,pd in sorted(pairs,key=lambda x:x[3])[:8]: print(f"    {a}<->{c}  sch {sd}mm -> pcb {pd}mm")
