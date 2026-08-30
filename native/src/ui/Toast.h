#pragma once
// Small in-app notification queue for errors/warnings that used to go to stderr via
// fprintf - which is invisible now that HNode.exe builds as a GUI (/SUBSYSTEM:WINDOWS)
// app with no attached console (see the app-icon fix's commit for why that changed).
// Save/Load failures, GLFW errors, etc. should surface here instead so they're
// actually visible to the person running the app.
#include <chrono>
#include <deque>
#include <string>

enum class ToastLevel { Info, Warning, Error };

class ToastQueue {
public:
    // Queues a message to show for `durationSeconds` (from the moment it's pushed,
    // not from when it's first drawn - matters if the app is idle/not drawing often).
    void Push(ToastLevel level, std::string text, double durationSeconds = 6.0);

    // Draws any still-active toasts as a small floating overlay (bottom-right corner)
    // and drops expired ones. Call once per frame, alongside DrawUiPanel.
    void Draw();

private:
    struct Toast {
        std::string text;
        ToastLevel level;
        std::chrono::steady_clock::time_point expiresAt;
    };

    std::deque<Toast> toasts_;
};
