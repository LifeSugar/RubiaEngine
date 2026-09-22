#pragma once

#include <cstddef>
#include <string>

namespace rubia::editor
{

enum class ContentLoadState
{
    Idle,
    Preparing,
    Uploading,
    Finalizing,
    Ready,
    Failed
};

/// Main-thread snapshot consumed by the UI; workers own separate CPU assets.
struct ContentLoadStatus
{
    ContentLoadState state = ContentLoadState::Idle;
    std::string message = "No scene loaded";
    std::size_t completed = 0;
    std::size_t total = 0;
};

} // namespace rubia::editor
