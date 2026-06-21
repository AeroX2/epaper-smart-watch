import sys
from pypdf import PdfReader
fn=sys.argv[1]
r=PdfReader(fn)
pat=[s.lower() for s in sys.argv[2:]] if len(sys.argv)>2 else None
for i,p in enumerate(r.pages):
    t=p.extract_text() or ''
    if pat is None or any(s in t.lower() for s in pat):
        print(f"===== {fn} PAGE {i} (len {len(t)}) =====")
        print(t)
        print()
