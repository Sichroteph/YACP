#pragma once

#include <memory>

class Activity;
class GfxRenderer;
class MappedInputManager;

std::unique_ptr<Activity> createDictionaryHistoryActivity(GfxRenderer& renderer, MappedInputManager& input);
