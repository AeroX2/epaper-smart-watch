f = r'C:\Users\James\Documents\git\epaper-smart-watch\pcb\epaper-smart-watch.kicad_sch'
data = open(f, encoding='utf-8').read()          # universal-newline read -> '\n'
data = data.replace('\r\n', '\n').replace('\r', '\n')  # normalize just in case
open(f, 'wb').write(data.replace('\n', '\r\n').encode('utf-8'))  # write CRLF
b = open(f, 'rb').read()
print("CRLF lines:", b.count(b'\r\n'), " bare-LF:", b.count(b'\n') - b.count(b'\r\n'))
print("INTERSHEET_REFS:", data.count('INTERSHEET_REFS'))
