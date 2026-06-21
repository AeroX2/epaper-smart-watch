import xml.etree.ElementTree as ET
t = ET.parse('netlist.xml'); r = t.getroot()

# Components
comps = r.find('components')
out = []
for c in comps.findall('comp'):
    ref = c.get('ref')
    val = (c.findtext('value') or '').strip()
    fp = (c.findtext('footprint') or '').strip()
    libsrc = c.find('libsource')
    lib = libsrc.get('lib')+':'+libsrc.get('part') if libsrc is not None else ''
    props = {}
    for f in c.findall('./fields/field'):
        props[f.get('name')] = (f.text or '').strip()
    ds = props.get('Datasheet','')
    out.append((ref,val,fp,lib,ds,props))

def refkey(x):
    ref=x[0]
    import re
    m=re.match(r'([A-Za-z_]+)(\d+)',ref)
    return (m.group(1), int(m.group(2))) if m else (ref,0)
out.sort(key=refkey)

print("=== COMPONENTS ({}) ===".format(len(out)))
for ref,val,fp,lib,ds,props in out:
    print(f"{ref}\t{val}\t[{lib}]\tfp={fp}")

print("\n=== EXTRA FIELDS (non-empty, excluding std) ===")
std={'Reference','Value','Footprint','Datasheet'}
for ref,val,fp,lib,ds,props in out:
    extra={k:v for k,v in props.items() if k not in std and v}
    if extra:
        print(f"{ref}: {extra}")
