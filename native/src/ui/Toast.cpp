#include "Toast.h"

#include <cstdio>

#include "imgui.h"

void ToastQueue::Push(ToastLevel level, std::string text, double durationSeconds) {
    auto expiresAt = std::chrono::steady_clock::now() +
                      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(durationSeconds));
    toasts_.push_back(Toast{std::move(text), level, expiresAt});
    // Cap the queue so a burst of failures can't grow this unbounded - drop the
    // oldest first, they'll have been seen already if the app was running.
    while (toasts_.size() > 8) {
        toasts_.pop_front();
    }
}

void ToastQueue::Draw() {
    auto now = std::chrono::steady_clock::now();
    while (!toasts_.empty() && toasts_.front().expiresAt <= now) {
        toasts_.pop_front();
    }
    if (toasts_.empty()) return;

    const float padding = 10.0f;
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float y = displaySize.y - padding;

    // Draw newest-at-bottom, growing upward, each in its own small auto-sized window
    // pinned to the bottom-right corner - simplest way to stack a variable number of
    // toasts without manually tracking per-toast heights.
    int index = 0;
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it, ++index) {
        ImGui::SetNextWindowBgAlpha(0.9f);
        ImGui::SetNextWindowPos(ImVec2(displaySize.x - padding, y), ImGuiCond_Always, ImVec2(1.0f, 1.0f));

        ImVec4 color;
        const char* prefix;
        switch (it->level) {
            case ToastLevel::Error:
                color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                prefix = "Error";
                break;
            case ToastLevel::Warning:
                color = ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
                prefix = "Warning";
                break;
            default:
                color = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
                prefix = "Info";
                break;
        }

        char windowId[32];
        std::snprintf(windowId, sizeof(windowId), "##toast%d", index);
        ImGui::Begin(windowId, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(color, "%s", prefix);
        ImGui::SameLine();
        ImGui::TextUnformatted(it->text.c_str());
        float windowHeight = ImGui::GetWindowHeight();
        ImGui::End();

        y -= windowHeight + padding * 0.5f;
    }
}
