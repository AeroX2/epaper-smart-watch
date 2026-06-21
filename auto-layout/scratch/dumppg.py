import sys
from pypdf import PdfReader
r = PdfReader(sys.argv[1])
for i in [int(x) for x in sys.argv[2:]]:
    t = r.pages[i].extract_text() or ''
    print("===== PAGE INDEX %d (len %d) =====" % (i, len(t)))
    print(t)
    print()
