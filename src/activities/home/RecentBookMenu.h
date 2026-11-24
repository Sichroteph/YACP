#pragma once

#include <memory>

class Activity;
class GfxRenderer;
class MappedInputManager;
struct RecentBook;

std::unique_ptr<Activity> createRecentBookMenu(GfxRenderer& renderer, MappedInputManager& input,
                                               const RecentBook& book);
