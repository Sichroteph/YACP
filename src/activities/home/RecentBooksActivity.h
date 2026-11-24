#pragma once

#include <memory>
#include <string>

class Activity;
class GfxRenderer;
class MappedInputManager;

std::unique_ptr<Activity> createRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                   const std::string& selectedPath, bool grid);
