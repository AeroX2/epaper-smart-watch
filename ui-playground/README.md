# E-paper watch UI playground

Interactive browser prototype for exploring the watch interface before moving
layouts into STM32 firmware.

## What it models

- a real 240 x 240 canvas for every watch frame
- one-bit black/white output after rendering
- the circular ET011TJ1 active area
- Prev, Next, Home, and Select button navigation
- e-paper refresh flashing and optional ghosting
- clock, messages, timer, alarms, music, weather, sensors, activity, and
  settings screens
- adjustable time, battery, weather, activity, and hardware states

Settings are stored only in the browser's local storage.

## Run locally

```text
npm install
npm run dev
```

Then open the local URL printed by the development server.

## Validate

```text
npm run build
npm test
```
