import re, collections
txt = open(r'C:/Users/James/AppData/Local/Temp/eswreview/design_data.md', encoding='utf-8', errors='replace').read()
net_re = re.compile(r'^\[(.+?)\]\s*\((\d+)\)$')
cur = None
net2pins = collections.defaultdict(list)
pin2net = {}
for line in txt.splitlines():
    s = line.strip()
    m = net_re.match(s)
    if m:
        cur = m.group(1); continue
    if cur and re.match(r'^[A-Z]+\d', s):
        for tok in s.replace(',', ' ').split():
            base = tok.split('(')[0]
            net2pins[cur].append(tok)
            pin2net[base] = cur

for key in ['/VOUT', '/VDD', '/VSYS', 'Net-(IC1-SW)', 'Net-(IC1-VBAT)', 'Net-(IC1-DEC)']:
    print(f'[{key}] ->', net2pins.get(key))
print()
# Where is each PMIC output / flash supply?
for pin in ['IC1.1', 'IC1.16', 'IC1.22', 'IC2.8', 'L2.1', 'L2.2', 'U4.15', 'U4.17']:
    print(f'{pin} is in net:', pin2net.get(pin))
