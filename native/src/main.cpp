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

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <string>

#include "dmx/DmxBuffer.h"
#include "artnet/ArtNetReceiver.h"
#include "serializers/SerializerRegistry.h"
#include "render/FrameRenderer.h"
#include "spout/SpoutOutput.h"
#include "exporters/ExporterRegistry.h"
#include "generators/GeneratorRegistry.h"
#include "config/ShowConfig.h"
#include "ui/UiPanel.h"
#include "ui/FileDialog.h"

namespace {

// Matches Loader.cs's CONFIG_FILE_REGEX ("-{1,2}[Cc]onfig-[Ff]ile="): an argument
// starting with 1-2 dashes, then "config-file=" case-insensitive only on the two
// letters the C# regex itself allowed to vary, followed by the path.
bool TryParseConfigFileArg(const std::string& arg, std::string& outPath) {
    size_t start = 0;
    if (arg.size() > 0 && arg[0] == '-') start = 1;
    if (arg.size() > start && arg[start] == '-') start++;

    const std::string kPrefixLower = "config-file=";
    if (arg.size() < start + kPrefixLower.size()) return false;

    std::string candidate = arg.substr(start, kPrefixLower.size());
    for (char& c : candidate) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (candidate != kPrefixLower) return false;

    outPath = arg.substr(start + kPrefixLower.size());
    return true;
}

} // namespace

int main(int argc, char** argv) {
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
    ExporterRegistry exporterRegistry;
    GeneratorRegistry generatorRegistry;

    ShowConfig config; // starts from defaults (no exporters, VRSL serializer); use Load to open a .shwcfg.
    config.serializer = serializerRegistry.Default();

    DmxBuffer dmxBuffer;
    FrameRenderer frameRenderer;
    frameRenderer.SetResolution(config.outputWidth, config.outputHeight);

    SpoutOutput spoutOutput;
    spoutOutput.SetName(config.spoutOutputName);

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

    // Captured BEFORE the CLI config-file load below (if any), so that if it changes
    // artNetAddress/artNetPort/spoutOutputName, the main loop's normal "re-apply only
    // if changed" diff on its first iteration naturally picks up the difference and
    // rebinds ArtNet/renames the Spout sender - rather than silently leaving the
    // sockets bound to the pre-load defaults.
    std::string appliedArtNetAddress = config.artNetAddress;
    int appliedArtNetPort = config.artNetPort;
    std::string appliedSpoutName = config.spoutOutputName;

    // CLI config file passthrough (Loader.cs's ReadCLIConfigFile).
    for (int i = 1; i < argc; ++i) {
        std::string configPath;
        if (!TryParseConfigFileArg(argv[i], configPath)) continue;

        ShowConfig loaded;
        std::string error;
        if (ShowConfig::Load(configPath, loaded, serializerRegistry, exporterRegistry, generatorRegistry, error)) {
            for (auto& exporter : loaded.exporters) exporter->Construct();
            for (auto& generator : loaded.generators) generator->Construct();
            config = std::move(loaded);
            dirty.store(true);
        } else {
            std::fprintf(stderr, "Failed to load CLI config file '%s': %s\n", configPath.c_str(), error.c_str());
        }
        break;
    }

    while (!glfwWindowShouldClose(window)) {
        // Some generators (Fade, Strobe, Time - anything reading the system clock)
        // need to keep animating even with no new ArtNet data - see IGenerator.h's
        // "animation" note. While one is active, tick at targetFramerate instead of
        // the normal idle timeout, and mark every frame dirty; otherwise fall back to
        // the render-on-change idle path (no wakeups, ~0% GPU).
        bool anyAnimatedGenerator = false;
        for (const auto& generator : config.generators) {
            if (generator->IsAnimated()) {
                anyAnimatedGenerator = true;
                break;
            }
        }
        double waitTimeout = anyAnimatedGenerator ? 1.0 / std::max(1, config.targetFramerate) : 0.25;

        // Blocks until input arrives, glfwPostEmptyEvent() is called (by the ArtNet
        // thread above), or the timeout elapses.
        glfwWaitEventsTimeout(waitTimeout);
        if (glfwWindowShouldClose(window)) break;
        if (anyAnimatedGenerator) dirty.store(true);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        UiPanelResult ui = DrawUiPanel(config, serializerRegistry, exporterRegistry, generatorRegistry,
                                        frameRenderer.TextureId(), artNetConnected.load());
        if (ui.configChanged) dirty.store(true);

        HWND hwnd = glfwGetWin32Window(window);

        if (ui.requestSave) {
            std::wstring path;
            if (ShowSaveFileDialog(hwnd, "Show Configurations", "shwcfg", "NewShowConfig.shwcfg", path)) {
                std::string error;
                if (!config.Save(WideToNarrow(path), error)) {
                    std::fprintf(stderr, "Save failed: %s\n", error.c_str());
                }
            }
        }
        if (ui.requestLoad) {
            std::wstring path;
            if (ShowOpenFileDialog(hwnd, "Show Configurations", "shwcfg", path)) {
                std::string error;
                ShowConfig loaded;
                if (ShowConfig::Load(WideToNarrow(path), loaded, serializerRegistry, exporterRegistry,
                                      generatorRegistry, error)) {
                    // Mirrors Loader.cs's UnloadShowConf()/LoadShowConf(): deconstruct
                    // the outgoing exporters/generators, construct the incoming ones.
                    for (auto& exporter : config.exporters) exporter->Deconstruct();
                    for (auto& generator : config.generators) generator->Deconstruct();
                    for (auto& exporter : loaded.exporters) exporter->Construct();
                    for (auto& generator : loaded.generators) generator->Construct();
                    config = std::move(loaded);
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
            frameRenderer.Render(dmxBuffer, config.generators, *config.serializer, config.maskedChannels,
                                  config.invertMask, config.autoMaskOnZero,
                                  config.serializeUniverseCount);
            spoutOutput.SendFrame(reinterpret_cast<const uint8_t*>(frameRenderer.Pixels().data()),
                                   static_cast<unsigned int>(frameRenderer.Width()),
                                   static_cast<unsigned int>(frameRenderer.Height()));
            for (auto& exporter : config.exporters) {
                exporter->FrameRendered(frameRenderer.Pixels(), frameRenderer.Width(), frameRenderer.Height());
            }
        }

        // Unlike the Spout/preview path above, exporters need a steady per-frame tick
        // regardless of whether DMX data actually changed (e.g. MIDIDMX's watchdog,
        // TimeCodeExporter's UDP broadcast - see IExporter.h's cadence note) - so this
        // runs every loop iteration, not just on `dirty`. glfwWaitEventsTimeout(0.25)
        // above bounds this to a ~4Hz minimum even when fully idle, enough to satisfy
        // e.g. MIDIDMX.cs's 1-second watchdog timeout without spinning the GPU.
        for (auto& exporter : config.exporters) {
            exporter->InitFrame(frameRenderer.MergedDmx());
            exporter->CompleteFrame(frameRenderer.MergedDmx());
        }

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
    for (auto& exporter : config.exporters) exporter->Deconstruct();
    for (auto& generator : config.generators) generator->Deconstruct();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
