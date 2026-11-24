#include "BackupStatsActivity.h"

#include <I18n.h>
#include "activities/reader/StatsBackup.h"

void BackupStatsActivity::onEnter() {
  Activity::onEnter();
  const char* choices[] = {tr(STR_CANCEL), tr(STR_CONFIRM)};
  popup.show(tr(STR_BACKUP_NOW), tr(STR_BACKUP_STATS_CONFIRM), choices, 2, 0, [this](int index) {
    if (index == 1) backupRequested = true;
    else finish();
  });
  requestUpdate();
}

void BackupStatsActivity::loop() {
  if (backupRequested) {
    RenderLock lock;
    backupRequested = false;
    char name[64];
    const bool saved = backupGlobalStats(true, name, sizeof(name));
    const char* choices[] = {tr(STR_BACK)};
    popup.show(saved ? tr(STR_BACKUP_STATS_DONE) : tr(STR_BACKUP_STATS_FAILED), saved ? name : "",
               choices, 1, 0, [this](int) { finish(); });
    requestUpdate();
    return;
  }
  if (popup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  finish();
}

void BackupStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (!popup.processRender(renderer, mappedInput)) renderer.displayBuffer();
}
