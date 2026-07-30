"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";

type Screen =
  | "clock"
  | "notifications"
  | "timer"
  | "alarms"
  | "music"
  | "weather"
  | "environment"
  | "activity"
  | "settings";

type Face = "linear" | "split" | "instrument";
type KeyAction = "home" | "prev" | "next" | "select";

type Settings = {
  screen: Screen;
  face: Face;
  time: string;
  battery: number;
  temperature: number;
  humidity: number;
  steps: number;
  bluetooth: boolean;
  is24Hour: boolean;
  ledRing: boolean;
  ghosting: boolean;
  safeGuide: boolean;
};

const STORAGE_KEY = "epaper-watch-ui-settings-v1";

const defaults: Settings = {
  screen: "clock",
  face: "linear",
  time: "09:42",
  battery: 72,
  temperature: 18,
  humidity: 54,
  steps: 6842,
  bluetooth: true,
  is24Hour: true,
  ledRing: false,
  ghosting: true,
  safeGuide: false,
};

const screens: { id: Screen; label: string; index: string }[] = [
  { id: "clock", label: "Clock", index: "00" },
  { id: "notifications", label: "Messages", index: "01" },
  { id: "timer", label: "Timer", index: "02" },
  { id: "alarms", label: "Alarms", index: "03" },
  { id: "music", label: "Music", index: "04" },
  { id: "weather", label: "Weather", index: "05" },
  { id: "environment", label: "Sensors", index: "06" },
  { id: "activity", label: "Activity", index: "07" },
  { id: "settings", label: "Settings", index: "08" },
];

const faces: { id: Face; label: string; note: string }[] = [
  { id: "linear", label: "Stack", note: "Asymmetric hour/minute" },
  { id: "split", label: "Centre", note: "Maximum-distance read" },
  { id: "instrument", label: "Data", note: "Dense complications" },
];

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

function formatTime(time: string, is24Hour: boolean) {
  const [hourText, minute = "00"] = time.split(":");
  const rawHour = Number(hourText || 0);
  if (is24Hour) {
    return {
      hour: String(rawHour).padStart(2, "0"),
      minute,
      suffix: "",
    };
  }

  return {
    hour: String(rawHour % 12 || 12).padStart(2, "0"),
    minute,
    suffix: rawHour >= 12 ? "PM" : "AM",
  };
}

