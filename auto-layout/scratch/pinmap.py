import xml.etree.ElementTree as ET, re
t = ET.parse('netlist.xml'); r = t.getroot()
# pin number -> net name, for a given ref
def pinnets(ref):
    m = {}
    for net in r.find('nets').findall('net'):
        name = net.get('name')
        for n in net.findall('node'):
            if n.get('ref') == ref:
                m[n.get('pin')] = (name, n.get('pinfunction') or '')
    return m

# get pin name (function-after-reset) from libpart for U4
def pinname(ref):
    # from component's libsource
    comp = None
    for c in r.find('components').findall('comp'):
        if c.get('ref') == ref:
            comp = c; break
    return comp

u4 = pinnets('U4')
print("=== U4 (STM32WB5MMG) pin -> net ===")
for pn in sorted(u4, key=lambda x:int(x)):
    name, fn = u4[pn]
    print(f"pin {pn:>2} {fn:<14} -> {name}")

# Detect nets that mix obviously-different subsystems (heuristic: net touching U4 plus multiple non-passive ICs)
print("\n=== nets containing a U4 pin AND another active device pin (signal nets) ===")
active = ('IC1','IC2','IC3','AC1','U1','J1')
for net in r.find('nets').findall('net'):
    refs = {}
    for n in net.findall('node'):
        refs.setdefault(n.get('ref'), []).append((n.get('pin'), n.get('pinfunction') or ''))
    if 'U4' in refs:
        others = [x for x in refs if x!='U4' and re.match(r'(IC|AC|U|J)\d', x)]
        if others:
            nm = net.get('name')
            parts = []
            for rf in ['U4']+others:
                parts.append(rf+":"+",".join(f"{p}({f})" for p,f in refs[rf]))
            print(f"  [{nm}]  " + " | ".join(parts))
