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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "dmx/DmxBuffer.h"
#include "artnet/ArtNetReceiver.h"
#include "serializers/SerializerRegistry.h"
#include "render/FrameRenderer.h"
#include "spout/SpoutOutput.h"
#include "spout/SpoutSendThread.h"
#include "spout/SpoutReceiveThread.h"
#include "exporters/ExporterRegistry.h"
#include "generators/GeneratorRegistry.h"
#include "config/ShowConfig.h"
#include "ui/UiPanel.h"
#include "ui/FileDialog.h"
#include "ui/PerfStats.h"
#include "ui/Toast.h"

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

// State persistence across restarts (the old Unity app remembered the selected
// serializer via PlayerPrefs - this does the equivalent for the whole show config):
// auto-saved on exit, auto-loaded on the next startup unless a CLI --config-file=
// was given, which always takes priority. %APPDATA% is used (not next to the exe)
// since HNode.exe may be running from a read-only location (e.g. Program Files).
std::string AutosavePath() {
    const char* appData = std::getenv("APPDATA");
    if (!appData || !*appData) return {};
    std::filesystem::path dir = std::filesystem::path(appData) / "HNode";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec); // best-effort; Load/Save below will just fail harmlessly if this didn't work
    return (dir / "autosave.shwcfg").string();
}

} // namespace