function WatchCanvas({ settings }: { settings: Settings }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const context = canvas.getContext("2d", { willReadFrequently: true });
    if (!context) return;

    const ctx = context;
    const ink = "#000000";
    const paper = "#ffffff";
    const currentIndex = screens.findIndex((item) => item.id === settings.screen);
    const time = formatTime(settings.time, settings.is24Hour);

    const font = (size: number, weight = 700) =>
      `${weight} ${size}px "Arial Narrow", "Bahnschrift", sans-serif`;
    const mono = (size: number, weight = 700) =>
      `${weight} ${size}px "Cascadia Mono", "Consolas", monospace`;

    const text = (
      value: string,
      x: number,
      y: number,
      options: {
        size?: number;
        weight?: number;
        align?: CanvasTextAlign;
        family?: "mono" | "sans";
      } = {},
    ) => {
      ctx.fillStyle = ink;
      ctx.font =
        options.family === "sans"
          ? font(options.size ?? 10, options.weight)
          : mono(options.size ?? 10, options.weight);
      ctx.textAlign = options.align ?? "left";
      ctx.textBaseline = "top";
      ctx.fillText(value, x, y);
    };

    const line = (x1: number, y1: number, x2: number, y2: number, width = 1) => {
      ctx.strokeStyle = ink;
      ctx.lineWidth = width;
      ctx.beginPath();
      ctx.moveTo(x1, y1);
      ctx.lineTo(x2, y2);
      ctx.stroke();
    };

    const outline = (x: number, y: number, width: number, height: number) => {
      ctx.strokeStyle = ink;
      ctx.lineWidth = 1;
      ctx.strokeRect(x + 0.5, y + 0.5, width - 1, height - 1);
    };

    const header = (label: string, marker = "◆") => {
      text(`${marker} ${label}`, 120, 15, {
        size: 8,
        align: "center",
      });
    };

    const edgeArrows = () => {
      ctx.fillStyle = ink;
      ctx.beginPath();
      ctx.moveTo(10, 118);
      ctx.lineTo(17, 113);
      ctx.lineTo(17, 123);
      ctx.closePath();
      ctx.fill();
      ctx.beginPath();
      ctx.moveTo(230, 118);
      ctx.lineTo(223, 113);
      ctx.lineTo(223, 123);
      ctx.closePath();
      ctx.fill();
    };

    const dots = () => {
      const count = screens.length;
      const gap = 10;
      const start = 120 - ((count - 1) * gap) / 2;
      for (let index = 0; index < count; index += 1) {
        if (index === currentIndex) {
          ctx.fillStyle = ink;
          ctx.fillRect(start + index * gap - 2, 202, 5, 5);
        } else {
          outline(start + index * gap - 2, 202, 5, 5);
        }
      }
    };

    const cardFrame = (label: string, marker = "◆", footer = "SEL · OPEN") => {
      header(label, marker);
      edgeArrows();
      dots();
      text(footer, 120, 218, { size: 7, align: "center" });
    };

    const progress = (
      x: number,
      y: number,
      width: number,
      value: number,
      height = 7,
    ) => {
      outline(x, y, width, height);
      ctx.fillStyle = ink;
      ctx.fillRect(x + 2, y + 2, Math.max(0, (width - 4) * clamp(value, 0, 1)), height - 4);
    };

    const toggle = (x: number, y: number, enabled: boolean) => {
      outline(x, y, 28, 13);
      ctx.fillStyle = ink;
      ctx.fillRect(enabled ? x + 17 : x + 3, y + 3, 8, 7);
    };

    const fitText = (
      value: string,
      x: number,
      y: number,
      maxWidth: number,
      size = 10,
      weight = 700,
    ) => {
      ctx.font = font(size, weight);
      let output = value;
      while (output.length > 1 && ctx.measureText(output).width > maxWidth) {
        output = `${output.slice(0, -2)}…`;
      }
      text(output, x, y, { size, weight, family: "sans" });
    };

    const drawClock = () => {
      if (settings.face === "split") {
        text("THU 31 JUL", 120, 18, { size: 8, align: "center" });
        text(time.hour, 120, 48, { size: 70, align: "center" });
        line(67, 116, 173, 116, 2);
        text(time.minute, 120, 119, { size: 70, align: "center" });
        if (time.suffix) text(time.suffix, 181, 151, { size: 7 });
        text(`${settings.temperature}°  ·  ${settings.steps.toLocaleString()} STEPS`, 120, 202, {
          size: 8,
          align: "center",
        });
        return;
      }

      if (settings.face === "instrument") {
        text("SYD / THU", 120, 17, { size: 8, align: "center" });
        text(`${time.hour}:${time.minute}`, 120, 49, {
          size: 47,
          align: "center",
        });
        line(48, 105, 192, 105, 2);
        const cells = [
          ["LOCAL", `${settings.temperature}°`],
          ["STEPS", settings.steps.toLocaleString()],
          ["HUMID", `${settings.humidity}%`],
          ["ALARM", "07:30"],
        ];
        cells.forEach(([label, value], index) => {
          const x = index % 2 === 0 ? 53 : 124;
          const y = index < 2 ? 119 : 159;
          text(label, x, y, { size: 7 });
          text(value, x, y + 10, { size: 14 });
        });
        line(118, 113, 118, 191);
        line(49, 151, 191, 151);
        text(`${settings.bluetooth ? "BT" : "--"} · ${settings.battery}%`, 120, 207, {
          size: 8,
          align: "center",
        });
        return;
      }

      text(time.hour, 35, 39, { size: 61 });
      text(time.minute, 35, 96, { size: 61 });
      line(139, 46, 139, 186, 2);
      text("FRI", 151, 51, { size: 7 });
      text("31 JUL", 151, 62, { size: 9 });
      if (time.suffix) text(time.suffix, 199, 52, { size: 6 });
      outline(151, 82, 28, 12);
      ctx.fillStyle = ink;
      ctx.fillRect(179, 86, 3, 4);
      ctx.fillRect(153, 84, Math.round(23 * settings.battery / 100), 8);
      text(`${settings.battery}%`, 151, 97, { size: 7 });
      text(`${settings.temperature}°`, 151, 119, { size: 17 });
      text(`${settings.humidity}% RH`, 151, 140, { size: 7 });
      line(150, 156, 208, 156);
      text("ALARM", 151, 164, { size: 6 });
      text("07:30", 151, 174, { size: 10 });
      text(`${settings.bluetooth ? "B CONNECTED" : "OFFLINE"} · ${settings.steps.toLocaleString()} STEP`, 120, 211, {
        size: 7,
        align: "center",
      });
    };

    const drawMessage = () => {
      cardFrame("MESSAGE · 2 MIN", "✉", "SEL · DISMISS");
      text("Robin Meyer", 42, 55, { size: 16, family: "sans" });
      fitText("Lunch at the usual place?", 42, 78, 155, 11, 500);
      fitText("I’m heading out around 12:30.", 42, 93, 155, 10, 500);
      line(42, 116, 198, 116);
      text("SIGNAL", 42, 126, { size: 7 });
      text("REPLY ON PHONE", 42, 138, { size: 7 });
    };

    const drawTimer = () => {
      cardFrame("TIMER", "⌛", "PREV/NEXT ±1MIN · SEL GO");
      text("05:00", 120, 69, { size: 43, align: "center" });
      text("READY", 120, 119, { size: 8, align: "center" });
      line(67, 138, 173, 138, 2);
      text("VIBRATION AT FINISH", 120, 153, { size: 7, align: "center" });
    };

    const drawAlarms = () => {
      cardFrame("ALARMS", "♟", "SEL · TOGGLE");
      const alarmRows = [
        ["07:30", "MO TU WE TH FR", true],
        ["09:00", "SA SU", false],
      ] as const;
      alarmRows.forEach(([alarmTime, days, enabled], index) => {
        const y = 57 + index * 56;
        text(alarmTime, 45, y, { size: 19 });
        text(days, 46, y + 23, { size: 7 });
        toggle(163, y + 5, enabled);
        if (index === 0) line(42, y + 45, 198, y + 45);
      });
    };

    const drawMusic = () => {
      cardFrame("PLAYING", "♫", "PREV · PLAY/PAUSE · NEXT");
      fitText("Paper Moon", 42, 52, 155, 15);
      fitText("Low Tide Radio", 42, 72, 155, 10, 500);
      progress(42, 96, 156, 0.62, 8);
      text("1:48", 42, 109, { size: 7 });
      text("3:02", 198, 109, { size: 7, align: "right" });
      text("|◀", 62, 135, { size: 17, align: "center" });
      ctx.beginPath();
      ctx.arc(120, 143, 21, 0, Math.PI * 2);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(115, 132);
      ctx.lineTo(115, 154);
      ctx.lineTo(130, 143);
      ctx.closePath();
      ctx.fill();
      text("▶|", 178, 135, { size: 17, align: "center" });
      text("PAUSED · 1/2", 120, 176, { size: 7, align: "center" });
    };

    const drawWeather = () => {
      cardFrame("SYDNEY", "◆", "PHONE · UPDATED 8 MIN AGO");
      text(`${settings.temperature}°`, 120, 48, { size: 53, align: "center" });
      text("PARTLY CLOUDY", 120, 102, { size: 9, align: "center" });
      text("H 21°  ·  L 12°", 120, 120, { size: 9, align: "center" });
      const forecast = [
        ["12H", "19°"],
        ["15H", "18°"],
        ["18H", "16°"],
      ];
      forecast.forEach(([hour, value], index) => {
        const x = 76 + index * 44;
        text(hour, x, 147, { size: 7, align: "center" });
        text(value, x, 159, { size: 11, align: "center" });
      });
      text("RAIN 10%", 120, 181, { size: 7, align: "center" });
    };

    const drawSensors = () => {
      cardFrame("SENSORS", "▣", "BME + BMA400 · DEVICE DATA");
      const rows = [
        ["ACCEL", "0.12 g"],
        ["PRESS", "1017 hPa"],
        ["TEMP", `${settings.temperature}.2°C`],
        ["HUMID", `${settings.humidity}% RH`],
      ];
      rows.forEach(([label, value], index) => {
        const y = 51 + index * 29;
        text(label, 48, y, { size: 7 });
        text(value, 190, y, { size: 10, align: "right" });
        line(47, y + 19, 193, y + 19);
      });
      text("NOT BODY TEMPERATURE", 120, 174, { size: 7, align: "center" });
    };

    const drawActivity = () => {
      cardFrame("ACTIVITY", "★", "GOAL · 10,000 STEPS");
      text(settings.steps.toLocaleString(), 120, 54, {
        size: 37,
        align: "center",
      });
      text("STEPS TODAY", 120, 96, { size: 8, align: "center" });
      progress(48, 116, 144, settings.steps / 10000, 9);
      text(`${Math.round(settings.steps * 0.00074 * 10) / 10} KM`, 72, 140, {
        size: 10,
        align: "center",
      });
      text(`${Math.round(settings.steps * 0.041)} KCAL`, 168, 140, {
        size: 10,
        align: "center",
      });
      line(120, 136, 120, 163);
      text("54 ACTIVE MIN", 120, 173, { size: 8, align: "center" });
    };

    const drawSettings = () => {
      cardFrame("SETTINGS", "⚙", "PREV/NEXT · SEL");
      const tiles = [
        ["B", settings.bluetooth ? "BT ON" : "BT OFF"],
        ["◐", "QUIET"],
        ["✳", settings.ledRing ? "LED ON" : "LED OFF"],
        ["◖", "VIBRA"],
      ];
      tiles.forEach(([icon, label], index) => {
        const x = index % 2 === 0 ? 60 : 121;
        const y = index < 2 ? 52 : 115;
        outline(x, y, 58, 55);
        text(icon, x + 29, y + 9, { size: 16, align: "center" });
        text(label, x + 29, y + 34, { size: 7, align: "center" });
      });
    };

    ctx.clearRect(0, 0, 240, 240);
    ctx.save();
    ctx.beginPath();
    ctx.arc(120, 120, 118, 0, Math.PI * 2);
    ctx.clip();
    ctx.fillStyle = paper;
    ctx.fillRect(0, 0, 240, 240);
    ctx.fillStyle = ink;
    ctx.strokeStyle = ink;
    ctx.lineCap = "square";
    ctx.lineJoin = "miter";

    switch (settings.screen) {
      case "notifications":
        drawMessage();
        break;
      case "timer":
        drawTimer();
        break;
      case "alarms":
        drawAlarms();
        break;
      case "music":
        drawMusic();
        break;
      case "weather":
        drawWeather();
        break;
      case "environment":
        drawSensors();
        break;
      case "activity":
        drawActivity();
        break;
      case "settings":
        drawSettings();
        break;
      default:
        drawClock();
    }
    ctx.restore();

    const pixels = ctx.getImageData(0, 0, 240, 240);
    for (let index = 0; index < pixels.data.length; index += 4) {
      if (pixels.data[index + 3] === 0) continue;
      const luminance =
        pixels.data[index] * 0.299 +
        pixels.data[index + 1] * 0.587 +
        pixels.data[index + 2] * 0.114;
      const value = luminance < 160 ? 0 : 255;
      pixels.data[index] = value;
      pixels.data[index + 1] = value;
      pixels.data[index + 2] = value;
      pixels.data[index + 3] = 255;
    }
    ctx.putImageData(pixels, 0, 0);
  }, [settings]);

  return (
    <canvas
      ref={canvasRef}
      className="watch-canvas"
      width={240}
      height={240}
      aria-label={`${screens.find((item) => item.id === settings.screen)?.label ?? "Clock"} screen, rendered as a true 240 by 240 one-bit buffer`}
    />
  );
}

