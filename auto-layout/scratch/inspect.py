import xml.etree.ElementTree as ET
r = ET.parse(r'C:\Users\James\AppData\Local\Temp\eswreview\netlist.xml').getroot()
comps = r.find('components').findall('comp')
print("total components:", len(comps))
refs = sorted(c.get('ref') for c in comps)
print("refs:", ", ".join(refs))
for c in comps:
    ls = c.find('libsource')
    part = ls.get('part') if ls is not None else ''
    val = c.findtext('value') or ''
    if 'WB5' in part or 'WB5' in val or 'STM32' in val or 'STM32' in part:
        print("MCU:", c.get('ref'), "value=", val, "part=", part)
