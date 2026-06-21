import xml.etree.ElementTree as ET
t = ET.parse('netlist.xml'); r = t.getroot()
nets = r.find('nets')
lines=[]
for net in nets.findall('net'):
    name = net.get('name')
    nodes=[]
    for n in net.findall('node'):
        ref=n.get('ref'); pin=n.get('pin'); fn=n.get('pinfunction') or ''
        nodes.append(f"{ref}.{pin}" + (f"({fn})" if fn else ""))
    lines.append((name, nodes))
# sort: power/named first
def key(x):
    return (len(x[1])==0, x[0])
lines.sort(key=key)
print(f"=== NETS ({len(lines)}) ===")
for name,nodes in lines:
    print(f"\n[{name}] ({len(nodes)})")
    print("   " + ", ".join(nodes))