function BatteryIcon({ value }: { value: number }) {
  return (
    <span className="battery-icon" aria-label={`${value}% battery`}>
      <span
        className="battery-icon__fill"
        style={{ width: `${clamp(value, 0, 100)}%` }}
      />
    </span>
  );
}

function StatusBar({
  battery,
  bluetooth,
  title,
}: {
  battery: number;
  bluetooth: boolean;
  title: string;
}) {
  return (
    <div className="watch-status">
      <span>{title}</span>
      <span className="watch-status__right">
        <span aria-label={bluetooth ? "Bluetooth connected" : "Bluetooth disconnected"}>
          {bluetooth ? "B" : "—"}
        </span>
        <BatteryIcon value={battery} />
      </span>
    </div>
  );
}

function ClockScreen({
  settings,
}: {
  settings: Settings;
}) {
  const displayTime = formatTime(settings.time, settings.is24Hour);

  if (settings.face === "split") {
    return (
      <div className="watch-screen-content split-face">
        <StatusBar
          battery={settings.battery}
          bluetooth={settings.bluetooth}
          title="THU 31"
        />
        <div className="split-time" aria-label={`${displayTime.hour}:${displayTime.minute}`}>
          <span>{displayTime.hour}</span>
          <span>{displayTime.minute}</span>
          {displayTime.suffix && <small>{displayTime.suffix}</small>}
        </div>
        <div className="split-footer">
          <span>{settings.temperature}°</span>
          <span>AL 06:30</span>
        </div>
      </div>
    );
  }

  if (settings.face === "instrument") {
    return (
      <div className="watch-screen-content instrument-face">
        <StatusBar
          battery={settings.battery}
          bluetooth={settings.bluetooth}
          title="SYD / THU"
        />
        <div className="instrument-time">
          <span>{displayTime.hour}</span>
          <i />
          <span>{displayTime.minute}</span>
          {displayTime.suffix && <small>{displayTime.suffix}</small>}
        </div>
        <div className="instrument-grid">
          <div>
            <b>{settings.temperature}°</b>
            <span>LOCAL</span>
          </div>
          <div>
            <b>{settings.steps.toLocaleString()}</b>
            <span>STEPS</span>
          </div>
          <div>
            <b>06:30</b>
            <span>NEXT ALARM</span>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="watch-screen-content linear-face">
      <StatusBar
        battery={settings.battery}
        bluetooth={settings.bluetooth}
        title="THU · 31 JUL"
      />
      <div className="linear-time" aria-label={`${displayTime.hour}:${displayTime.minute}`}>
        <span>{displayTime.hour}</span>
        <i>:</i>
        <span>{displayTime.minute}</span>
        {displayTime.suffix && <small>{displayTime.suffix}</small>}
      </div>
      <div className="weather-line">
        <span className="weather-symbol" aria-hidden="true">○</span>
        <b>{settings.temperature}°</b>
        <span>Clear · H21 L13</span>
      </div>
      <div className="clock-footer">
        <span>ALARM 06:30</span>
        <span>{settings.steps.toLocaleString()} STEP</span>
      </div>
    </div>
  );
}

function NotificationScreen({ settings }: { settings: Settings }) {
  return (
    <div className="watch-screen-content notice-screen">
      <StatusBar
        battery={settings.battery}
        bluetooth={settings.bluetooth}
        title="NOTIFICATION"
      />
      <div className="notice-source">
        <span className="app-mark">S</span>
        <div>
          <b>Signal</b>
          <span>2 min ago</span>
        </div>
      </div>
      <p className="notice-sender">Maya</p>
      <p className="notice-copy">
        The boards arrived. Display window looks even better in person.
      </p>
      <div className="screen-actions">
        <span>BACK</span>
        <b>OPEN</b>
        <span>NEXT</span>
      </div>
    </div>
  );
}

function WeatherScreen({ settings }: { settings: Settings }) {
  return (
    <div className="watch-screen-content weather-screen">
      <StatusBar
        battery={settings.battery}
        bluetooth={settings.bluetooth}
        title="SYDNEY"
      />
      <div className="weather-hero">
        <div className="sun-glyph" aria-label="Clear">●</div>
        <div>
          <strong>{settings.temperature}°</strong>
          <span>Clear</span>
        </div>
      </div>
      <div className="weather-stats">
        <div><span>HIGH</span><b>21°</b></div>
        <div><span>LOW</span><b>13°</b></div>
        <div><span>RAIN</span><b>10%</b></div>
      </div>
      <p className="weather-note">Phone forecast · updated 8 min ago</p>
    </div>
  );
}

function ActivityScreen({ settings }: { settings: Settings }) {
  const progress = clamp(settings.steps / 10000, 0, 1);
  return (
    <div className="watch-screen-content activity-screen">
      <StatusBar
        battery={settings.battery}
        bluetooth={settings.bluetooth}
        title="ACTIVITY"
      />
      <div className="step-number">
        <strong>{settings.steps.toLocaleString()}</strong>
        <span>STEPS TODAY</span>
      </div>
      <div className="step-track">
        <span style={{ width: `${progress * 100}%` }} />
      </div>
      <div className="activity-meta">
        <div><b>{Math.round(settings.steps * 0.00074 * 10) / 10}</b><span>KM</span></div>
        <div><b>54</b><span>ACTIVE MIN</span></div>
        <div><b>{Math.round(settings.steps * 0.041)}</b><span>KCAL</span></div>
      </div>
      <div className="week-bars" aria-label="Seven day step chart">
        {[42, 65, 38, 82, 58, 92, progress * 100].map((height, index) => (
          <span key={index} style={{ height: `${Math.max(10, height)}%` }} />
        ))}
      </div>
    </div>
  );
}

function EnvironmentScreen({ settings }: { settings: Settings }) {
  return (
    <div className="watch-screen-content environment-screen">
      <StatusBar
        battery={settings.battery}
        bluetooth={settings.bluetooth}
        title="ENVIRONMENT"
      />
      <div className="environment-reading">
        <span>LOCAL TEMP</span>
        <strong>{settings.temperature}.2°</strong>
        <small>Device reading, not body temperature</small>
      </div>
      <div className="environment-grid">
        <div><span>HUMIDITY</span><b>{settings.humidity}%</b></div>
        <div><span>PRESSURE</span><b>1017</b><small>hPa</small></div>
        <div><span>TREND</span><b>+1.2</b><small>hPa / 3h</small></div>
      </div>
    </div>
  );
}

function MusicScreen({ settings }: { settings: Settings }) {
  return (
    <div className="watch-screen-content music-screen">
      <StatusBar
        battery={settings.battery}
        bluetooth={settings.bluetooth}
        title="PLAYING"
      />
      <div className="album-mark">
        <span>NO</span>
        <span>ART</span>
      </div>
      <p className="track-name">An Ending (Ascent)</p>
      <p className="artist-name">Brian Eno</p>
      <div className="track-progress"><span /></div>
      <div className="music-controls">
        <button type="button" aria-label="Previous track">|‹</button>
        <button type="button" className="play-button" aria-label="Play">▶</button>
        <button type="button" aria-label="Next track">›|</button>
      </div>
    </div>
  );
}

function TimerScreen({ settings }: { settings: Settings }) {
  return (
    <div className="watch-screen-content timer-screen">
      <StatusBar
        battery={settings.battery}
        bluetooth={settings.bluetooth}
        title="FOCUS TIMER"
      />
      <div className="timer-dial">
        <span>24:38</span>
        <small>OF 30 MIN</small>
      </div>
      <div className="timer-rule"><span /></div>
      <p>Vibration cue at finish</p>
      <div className="screen-actions">
        <span>RESET</span>
        <b>PAUSE</b>
        <span>+5 MIN</span>
      </div>
    </div>
  );
}

function SettingsScreen({ settings }: { settings: Settings }) {
  const rows = [
    ["Bluetooth", settings.bluetooth ? "Connected" : "Off"],
    ["Quiet mode", "22:30–07:00"],
    ["Clock", settings.is24Hour ? "24 hour" : "12 hour"],
    ["LED ring", settings.ledRing ? "On" : "Off"],
  ];
  return (
    <div className="watch-screen-content settings-screen">
      <StatusBar
        battery={settings.battery}
        bluetooth={settings.bluetooth}
        title="SETTINGS"
      />
      <div className="settings-list">
        {rows.map(([label, value], index) => (
          <div className={index === 0 ? "is-selected" : ""} key={label}>
            <span>{label}</span>
            <b>{value}</b>
          </div>
        ))}
      </div>
      <p className="settings-hint">SELECT TO CHANGE</p>
    </div>
  );
}

function WatchContent({ settings }: { settings: Settings }) {
  switch (settings.screen) {
    case "notifications":
      return <NotificationScreen settings={settings} />;
    case "weather":
      return <WeatherScreen settings={settings} />;
    case "activity":
      return <ActivityScreen settings={settings} />;
    case "environment":
      return <EnvironmentScreen settings={settings} />;
    case "music":
      return <MusicScreen settings={settings} />;
    case "timer":
      return <TimerScreen settings={settings} />;
    case "settings":
      return <SettingsScreen settings={settings} />;
    default:
      return <ClockScreen settings={settings} />;
  }
}

export default function Home() {
  const [settings, setSettings] = useState<Settings>(defaults);
  const [hydrated, setHydrated] = useState(false);
  const [isRefreshing, setIsRefreshing] = useState(false);
  const [refreshCount, setRefreshCount] = useState(14);
  const [lastAction, setLastAction] = useState("Ready");

  useEffect(() => {
    try {
      const saved = localStorage.getItem(STORAGE_KEY);
      if (saved) setSettings({ ...defaults, ...JSON.parse(saved) });
    } catch {
      // A corrupt playground preference should never block the prototype.
    }
    setHydrated(true);
  }, []);

  useEffect(() => {
    if (hydrated) localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
  }, [settings, hydrated]);

  const updateSetting = useCallback(
    <K extends keyof Settings>(key: K, value: Settings[K], refresh = true) => {
      setSettings((current) => ({ ...current, [key]: value }));
      if (refresh) {
        setIsRefreshing(true);
        window.setTimeout(() => {
          setIsRefreshing(false);
          setRefreshCount((count) => count + 1);
        }, 520);
      }
    },
    [],
  );

  const currentScreenIndex = useMemo(
    () => screens.findIndex((screen) => screen.id === settings.screen),
    [settings.screen],
  );

  const handleAction = useCallback(
    (action: KeyAction) => {
      if (action === "home") {
        updateSetting("screen", "clock");
        setLastAction("Home · clock");
        return;
      }

      if (action === "prev" || action === "next") {
        const direction = action === "prev" ? -1 : 1;
        const nextIndex =
          (currentScreenIndex + direction + screens.length) % screens.length;
        updateSetting("screen", screens[nextIndex].id);
        setLastAction(
          `${action === "prev" ? "Prev" : "Next"} · ${screens[nextIndex].label}`,
        );
        return;
      }

      if (settings.screen === "clock") {
        updateSetting("screen", "notifications");
        setLastAction("Select · open messages");
      } else {
        setLastAction(`Select · ${screens[currentScreenIndex].label}`);
        setIsRefreshing(true);
        window.setTimeout(() => {
          setIsRefreshing(false);
          setRefreshCount((count) => count + 1);
        }, 520);
      }
    },
    [currentScreenIndex, settings.screen, updateSetting],
  );

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      if (target?.matches("input, select, button")) return;
      const actionMap: Record<string, KeyAction | undefined> = {
        ArrowLeft: "prev",
        ArrowRight: "next",
        Enter: "select",
        Escape: "home",
      };
      const action = actionMap[event.key];
      if (action) {
        event.preventDefault();
        handleAction(action);
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [handleAction]);

  const triggerRefresh = () => {
    setIsRefreshing(true);
    setLastAction("Manual full refresh");
    window.setTimeout(() => {
      setIsRefreshing(false);
      setRefreshCount((count) => count + 1);
    }, 700);
  };

  const resetPlayground = () => {
    setSettings(defaults);
    localStorage.removeItem(STORAGE_KEY);
    setLastAction("Prototype reset");
    triggerRefresh();
  };

  return (
    <main className="workbench">
      <header className="page-header">
        <div>
          <p className="eyebrow">EPW / INTERFACE LAB</p>
          <h1>Shape the watch<br />before the firmware.</h1>
        </div>
        <div className="header-note">
          <span>TRUE 240 × 240 / 1-BIT</span>
          <p>
            Every watch frame is drawn to the same monochrome pixel buffer the
            firmware will eventually target.
          </p>
        </div>
      </header>

      <section className="lab-grid">
        <div className="watch-stage">
          <div className="stage-label">
            <span>INTERACTIVE PROTOTYPE</span>
            <span>ONE-BIT BUFFER</span>
          </div>

          <div className={`watch-object ${settings.ledRing ? "led-on" : ""}`}>
            <div className="strap strap--top"><i /></div>
            <button
              className="case-button case-button--left-top"
              type="button"
              onClick={() => handleAction("prev")}
              aria-label="Previous screen"
              data-testid="watch-prev"
            />
            <button
              className="case-button case-button--left-bottom"
              type="button"
              onClick={() => handleAction("home")}
              aria-label="Home"
              data-testid="watch-home"
            />
            <button
              className="case-button case-button--right-top"
              type="button"
              onClick={() => handleAction("next")}
              aria-label="Next screen"
              data-testid="watch-next"
            />
            <button
              className="case-button case-button--right-bottom"
              type="button"
              onClick={() => handleAction("select")}
              aria-label="Select"
              data-testid="watch-select"
            />
            <span className="button-callout button-callout--back">PREV</span>
            <span className="button-callout button-callout--up">HOME</span>
            <span className="button-callout button-callout--select">NEXT</span>
            <span className="button-callout button-callout--down">SEL</span>

            <div className="watch-case">
              <div className={`led-ring ${settings.ledRing ? "is-lit" : ""}`}>
                <div
                  className={`watch-display ${isRefreshing ? "is-refreshing" : ""} ${
                    settings.ghosting ? "ghosting-on" : ""
                  }`}
                >
                  <div className="paper-texture" />
                  <WatchCanvas settings={settings} />
                  {settings.safeGuide && (
                    <div className="safe-area-guide" aria-hidden="true">
                      <span>SAFE</span>
                    </div>
                  )}
                  <div className="refresh-flash" aria-hidden="true" />
                </div>
              </div>
            </div>
            <div className="strap strap--bottom"><i /></div>
          </div>

          <div className="stage-footer">
            <div>
              <span>LAST INPUT</span>
              <b>{lastAction}</b>
            </div>
            <div>
              <span>FULL REFRESH</span>
              <b>{String(refreshCount).padStart(3, "0")}</b>
            </div>
            <div>
              <span>KEYS</span>
              <b>← → · ENTER · ESC</b>
            </div>
          </div>
        </div>

        <aside className="control-panel" aria-label="Watch UI controls">
          <div className="panel-heading">
            <div>
              <p className="eyebrow">SIMULATION CONTROLS</p>
              <h2>Dial it in.</h2>
            </div>
            <button type="button" className="text-button" onClick={resetPlayground}>
              Reset
            </button>
          </div>

          <section className="control-section">
            <div className="section-title">
              <span>01</span>
              <h3>Clock face</h3>
            </div>
            <div className="face-options">
              {faces.map((face) => (
                <button
                  type="button"
                  key={face.id}
                  className={settings.face === face.id ? "is-active" : ""}
                  onClick={() => {
                    updateSetting("face", face.id);
                    updateSetting("screen", "clock", false);
                    setLastAction(`Face · ${face.label}`);
                  }}
                >
                  <span>{face.label}</span>
                  <small>{face.note}</small>
                </button>
              ))}
            </div>
          </section>

          <section className="control-section">
            <div className="section-title">
              <span>02</span>
              <h3>Screen</h3>
            </div>
            <div className="screen-grid">
              {screens.map((screen) => (
                <button
                  type="button"
                  key={screen.id}
                  className={settings.screen === screen.id ? "is-active" : ""}
                  onClick={() => {
                    updateSetting("screen", screen.id);
                    setLastAction(`Screen · ${screen.label}`);
                  }}
                >
                  <span>{screen.index}</span>
                  {screen.label}
                </button>
              ))}
            </div>
          </section>

          <section className="control-section">
            <div className="section-title">
              <span>03</span>
              <h3>Live data</h3>
            </div>
            <div className="field-grid">
              <label>
                <span>Time</span>
                <input
                  type="time"
                  value={settings.time}
                  onChange={(event) => updateSetting("time", event.target.value)}
                />
              </label>
              <label>
                <span>Temperature</span>
                <span className="number-input">
                  <input
                    type="number"
                    min="-10"
                    max="50"
                    value={settings.temperature}
                    onChange={(event) =>
                      updateSetting("temperature", Number(event.target.value))
                    }
                  />
                  <i>°C</i>
                </span>
              </label>
              <label className="range-field">
                <span><b>Battery</b><output>{settings.battery}%</output></span>
                <input
                  type="range"
                  min="0"
                  max="100"
                  value={settings.battery}
                  onChange={(event) =>
                    updateSetting("battery", Number(event.target.value), false)
                  }
                  onPointerUp={triggerRefresh}
                />
              </label>
              <label className="range-field">
                <span><b>Humidity</b><output>{settings.humidity}%</output></span>
                <input
                  type="range"
                  min="0"
                  max="100"
                  value={settings.humidity}
                  onChange={(event) =>
                    updateSetting("humidity", Number(event.target.value), false)
                  }
                  onPointerUp={triggerRefresh}
                />
              </label>
              <label className="range-field range-field--wide">
                <span><b>Steps</b><output>{settings.steps.toLocaleString()}</output></span>
                <input
                  type="range"
                  min="0"
                  max="20000"
                  step="100"
                  value={settings.steps}
                  onChange={(event) =>
                    updateSetting("steps", Number(event.target.value), false)
                  }
                  onPointerUp={triggerRefresh}
                />
              </label>
            </div>
          </section>

          <section className="control-section control-section--last">
            <div className="section-title">
              <span>04</span>
              <h3>Hardware states</h3>
            </div>
            <div className="toggle-list">
              <label>
                <span><b>Bluetooth</b><small>Phone connection indicator</small></span>
                <input
                  type="checkbox"
                  checked={settings.bluetooth}
                  onChange={(event) =>
                    updateSetting("bluetooth", event.target.checked)
                  }
                />
              </label>
              <label>
                <span><b>24-hour clock</b><small>Switch HH:MM formatting</small></span>
                <input
                  type="checkbox"
                  checked={settings.is24Hour}
                  onChange={(event) =>
                    updateSetting("is24Hour", event.target.checked)
                  }
                />
              </label>
              <label>
                <span><b>LED ring</b><small>Preview the display light</small></span>
                <input
                  type="checkbox"
                  checked={settings.ledRing}
                  onChange={(event) =>
                    updateSetting("ledRing", event.target.checked, false)
                  }
                />
              </label>
              <label>
                <span><b>Ghost texture</b><small>Approximate e-paper residue</small></span>
                <input
                  type="checkbox"
                  checked={settings.ghosting}
                  onChange={(event) =>
                    updateSetting("ghosting", event.target.checked, false)
                  }
                />
              </label>
              <label>
                <span><b>Circular safe area</b><small>Show the firmware layout boundary</small></span>
                <input
                  type="checkbox"
                  checked={settings.safeGuide}
                  onChange={(event) =>
                    updateSetting("safeGuide", event.target.checked, false)
                  }
                />
              </label>
            </div>
            <button type="button" className="refresh-button" onClick={triggerRefresh}>
              <span>Run full refresh</span>
              <b>FLASH PANEL</b>
            </button>
          </section>
        </aside>
      </section>
    </main>
  );
}
