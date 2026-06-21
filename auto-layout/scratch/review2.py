import xml.etree.ElementTree as ET, re
P = r'C:\Users\James\AppData\Local\Temp\eswreview\netlist.xml'
r = ET.parse(P).getroot()

# ---- components ----
comps = {}
for c in r.find('components').findall('comp'):
    ls = c.find('libsource')
    comps[c.get('ref')] = {
        'val': (c.findtext('value') or '').strip(),
        'fp': (c.findtext('footprint') or '').strip(),
        'part': (ls.get('lib')+':'+ls.get('part')) if ls is not None else '',
    }
def k(ref):
    m=re.match(r'([A-Za-z]+)(\d+)',ref); return (m.group(1),int(m.group(2))) if m else (ref,0)

print("=== COMPONENTS ({}) ===".format(len(comps)))
for ref in sorted(comps,key=k):
    d=comps[ref]; print(f"{ref:6} {d['val']:22} [{d['part']}]  fp={d['fp']}")

# ---- nets ----
nets=[]
for net in r.find('nets').findall('net'):
    nodes=[(n.get('ref'),n.get('pin'),n.get('pinfunction') or '') for n in net.findall('node')]
    nets.append((net.get('name'),nodes))

# ---- U3 pin map ----
MCU='U3'
print(f"\n=== {MCU} pin -> net ===")
pinmap={}
for name,nodes in nets:
    for ref,pin,fn in nodes:
        if ref==MCU: pinmap[pin]=(name,fn)
for pn in sorted(pinmap,key=lambda x:int(x)):
    name,fn=pinmap[pn]
    print(f"pin {pn:>2} {fn:<14} -> {name}")

# ---- all nets ----
print(f"\n=== NETS ({len(nets)}) ===")
def nk(x): return (x[0].startswith('unconnected-'), x[0])
for name,nodes in sorted(nets,key=nk):
    s=", ".join(f"{rf}.{p}"+(f"({f})" if f else "") for rf,p,f in nodes)
    print(f"[{name}] ({len(nodes)})  {s}")
