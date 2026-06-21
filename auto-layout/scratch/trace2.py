import re, collections
txt = open(r'C:/Users/James/AppData/Local/Temp/eswreview/design_data.md', encoding='utf-8', errors='replace').read()
net_re = re.compile(r'^\[(.+?)\]\s*\((\d+)\)$')
cur = None
net2pins = collections.defaultdict(list)
pin2net = {}
comp_pins = collections.defaultdict(list)  # component -> list of (pin, net)
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
            comp = re.match(r'^[A-Za-z]+\d+', base).group(0)
            comp_pins[comp].append((base, cur))

# Two-terminal parts that could bridge /VOUT to /VDD: list any component touching /VOUT and its other net
print("Components touching /VOUT and their other pins' nets:")
for comp, pins in comp_pins.items():
    nets = {n for _, n in pins}
    if '/VOUT' in nets:
        print(f'  {comp}:', pins)
print()
print("Components touching /VDD (their other pin nets), to see what feeds /VDD:")
for comp, pins in comp_pins.items():
    nets = {n for _, n in pins}
    if '/VDD' in nets and len(nets) > 1:
        print(f'  {comp}:', pins)
