#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "util/ParagraphIndentMigration.h"

inline std::string paragraphIndentLabel(const uint8_t mode) {
  if (mode == FORCE_PARAGRAPH_INDENTS) return tr(STR_FORCE_PARAGRAPH_INDENTS);
  if (mode == 0) return tr(STR_STATE_OFF);
  return std::to_string(mode);
}

inline std::vector<std::string> paragraphIndentLabels() {
  std::vector<std::string> labels;
  labels.reserve(FORCE_PARAGRAPH_INDENTS + 1);
  for (uint8_t mode = 0; mode <= FORCE_PARAGRAPH_INDENTS; ++mode) {
    labels.push_back(paragraphIndentLabel(mode));
  }
  return labels;
}
