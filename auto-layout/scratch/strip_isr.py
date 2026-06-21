import sys
fn=sys.argv[1]
lines=open(fn,encoding='utf-8').read().split('\n')
out=[]; i=0; removed=0
while i < len(lines):
    l=lines[i]
    if l.strip().startswith('(property "Sheet References" "${INTERSHEET_REFS}"'):
        indent=l[:len(l)-len(l.lstrip('\t'))]   # leading tabs of the (property
        close=indent+')'
        j=i+1
        while j < len(lines) and lines[j]!=close:
            j+=1
        i=j+1            # skip block through its closing ) line
        removed+=1
        continue
    out.append(l); i+=1
open(fn,'w',encoding='utf-8',newline='\n').write('\n'.join(out))
print(f"removed {removed} INTERSHEET_REFS property blocks")
