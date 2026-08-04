# E Ink ET011TJ1 3D model

This is a millimetre-scale mechanical model derived from the ET011TJ1 Rev. 1.0
datasheet drawing in `spooky.pdf`.

## Files

- `ET011TJ1.FCStd` - editable FreeCAD assembly with named, coloured features
- `ET011TJ1.step` - CAD exchange model for case and PCB work
- `ET011TJ1.stl` - triangulated model for printing or mesh workflows
- `build_et011tj1.py` - reproducible FreeCAD builder
- `ET011TJ1-preview.png` - rendered geometry check

## Modelled dimensions

- Overall envelope: 36.4 x 52.8 x 5.1 mm
- Circular active area: 27.96 mm diameter
- Mounting-hole pitch: 25.0 x 26.0 mm
- Mounting holes: 1.5 mm diameter
- Flex tail: 7.2 mm wide
- Connector envelope: 7.6 x 2.3 mm

The four mounting bosses, front bezel, display surface, plastic back case, flex
tail, Panasonic AXT624124 connector, contacts, clips, and barcode-side details
are separate named features in FreeCAD. Very small moulding details that are not
fully dimensioned in the datasheet are representative rather than metrology
grade. Use the STEP model for enclosure clearance, but allow normal manufacturing
clearance and the datasheet's general +/-0.1 mm tolerance.

## Rebuild

From PowerShell with FreeCAD 1.1 installed:

```powershell
& 'C:\Program Files\FreeCAD 1.1\bin\freecadcmd.exe' build_et011tj1.py
```
