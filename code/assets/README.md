# Watch UI assets

The firmware uses build-time rasterized assets so it gets the typography and
icon consistency of real design files without carrying a TTF or SVG renderer on
the STM32.

- `fonts/`: Atkinson Hyperlegible Next Regular and ExtraBold, licensed under
  the SIL Open Font License 1.1 in `fonts/OFL.txt`.
- `icons/`: the small Lucide SVG subset used by the watch, with its upstream
  license in `icons/LICENSE`.

`code/tools/generate_ui_assets.py` converts these sources into the generated
`watch_assets.h` and `watch_assets.cpp` 1-bit bitmaps. PlatformIO compiles the
generated files directly; Python, Pillow, and CairoSVG are only needed when the
asset sources or sizes change. Install those tools with
`python -m pip install -r code/tools/requirements-ui-assets.txt`, then run
`python code/tools/generate_ui_assets.py` from the repository root.

The minimum interface font is 16 px. Dynamic phone text is width-fitted with an
ellipsis rather than rendered with a smaller fallback font.

Fonts remain in the STM32's internal flash for now. The generated font and icon
bitmaps use about 33 KiB, so moving them into the external QSPI flash would add
runtime complexity without a useful capacity benefit yet.
