#pragma once

#include <GfxRenderer.h>
#include <I18n.h>
#include <FreeInkApp.h>
#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

inline void lockedSettingsStyles(freeink::ui::ListProps& props, const freeink::ui::ThemeTokens& theme) {
  namespace fui = freeink::ui;
  props.rowStyles = theme.listRow.unset() ? fui::defaultListRowStyles() : theme.listRow;
  // The renderer may lack gray glyph drawing. A dithered row background
  // still makes the locked state visible without a new rendering path.
  props.rowStyles.disabled.background = fui::Paint::dither(fui::Color::LightGray);
  switch (theme.listSelectionStyle) {
    case fui::SelectionStyle::LightPill:
      props.rowStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
      props.rowStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
      props.rowStyles.active = props.rowStyles.selected;
      break;
    case fui::SelectionStyle::Underline:
    case fui::SelectionStyle::Triangle:
      props.rowStyles.selected = props.rowStyles.normal;
      props.selectionMarker = theme.listSelectionStyle == fui::SelectionStyle::Underline
                                  ? fui::SelectionMarker::Underline : fui::SelectionMarker::Triangle;
      break;
    default:
      break;
  }
}

inline int settingsHelpHeight(const GfxRenderer& renderer) {
  return 2 * renderer.getLineHeight(UI_10_FONT_ID) + 3 * UITheme::getInstance().getMetrics().verticalSpacing;
}

inline void drawSettingsHelp(const GfxRenderer& renderer, StrId hint) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int x = std::max(safe.x, left) + metrics.contentSidePadding;
  const int width = std::min(safe.x + safe.width, renderer.getScreenWidth() - right) - x - metrics.contentSidePadding;
  int y = std::min(safe.y + safe.height, renderer.getScreenHeight() - bottom) - settingsHelpHeight(renderer) +
          metrics.verticalSpacing;
  // At most two wrapped lines, allocated only while drawing settings help.
  // The reader path and each SettingInfo acquire no additional owned buffers.
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, I18N.get(hint), width, 2);
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
}
