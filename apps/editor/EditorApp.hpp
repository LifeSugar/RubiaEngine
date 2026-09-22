#pragma once

#include "App.hpp"
#include "EditorLayer.hpp"

namespace rubia::editor
{

/// Editor composition root. Runtime App remains unaware of Editor classes.
class EditorApp final
{
public:
    void run();
    void runRenderTest();

private:
    [[nodiscard]] static App::RunConfig makeRunConfig();

    // App joins content workers before EditorLayer releases its log capture.
    EditorLayer editorLayer_;
    App app_;
};

} // namespace rubia::editor
