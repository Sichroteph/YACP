#pragma once

#include "OpdsServerStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

/**
 * Edit screen for a single OPDS server.
 * Edits the connection, custom headers and filename format.
 * Used for both adding new servers and editing existing ones.
 */
class OpdsSettingsActivity final : public UiListActivity {
 public:
  /**
   * @param serverIndex Index into OpdsServerStore, or -1 for a new server
   */
  explicit OpdsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int serverIndex;
  OpdsServer editServer;
  bool isNewServer = false;
  bool showSaveError = false;
  OptionPopup optionPopup;

  int listCount() const override { return getMenuItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  int getMenuItemCount() const;
  void handleSelection();
  bool saveServer();

  // Four connection fields, the custom headers, filename format and Delete.
  static constexpr int MAX_MENU_ITEMS = 6 + OpdsServer::MAX_CUSTOM_HEADERS;
  freeink::ui::ListItem fieldRowItems[MAX_MENU_ITEMS]{};

  // Masked "Name: ******" display text for the two custom header rows.
  // fieldRowItems[i].value points here, so these must outlive each render
  // (unlike Name/URL/Username, which point directly into editServer's fields).
  std::string headerDisplayBuf[OpdsServer::MAX_CUSTOM_HEADERS];
};
