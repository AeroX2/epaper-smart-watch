#include "watch_display.h"

WatchDisplayController::WatchDisplayController(ET011TJ1& display, WatchUi& ui,
                                               WatchUiModel& model,
                                               WatchButtons& buttons)
    : display_(display), ui_(ui), model_(model), buttons_(buttons) {}

void WatchDisplayController::begin(Print& out) {
  out.println();
  out.println(F("Boot display: clearing panel to white..."));
  if (!display_.show(ET011TJ1Pattern::White, out)) {
    out.println(F("Boot clear failed; attempting the watch face anyway."));
    history_valid_ = false;
  } else {
    updates_since_clean_refresh_ = 0;
    memset(previous_framebuffer_, 0, sizeof(previous_framebuffer_));
    history_valid_ = true;
  }
  buttons_.restoreSharedSpiButton();

  ui_.showScreen(WatchScreen::Clock);
  out.println(F("Boot display: rendering watch face..."));
  render(out);
}

bool WatchDisplayController::render(Print& out) {
  return renderImpl(out, false);
}

bool WatchDisplayController::renderFull(Print& out) {
  return renderImpl(out, true);
}

bool WatchDisplayController::renderImpl(Print& out,
                                        bool force_clean_refresh) {
  ui_.render(model_);

  out.print(F("Watch UI: "));
  out.print(ui_.screenName());
  out.println(ui_.isInside() ? F(" (inside)") : F(" (browse)"));

  const bool clean_refresh =
      force_clean_refresh || !history_valid_ ||
      updates_since_clean_refresh_ >= CLEAN_REFRESH_INTERVAL;
  if (clean_refresh) {
    out.println(force_clean_refresh
                    ? F("Forced full UI cleaning refresh")
                    : F("Periodic full UI cleaning refresh"));
  }

  // The cleaning refresh: two quick refreshes, the old screen to plain white
  // and white to the new screen. Every pixel that was black is driven to
  // white and every pixel of the new screen is driven from white, with the
  // quick waveform, which drives hard. Measured with the webcam: the vendor
  // cleaning waveform (showFrame) under-drives this panel (washed-out
  // blacks, a black panel needs two passes to turn white) and left ghosts of
  // the launcher in the dithered hills; this left none. 1.45 s against
  // 1.2 s. The vendor waveform stays for when the old screen is not known.
  bool quick_clean = false;
  if (clean_refresh && history_valid_ && kw_mode_) {
    static uint8_t white[sizeof(previous_framebuffer_)];  // all zero: white
    out.println(F("Cleaning: old screen -> white -> new screen (quick)"));
    if (display_.showFrameKw(white, previous_framebuffer_,
                             WatchUi::framebufferStride(), out)) {
      memset(previous_framebuffer_, 0, sizeof(previous_framebuffer_));
      quick_clean = true;
    } else {
      history_valid_ = false;
    }
  }

  const bool frame_shown = clean_refresh && !quick_clean
      ? display_.showFrame(ui_.framebuffer(), WatchUi::framebufferStride(), out)
      : kw_mode_
            ? display_.showFrameKw(ui_.framebuffer(), previous_framebuffer_,
                                   WatchUi::framebufferStride(), out)
            : display_.showFrameFast(ui_.framebuffer(), previous_framebuffer_,
                                     WatchUi::framebufferStride(), out);
  if (!frame_shown) {
    out.println(F("Watch UI update stopped safely at the failed stage."));
    history_valid_ = false;
  } else {
    memcpy(previous_framebuffer_, ui_.framebuffer(),
           sizeof(previous_framebuffer_));
    history_valid_ = true;
    if (clean_refresh) {
      updates_since_clean_refresh_ = 0;
    } else if (updates_since_clean_refresh_ < UINT8_MAX) {
      ++updates_since_clean_refresh_;
    }
  }
  buttons_.restoreSharedSpiButton();
  active_ = true;
  last_rendered_minute_ = model_.minute;
  return frame_shown;
}

bool WatchDisplayController::active() const {
  return active_;
}

bool WatchDisplayController::clockMinuteChanged() const {
  return active_ && ui_.screen() == WatchScreen::Clock &&
         model_.minute != last_rendered_minute_;
}
