import pcbnew, math
b=pcbnew.LoadBoard(r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb")
tomm=lambda v:v/1e6
pcb={fp.GetReference():(tomm(fp.GetPosition().x),tomm(fp.GetPosition().y)) for fp in b.GetFootprints()}
POWER={"GND","+3V0","+5V","/VSYS",""}
# decoupling caps = C* with every pad on a power/gnd net
deco=[]
for fp in b.GetFootprints():
    r=fp.GetReference()
    if not r.startswith('C'): continue
    nets=set(p.GetNetname() for p in fp.Pads())
    if nets and nets<=POWER: deco.append(r)
# schematic positions
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
ics=[r for r in sch if (r.startswith('U') or r.startswith('IC')) and r in pcb]
print(f"decoupling caps: {len(deco)}")
ds=[]
for c in deco:
    if c not in sch: continue
    # nearest IC in schematic
    near=min(ics,key=lambda u:math.hypot(sch[c][0]-sch[u][0],sch[c][1]-sch[u][1]))
    schd=math.hypot(sch[c][0]-sch[near][0],sch[c][1]-sch[near][1])
    pcbd=math.hypot(pcb[c][0]-pcb[near][0],pcb[c][1]-pcb[near][1])
    ds.append((c,near,round(schd,1),round(pcbd,1)))
ds.sort(key=lambda x:x[3])
print(f"PCB cap->its-IC distance: avg {round(sum(d[3] for d in ds)/len(ds),1)}mm  max {max(d[3] for d in ds)}mm")
for c,u,sd,pd in ds: print(f"  {c}->{u}  sch {sd}mm  pcb {pd}mm")
