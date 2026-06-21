import json
src = r"C:\Users\James\AppData\Local\Temp\claude\C--Users-James\e4efe0c5-adaf-4fef-8372-deef8af2c504\tasks\wzeesm38c.output"
with open(src, 'r', encoding='utf-8') as f:
    data = json.load(f)
report = data['result']['report']
report = report.replace(
    "Across the five subsystems (power/PMIC, STM32WB5MMG MCU, I2C sensors, SPI NOR flash, and the e-paper display/driver/frontlight)",
    "Across the six subsystems (power/PMIC, STM32WB5MMG MCU, I2C sensors, SPI NOR flash, the e-paper display/driver, and the loads/drivers/touch)")
header = ("<!-- Generated review - preliminary, pre-layout. Source of truth: the KiCad schematic\n"
          "     (pcb/epaper-smart-watch.kicad_sch). Connectivity verified against a kicad-cli netlist\n"
          "     export; electrical claims checked against the datasheets in pcb/. Date: 2026-06-19. -->\n\n")
out = r"C:\Users\James\Documents\git\epaper-smart-watch\pcb\SCHEMATIC_REVIEW.md"
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write(header + report + "\n")
import re
print("Wrote", out)
print("bytes:", len(header) + len(report))
print("H2 sections:", len(re.findall(r'^## ', report, re.M)))
print("table rows:", report.count('\n|'))
