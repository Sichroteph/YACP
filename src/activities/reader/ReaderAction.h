#pragma once
#include <cstdint>

// Power-action values match frozen YACP. Menu/Back use the explicit map below.
enum class ReaderAction : uint8_t {
  Ignore, Sleep, NextPage, Refresh, Font, Guide, Focus, Bookmark, Sync, Completed,
  Stats, Screenshot, Interval, Transfer, Tilt, Dark, Footnotes, Browser, Calibre,
  Join, Hotspot, Clipping, Reinforce, Upstream, Dictionary = 24
};

constexpr ReaderAction readerMenuAction(uint8_t value) {
  constexpr ReaderAction actions[] = {
      ReaderAction::Ignore, ReaderAction::Sleep, ReaderAction::Font, ReaderAction::Guide,
      ReaderAction::Focus, ReaderAction::Bookmark, ReaderAction::Refresh, ReaderAction::Sync,
      ReaderAction::Completed, ReaderAction::Stats, ReaderAction::Screenshot, ReaderAction::Interval,
      ReaderAction::Transfer, ReaderAction::Tilt, ReaderAction::Dark, ReaderAction::Footnotes,
      ReaderAction::Browser, ReaderAction::Calibre, ReaderAction::Join, ReaderAction::Hotspot,
      ReaderAction::Clipping, ReaderAction::Reinforce, ReaderAction::Upstream, ReaderAction::Dictionary};
  return value < sizeof(actions) / sizeof(actions[0]) ? actions[value] : ReaderAction::Upstream;
}

constexpr bool supportedReaderAction(ReaderAction action) {
  return action < ReaderAction::Upstream || action == ReaderAction::Dictionary;
}
