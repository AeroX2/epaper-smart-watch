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

  const bool frame_shown = clean_refresh
      ? display_.showFrame(ui_.framebuffer(), WatchUi::framebufferStride(), out)
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
