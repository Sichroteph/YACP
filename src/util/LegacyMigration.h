#pragma once

namespace LegacyMigration {
// Runs before any settings/state loader. Original stores are retained outside
// /.crosspoint; only a completely prepared directory becomes the active store.
bool run(bool checkedDeepSleepResume = false);
// Call after recovering a pending book move, before opening the moved book.
bool repairTextAfterBookMove();
bool blocked();
bool sourceBackupFailed();
}  // namespace LegacyMigration
