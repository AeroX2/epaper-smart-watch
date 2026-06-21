"""Import Freerouting .ses into the KiCad board (pcbnew SES import hangs headless).
SES coords are 1/100 um; kicad_nm = val*10, Y un-flipped (kicad_y = -ses_y)."""
import pcbnew
P=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.kicad_pcb"
S=r"C:\Users\James\AppData\Local\Temp\eswreview\autoplace\board.ses"
b=pcbnew.LoadBoard(P)
LMAP={"F.Cu":pcbnew.F_Cu,"In1.Cu":pcbnew.In1_Cu,"In2.Cu":pcbnew.In2_Cu,"B.Cu":pcbnew.B_Cu}

# ---- s-expr parse (quotes preserved) ----
def tokenize(s):
    out=[];i=0;n=len(s)
    while i<n:
        c=s[i]
        if c.isspace(): i+=1; continue
        if c in '()': out.append(c); i+=1; continue
        if c=='"':
            j=i+1
            while j<n and s[j]!='"': j+=1
            out.append(s[i:j+1]); i=j+1; continue
        j=i
        while j<n and (not s[j].isspace()) and s[j] not in '()"': j+=1
        out.append(s[i:j]); i=j
    return out
def parse(toks):
    pos=[0]
    def rd():
        t=toks[pos[0]]; pos[0]+=1
        if t=='(':
            l=[]
            while toks[pos[0]]!=')': l.append(rd())
            pos[0]+=1; return l
        return t
    return rd()
tree=parse(tokenize(open(S).read()))
def unq(t): return t[1:-1] if t.startswith('"') else t

def find(node,key):
    if isinstance(node,list):
        if node and node[0]==key: yield node
        for x in node:
            yield from find(x,key)

K=10  # ses -> nm factor
ntrk=nvia=0; missing=set()
for net in find(tree,"net"):
    name=unq(net[1])
    ni=b.FindNet(name)
    if ni is None: missing.add(name); continue
    for item in net[2:]:
        if not isinstance(item,list): continue
        if item[0]=="wire":
            path=next((x for x in item if isinstance(x,list) and x and x[0]=="path"),None)
            if not path: continue
            layer=LMAP.get(path[1]); w=int(path[2])*K
            nums=[int(v) for v in path[3:]]
            pts=[(nums[i]*K, -nums[i+1]*K) for i in range(0,len(nums),2)]
            for i in range(len(pts)-1):
                t=pcbnew.PCB_TRACK(b)
                t.SetStart(pcbnew.VECTOR2I(*pts[i])); t.SetEnd(pcbnew.VECTOR2I(*pts[i+1]))
                t.SetWidth(w); t.SetLayer(layer); t.SetNet(ni); b.Add(t); ntrk+=1
        elif item[0]=="via":
            x=int(item[2])*K; y=-int(item[3])*K
            v=pcbnew.PCB_VIA(b)
            v.SetPosition(pcbnew.VECTOR2I(x,y)); v.SetDrill(300000); v.SetWidth(600000)
            v.SetLayerPair(pcbnew.F_Cu,pcbnew.B_Cu); v.SetNet(ni); b.Add(v); nvia+=1
b.Save(P)
print("added tracks",ntrk,"vias",nvia,"| nets not found:",len(missing))
# routing completion via ratsnest
b.BuildConnectivity()
cn=b.GetConnectivity()
print("unrouted ratsnest:", cn.GetUnconnectedCount(True) if hasattr(cn,'GetUnconnectedCount') else "n/a")