int main(int argc, char** argv) {
    // Toasts aren't wired up yet this early (no ImGui context exists until after
    // glfwInit()/window creation below), so a GLFW init-time error still has to go to
    // stderr - that's fine, an error this early means the app never gets to a visible
    // window at all anyway, unlike the Save/Load/CLI-load failures below which happen
    // once the UI is already up and running.
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

    // Hidden window whose only purpose is to own a second GL context sharing objects
    // (textures) with the main one above, so SpoutSendThread can send frames without
    // ever needing the main thread's context - see SpoutSendThread.h for why.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* spoutContextWindow = glfwCreateWindow(1, 1, "", nullptr, window);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    if (!spoutContextWindow) {
        glfwTerminate();
        return 1;
    }

    // Same idea, a second/separate hidden shared-context window for
    // SpoutReceiveThread (Spout input/Transcode) - each Spout worker thread needs its
    // OWN context, they can't share one between them since both make their context
    // current on different threads simultaneously.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* spoutInputContextWindow = glfwCreateWindow(1, 1, "", nullptr, window);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    if (!spoutInputContextWindow) {
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    ToastQueue toasts;

    SerializerRegistry serializerRegistry;
    ExporterRegistry exporterRegistry;
    GeneratorRegistry generatorRegistry;

    ShowConfig config; // starts from defaults (no exporters, VRSL serializer); use Load to open a .shwcfg.
    config.serializer = serializerRegistry.Default();
    config.deserializer = serializerRegistry.Default();

    DmxBuffer dmxBuffer;
    FrameRenderer frameRenderer;
    frameRenderer.SetResolution(config.outputWidth, config.outputHeight);

    SpoutSendThread spoutSendThread;
    spoutSendThread.Start(spoutContextWindow);
    spoutSendThread.SetName(config.spoutOutputName);

    std::atomic<bool> dirty{true};
    std::atomic<bool> artNetConnected{false};
    std::atomic<uint64_t> artNetPacketCount{0}; // for the "nerdy stats" packets/sec readout

    // Spout input ("Transcode") - only actually polls/receives while
    // config.transcode is true (see SpoutReceiveThread.h); starts either way so it's
    // ready to go the moment Transcode is turned on in the UI/config.
    SpoutReceiveThread spoutReceiveThread;
    spoutReceiveThread.SetCallback([&dirty, window]() {
        dirty.store(true);
        glfwPostEmptyEvent();
    });
    spoutReceiveThread.Start(spoutInputContextWindow);
    spoutReceiveThread.SetName(config.spoutInputName);
    spoutReceiveThread.SetEnabled(config.transcode);
    // Latest frame the main thread has picked up from the receive thread - persists
    // across loop iterations (a fresh frame doesn't arrive every iteration) since
    // TranscodeInput.pixels needs SOMETHING to deserialize against even between new
    // Spout frames, same as how the C# reference's `reader.dmxData` just holds the
    // last-computed transcode result until TextureReader.Update() runs again.
    std::vector<RGBA8> latestSpoutInputFrame;
    int latestSpoutInputWidth = 0;
    int latestSpoutInputHeight = 0;

    PerfStats perfStats;
    auto lastRenderTime = std::chrono::steady_clock::now();
    auto lastPacketRateSampleTime = std::chrono::steady_clock::now();
    uint64_t lastPacketRateSampleCount = 0;

    ArtNetReceiver artnet;
    artnet.SetCallback([&](uint16_t universe, const uint8_t* data, size_t length) {
        dmxBuffer.SetUniverse(universe, data, length);
        artNetConnected.store(true);
        artNetPacketCount.fetch_add(1, std::memory_order_relaxed);
        dirty.store(true);
        // Wake the (otherwise event-driven) main thread promptly instead of waiting
        // out its poll timeout - this is what lets the render-on-change loop react to
        // a live show without spinning.
        glfwPostEmptyEvent();
    });
    artnet.Start(config.artNetAddress, static_cast<uint16_t>(config.artNetPort));
    // Lets consoles (QLC+, etc.) discover HNode via ArtPoll instead of needing its IP
    // typed in by hand - see ArtNetReceiver.h's header comment. The long name embeds
    // the Spout output name so multiple HNode instances are distinguishable in a
    // console's node list; re-applied below whenever spoutOutputName changes.
    artnet.SetNodeInfo("HNode", "HNode (native) - " + config.spoutOutputName);

    // Captured BEFORE the CLI config-file load below (if any), so that if it changes
    // artNetAddress/artNetPort/spoutOutputName, the main loop's normal "re-apply only
    // if changed" diff on its first iteration naturally picks up the difference and
    // rebinds ArtNet/renames the Spout sender - rather than silently leaving the
    // sockets bound to the pre-load defaults.
    std::string appliedArtNetAddress = config.artNetAddress;
    int appliedArtNetPort = config.artNetPort;
    std::string appliedSpoutName = config.spoutOutputName;
    std::string appliedSpoutInputName = config.spoutInputName;
    bool appliedTranscode = config.transcode;

    // CLI config file passthrough (Loader.cs's ReadCLIConfigFile) - takes priority
    // over the autosave restore below if given.
    bool cliConfigLoaded = false;
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
            cliConfigLoaded = true;
        } else {
            toasts.Push(ToastLevel::Error, "Failed to load CLI config file '" + configPath + "': " + error);
        }
        break;
    }

    // Restore state from the previous run (the old Unity app remembered the selected
    // serializer via PlayerPrefs - this is the equivalent for the whole show config),
    // unless a CLI config file was already loaded above.
    const std::string autosavePath = AutosavePath();
    if (!cliConfigLoaded && !autosavePath.empty() && std::filesystem::exists(autosavePath)) {
        ShowConfig loaded;
        std::string error;
        if (ShowConfig::Load(autosavePath, loaded, serializerRegistry, exporterRegistry, generatorRegistry, error)) {
            for (auto& exporter : loaded.exporters) exporter->Construct();
            for (auto& generator : loaded.generators) generator->Construct();
            config = std::move(loaded);
            dirty.store(true);
        } else {
            toasts.Push(ToastLevel::Warning, "Could not restore last session: " + error);
        }
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
                                        frameRenderer.TextureId(), artNetConnected.load(), perfStats);
        if (ui.configChanged) dirty.store(true);

        HWND hwnd = glfwGetWin32Window(window);

        if (ui.requestSave) {
            std::wstring path;
            if (ShowSaveFileDialog(hwnd, "Show Configurations", "shwcfg", "NewShowConfig.shwcfg", path)) {
                std::string error;
                if (!config.Save(WideToNarrow(path), error)) {
                    toasts.Push(ToastLevel::Error, "Save failed: " + error);
                } else {
                    toasts.Push(ToastLevel::Info, "Saved.", 2.5);
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
                    toasts.Push(ToastLevel::Info, "Loaded.", 2.5);
                } else {
                    toasts.Push(ToastLevel::Error, "Load failed: " + error);
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
            spoutSendThread.SetName(config.spoutOutputName);
            artnet.SetNodeInfo("HNode", "HNode (native) - " + config.spoutOutputName);
            appliedSpoutName = config.spoutOutputName;
        }
        if (config.spoutInputName != appliedSpoutInputName) {
            spoutReceiveThread.SetName(config.spoutInputName);
            appliedSpoutInputName = config.spoutInputName;
        }
        if (config.transcode != appliedTranscode) {
            spoutReceiveThread.SetEnabled(config.transcode);
            appliedTranscode = config.transcode;
        }

        frameRenderer.SetResolution(config.outputWidth, config.outputHeight);

        // Pull whatever the receive thread has captured since the last iteration -
        // cheap (a mutex + move) even when nothing new arrived. Only actually matters
        // while Transcode is on; harmless (and near-instantly false) otherwise since
        // SpoutReceiveThread doesn't poll/capture frames while disabled.
        if (spoutReceiveThread.TryGetLatestFrame(latestSpoutInputFrame, latestSpoutInputWidth,
                                                  latestSpoutInputHeight)) {
            if (config.transcode) dirty.store(true);
        }

        if (dirty.exchange(false)) {
            // "Nerdy statistics" (README feature list / TextureWriter.cs's on-screen
            // frame-time text) - measure how long this render actually took and how
            // much wall-clock time elapsed since the last one, to derive a throughput
            // figure the same way TextureWriter.cs did (channels / time).
            auto renderStart = std::chrono::steady_clock::now();

            TranscodeInput transcodeInput;
            transcodeInput.transcode = config.transcode;
            transcodeInput.mergeTranscode = config.mergeTranscode;
            transcodeInput.deserializer = config.deserializer;
            transcodeInput.universeCount = config.transcodeUniverseCount;
            transcodeInput.pixels = &latestSpoutInputFrame;
            transcodeInput.width = latestSpoutInputWidth;
            transcodeInput.height = latestSpoutInputHeight;

            frameRenderer.Render(dmxBuffer, config.generators, *config.serializer, config.maskedChannels,
                                  config.invertMask, config.autoMaskOnZero,
                                  config.serializeUniverseCount, transcodeInput);
            auto renderEnd = std::chrono::steady_clock::now();

            // Flush before handing off to the send thread - required so its context
            // (sharing GL objects with this one, but on another thread) is guaranteed
            // to see the finished texture upload above. See SpoutSendThread::SubmitFrame.
            glFlush();
            spoutSendThread.SubmitFrame(frameRenderer.TextureId(), static_cast<unsigned int>(frameRenderer.Width()),
                                         static_cast<unsigned int>(frameRenderer.Height()));
            for (auto& exporter : config.exporters) {
                exporter->FrameRendered(frameRenderer.Pixels(), frameRenderer.Width(), frameRenderer.Height());
            }

            float renderMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
            perfStats.PushRenderSample(renderMs, frameRenderer.MergedDmx().size());

            double sinceLastRenderSeconds = std::chrono::duration<double>(renderEnd - lastRenderTime).count();
            lastRenderTime = renderEnd;
            if (sinceLastRenderSeconds > 0.001) { // guard against a near-zero delta producing a bogus spike
                perfStats.dataThroughputBytesPerSecond =
                    static_cast<double>(frameRenderer.MergedDmx().size()) / sinceLastRenderSeconds;
            }
        }

        // Sample the ArtNet packet rate roughly twice a second, independent of the
        // render-on-change cadence above (packets can arrive with no actual value
        // change, which still counts toward this rate even when nothing re-renders).
        {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - lastPacketRateSampleTime).count();
            if (elapsed >= 0.5) {
                uint64_t count = artNetPacketCount.load(std::memory_order_relaxed);
                perfStats.artNetPacketsPerSecond = static_cast<double>(count - lastPacketRateSampleCount) / elapsed;
                lastPacketRateSampleCount = count;
                lastPacketRateSampleTime = now;
            }
        }

        // Unlike the Spout/preview path above, exporters need a steady per-frame tick
        // regardless of whether DMX data actually changed (e.g. MIDIDMX's watchdog,
        // TimeCodeExporter's UDP broadcast - see IExporter.h's cadence note) - so this
        // runs every loop iteration, not just on `dirty`. glfwWaitEventsTimeout(0.25)
        // above bounds this to a ~4Hz minimum even when fully idle, enough to satisfy
        // e.g. MIDIDMX.cs's 1-second watchdog timeout without spinning the GPU.
        for (auto& exporter : config.exporters) {
            // Idempotent, cheap (a std::function reassignment) - runs every iteration
            // so a newly added/loaded exporter (e.g. FrameSnapshotExporter) picks this
            // up within one frame without needing a hook at every add/load call site.
            exporter->SetDirtyCallback([&dirty, window]() {
                dirty.store(true);
                glfwPostEmptyEvent();
            });
            exporter->InitFrame(frameRenderer.MergedDmx());
            exporter->CompleteFrame(frameRenderer.MergedDmx());
        }

        toasts.Draw();

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Best-effort - a failed autosave on exit isn't worth surfacing to the user (no
    // UI left to show a toast in at this point anyway).
    if (!autosavePath.empty()) {
        std::string error;
        config.Save(autosavePath, error);
    }

    artnet.Stop();
    // Joins the send/receive threads (releasing their Spout sender/receiver and GL
    // contexts) before the shared context windows they depend on are destroyed below.
    spoutSendThread.Stop();
    spoutReceiveThread.Stop();
    for (auto& exporter : config.exporters) exporter->Deconstruct();
    for (auto& generator : config.generators) generator->Deconstruct();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(spoutContextWindow);
    glfwDestroyWindow(spoutInputContextWindow);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
