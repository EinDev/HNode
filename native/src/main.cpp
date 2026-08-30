// HNode native port - Phase 1 (see native/README.md for scope).
// Entry point: GLFW window + Dear ImGui settings panel + the render-on-change loop
// that is the whole point of this port (ArtNet in -> VRSL serializer -> Spout out,
// only touching the GPU when there is actually new data or a UI change to draw).
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "dmx/DmxBuffer.h"
#include "artnet/ArtNetReceiver.h"
#include "serializers/SerializerRegistry.h"
#include "render/FrameRenderer.h"
#include "spout/SpoutOutput.h"
#include "exporters/MidiDmxExporter.h"
#include "config/ShowConfig.h"
#include "ui/UiPanel.h"

namespace {

// Windows narrow<->wide conversion uses the ANSI codepage (CP_ACP) rather than UTF-8,
// to match what the CRT's narrow std::ifstream/std::ofstream (used by ShowConfig)
// actually opens on this platform. Non-ASCII paths are a known Phase 1 limitation.
std::string WideToNarrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

bool ShowSaveDialog(HWND owner, std::wstring& outPath) {
    wchar_t buffer[MAX_PATH] = L"NewShowConfig.shwcfg";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Show Configurations (*.shwcfg)\0*.shwcfg\0\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"shwcfg";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) {
        outPath = buffer;
        return true;
    }
    return false;
}

bool ShowOpenDialog(HWND owner, std::wstring& outPath) {
    wchar_t buffer[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Show Configurations (*.shwcfg)\0*.shwcfg\0\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        outPath = buffer;
        return true;
    }
    return false;
}

} // namespace

int main() {
    glfwSetErrorCallback([](int code, const char* desc) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
    });
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(560, 780, "HNode", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    SerializerRegistry serializerRegistry;

    ShowConfig config; // starts from defaults; use Load to open a .shwcfg.
    config.serializer = serializerRegistry.Default();

    DmxBuffer dmxBuffer;
    FrameRenderer frameRenderer;
    frameRenderer.SetResolution(config.outputWidth, config.outputHeight);

    SpoutOutput spoutOutput;
    spoutOutput.SetName(config.spoutOutputName);

    MidiDmxExporter midiExporter;
    midiExporter.Reconnect();

    std::atomic<bool> dirty{true};
    std::atomic<bool> artNetConnected{false};

    ArtNetReceiver artnet;
    artnet.SetCallback([&](uint16_t universe, const uint8_t* data, size_t length) {
        dmxBuffer.SetUniverse(universe, data, length);
        artNetConnected.store(true);
        dirty.store(true);
        // Wake the (otherwise event-driven) main thread promptly instead of waiting
        // out its poll timeout - this is what lets the render-on-change loop react to
        // a live show without spinning.
        glfwPostEmptyEvent();
    });
    artnet.Start(config.artNetAddress, static_cast<uint16_t>(config.artNetPort));

    std::string appliedArtNetAddress = config.artNetAddress;
    int appliedArtNetPort = config.artNetPort;
    std::string appliedSpoutName = config.spoutOutputName;

    while (!glfwWindowShouldClose(window)) {
        // Blocks until input arrives, glfwPostEmptyEvent() is called (by the ArtNet
        // thread above), or the timeout elapses - this is the render-on-change idle
        // path: no ArtNet data and no UI interaction means no wakeups and ~0% GPU.
        glfwWaitEventsTimeout(0.25);
        if (glfwWindowShouldClose(window)) break;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        UiPanelResult ui = DrawUiPanel(config, serializerRegistry, midiExporter,
                                        frameRenderer.TextureId(), artNetConnected.load());
        if (ui.configChanged) dirty.store(true);
        if (ui.requestMidiReconnect) midiExporter.Reconnect();

        HWND hwnd = glfwGetWin32Window(window);

        if (ui.requestSave) {
            std::wstring path;
            if (ShowSaveDialog(hwnd, path)) {
                std::string error;
                if (!config.Save(WideToNarrow(path), error)) {
                    std::fprintf(stderr, "Save failed: %s\n", error.c_str());
                }
            }
        }
        if (ui.requestLoad) {
            std::wstring path;
            if (ShowOpenDialog(hwnd, path)) {
                std::string error;
                ShowConfig loaded;
                if (ShowConfig::Load(WideToNarrow(path), loaded, serializerRegistry, error)) {
                    config = loaded;
                    dirty.store(true);
                } else {
                    std::fprintf(stderr, "Load failed: %s\n", error.c_str());
                }
            }
        }

        // Re-apply networking/Spout identity only when they actually changed, mirroring
        // Loader.cs's ReloadShowConf() calling ChangePort/ChangeIPAddress/spoutSender.spoutName
        // on every show-config (re)load - but without tearing down the sockets every frame.
        if (config.artNetAddress != appliedArtNetAddress || config.artNetPort != appliedArtNetPort) {
            artnet.Start(config.artNetAddress, static_cast<uint16_t>(config.artNetPort));
            appliedArtNetAddress = config.artNetAddress;
            appliedArtNetPort = config.artNetPort;
        }
        if (config.spoutOutputName != appliedSpoutName) {
            spoutOutput.SetName(config.spoutOutputName);
            appliedSpoutName = config.spoutOutputName;
        }

        frameRenderer.SetResolution(config.outputWidth, config.outputHeight);

        if (dirty.exchange(false)) {
            frameRenderer.Render(dmxBuffer, *config.serializer, config.maskedChannels,
                                  config.invertMask, config.autoMaskOnZero,
                                  config.serializeUniverseCount);
            spoutOutput.SendFrame(reinterpret_cast<const uint8_t*>(frameRenderer.Pixels().data()),
                                   static_cast<unsigned int>(frameRenderer.Width()),
                                   static_cast<unsigned int>(frameRenderer.Height()));
        }

        // Unlike the Spout/preview path above, MIDIDMX needs a steady watchdog
        // heartbeat regardless of whether DMX data actually changed (the VRChat-side
        // receiver times out without one) - so this runs every loop iteration, not
        // just on `dirty`. glfwWaitEventsTimeout(0.25) above bounds this to a ~4Hz
        // minimum even when fully idle, which is enough to keep MIDIDMX.cs's 1-second
        // watchdog timeout satisfied without spinning the GPU.
        midiExporter.CompleteFrame(frameRenderer.MergedDmx());

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    artnet.Stop();
    spoutOutput.Release();
    midiExporter.Shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
