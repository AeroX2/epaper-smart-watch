import pdfplumber
with pdfplumber.open('w25.pdf') as pdf:
    for i in [4, 5]:
        t = pdf.pages[i].extract_text() or ''
        print('===== PAGE', i + 1, '=====')
        print(t)
