#include "YacpFonts.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <builtinFonts/yacp/families.generated.h>
#include <builtinFonts/yacp/ids.generated.h>

#include "CrossPointSettings.h"

void registerYacpFonts(GfxRenderer& renderer) {
#ifndef YACP_XLARGE
  renderer.insertFont(YACP_LEXENDDECA_12_FONT_ID, lexenddeca_12_family);
  renderer.insertFont(YACP_LEXENDDECA_14_FONT_ID, lexenddeca_14_family);
  renderer.insertFont(YACP_ATKINSONNEXT_12_FONT_ID, atkinsonnext_12_family);
  renderer.insertFont(YACP_ATKINSONNEXT_14_FONT_ID, atkinsonnext_14_family);
#else
  renderer.insertFont(YACP_LEXENDDECA_18_FONT_ID, lexenddeca_18_family);
  renderer.insertFont(YACP_LEXENDDECA_20_FONT_ID, lexenddeca_20_family);
  renderer.insertFont(YACP_ATKINSONNEXT_18_FONT_ID, atkinsonnext_18_family);
  renderer.insertFont(YACP_ATKINSONNEXT_20_FONT_ID, atkinsonnext_20_family);
#endif
  renderer.insertFont(YACP_LEXENDDECA_16_FONT_ID, lexenddeca_16_family);
  renderer.insertFont(YACP_ATKINSONNEXT_16_FONT_ID, atkinsonnext_16_family);
}

int yacpReaderFontId(const uint8_t family, const uint8_t pointSize) {
  const bool atkinson = family == CrossPointSettings::ATKINSONNEXT;
  switch (pointSize) {
#ifndef YACP_XLARGE
    case 12:
      return atkinson ? YACP_ATKINSONNEXT_12_FONT_ID : YACP_LEXENDDECA_12_FONT_ID;
    case 14:
      return atkinson ? YACP_ATKINSONNEXT_14_FONT_ID : YACP_LEXENDDECA_14_FONT_ID;
#else
    case 18:
      return atkinson ? YACP_ATKINSONNEXT_18_FONT_ID : YACP_LEXENDDECA_18_FONT_ID;
    case 20:
      return atkinson ? YACP_ATKINSONNEXT_20_FONT_ID : YACP_LEXENDDECA_20_FONT_ID;
#endif
    default:
      return atkinson ? YACP_ATKINSONNEXT_16_FONT_ID : YACP_LEXENDDECA_16_FONT_ID;
  }
}
