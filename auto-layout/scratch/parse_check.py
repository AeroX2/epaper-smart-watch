import re
txt=open(r'C:/Users/James/AppData/Local/Temp/eswreview/design_data.md',encoding='utf-8',errors='replace').read()
sec=txt.split('=== NETS')[1]
nets={}
cur=None
for line in sec.splitlines():
    m=re.match(r'^\[(.+?)\]\s*\((\d+)\)',line.strip())
    if m:
        cur=m.group(1); nets[cur]=[]; continue
    if cur and line.strip() and not line.startswith('==='):
        nodes=[n.strip() for n in line.strip().split(',') if n.strip()]
        nets[cur].extend(nodes)

for k in ['/VOUT','/VSYS','/VDD','/EPD_VDD','Net-(IC1-SW)','Net-(IC1-VBAT)','/VBUS','Net-(IC1-DEC)']:
    print(f"[{k}] ({len(nets.get(k,[]))})")
    for n in nets.get(k,[]):
        print("   ",n)
    print()

print("---- every net containing any IC1 pin ----")
for k,v in nets.items():
    ic1=[n for n in v if re.match(r'IC1\.\d',n)]
    if ic1:
        print(f"{k}: {ic1}")

print("\n---- U4 power pins (VBAT/VDD/VREF/VSS) and their nets ----")
for k,v in nets.items():
    for n in v:
        if re.match(r'U4\.',n) and any(x in n for x in ['VBAT','VDDA','VREF','VDDUSB','VDDSMPS']):
            print(f"{n:30s} -> {k}")

# sanity: is /VOUT name present anywhere as a load node? count occurrences of nets touching U4 or IC2 or J1.9
print("\n---- where are the loads? ----")
for tag in ['U4.15(VBAT)','U4.4(VREF+)','U4.6(VDDA)','IC2.8(VCC)','AC1.7(VDD)','U1.8(VDD)','J1.9(Pin_9)']:
    for k,v in nets.items():
        if tag in v:
            print(f"{tag:18s} on net {k}")
