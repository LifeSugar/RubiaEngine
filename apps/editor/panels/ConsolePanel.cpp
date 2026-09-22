#include "panels/ConsolePanel.hpp"

#include <imgui.h>

#include <iostream>

namespace rubia::editor
{

void ConsoleLogBuffer::append(const char* text, std::size_t size)
{
    if (text == nullptr || size == 0)
    {
        return;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    if (size >= kMaximumCharacters)
    {
        text_.assign(
            text + (size - kMaximumCharacters),
            kMaximumCharacters);
    }
    else
    {
        const std::size_t required = text_.size() + size;
        if (required > kMaximumCharacters)
        {
            text_.erase(0, required - kMaximumCharacters);
        }
        text_.append(text, size);
    }
    ++revision_;
}

void ConsoleLogBuffer::clear()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    text_.clear();
    ++revision_;
}

std::string ConsoleLogBuffer::snapshot(uint64_t& revision) const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    revision = revision_;
    return text_;
}

ConsoleTeeStreamBuffer::ConsoleTeeStreamBuffer(
    std::streambuf* destination,
    ConsoleLogBuffer& log) noexcept
    : destination_(destination),
      log_(&log)
{
}

ConsoleTeeStreamBuffer::int_type ConsoleTeeStreamBuffer::overflow(
    int_type value)
{
    if (traits_type::eq_int_type(value, traits_type::eof()))
    {
        return traits_type::not_eof(value);
    }

    const char character = traits_type::to_char_type(value);
    log_->append(&character, 1);
    return destination_->sputc(character);
}

std::streamsize ConsoleTeeStreamBuffer::xsputn(
    const char* text,
    std::streamsize size)
{
    if (size <= 0)
    {
        return 0;
    }
    const std::size_t safeSize = static_cast<std::size_t>(size);
    log_->append(text, safeSize);
    return destination_->sputn(text, size);
}

int ConsoleTeeStreamBuffer::sync()
{
    return destination_->pubsync();
}

ConsolePanel::ConsolePanel()
    : coutCapture_(std::cout.rdbuf(), log_),
      clogCapture_(std::clog.rdbuf(), log_),
      cerrCapture_(std::cerr.rdbuf(), log_)
{
    previousCout_ = std::cout.rdbuf(&coutCapture_);
    previousClog_ = std::clog.rdbuf(&clogCapture_);
    previousCerr_ = std::cerr.rdbuf(&cerrCapture_);
}

ConsolePanel::~ConsolePanel()
{
    std::cout.flush();
    std::clog.flush();
    std::cerr.flush();
    std::cout.rdbuf(previousCout_);
    std::clog.rdbuf(previousClog_);
    std::cerr.rdbuf(previousCerr_);
}

void ConsolePanel::draw(bool* open)
{
    const bool visible = ImGui::Begin("Console", open);
    if (!visible)
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
    {
        log_.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll_);
    ImGui::Separator();

    uint64_t revision = 0;
    std::string snapshot = log_.snapshot(revision);
    const bool changed = revision != cachedRevision_;
    if (changed)
    {
        cachedText_ = std::move(snapshot);
        cachedRevision_ = revision;
    }

    ImGui::BeginChild(
        "##ConsoleLog",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_HorizontalScrollbar);
    if (cachedText_.empty())
    {
        ImGui::TextDisabled("No log messages");
    }
    else
    {
        ImGui::TextUnformatted(
            cachedText_.data(),
            cachedText_.data() + cachedText_.size());
    }
    if (changed && autoScroll_)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace rubia::editor
