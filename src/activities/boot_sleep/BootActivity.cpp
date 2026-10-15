#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"

namespace {

struct CloudLobe {
  int dx;
  int dy;
  int r;
};

// Cloud silhouette, relative to its center: overlapping lobes on top of a flat base.
constexpr CloudLobe kLobes[] = {{-62, 12, 34}, {-22, -18, 46}, {30, -12, 40}, {66, 14, 31}};
constexpr int kBaseLeft = -62;
constexpr int kBaseRight = 66;
constexpr int kBaseTop = 12;
constexpr int kBaseBottom = 45;
constexpr int kOutline = 5;

bool insideCloud(const int x, const int y, const int inset) {
  if (x >= kBaseLeft && x <= kBaseRight && y >= kBaseTop && y <= kBaseBottom - inset) return true;
  for (const auto& lobe : kLobes) {
    const int r = lobe.r - inset;
    const int ddx = x - lobe.dx;
    const int ddy = y - lobe.dy;
    if (ddx * ddx + ddy * ddy <= r * r && y <= kBaseBottom - inset) return true;
  }
  return false;
}

void drawCloud(const GfxRenderer& renderer, const int cx, const int cy) {
  for (int y = -70; y <= kBaseBottom; y++) {
    for (int x = -100; x <= 100; x++) {
      if (insideCloud(x, y, 0) && !insideCloud(x, y, kOutline)) renderer.drawPixel(cx + x, cy + y);
    }
  }
}

}  // namespace

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  drawCloud(renderer, pageWidth / 2, pageHeight / 2 - 20);
  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 + 60, "Mixtli", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
