#include "UiPanel.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_stdlib.h"

namespace {

// Generic "dynamic list" UI shared by exporters and generators (both are a
// vector<unique_ptr<TInterface>> with Name()/DrawUi()/Construct()/Deconstruct() and a
// registry with Names()/Create()) - mirrors Loader.cs's SetupDynamicUI add/remove
// closures, including calling the item's own Construct()/Deconstruct() at the moment
// it's added/removed. Returns true if anything changed (an item's own settings, or
// the list itself via add/remove) - the caller should mark the frame dirty when true.
template <typename TInterface, typename TRegistry>
bool DrawDynamicList(const char* sectionLabel, const char* addComboLabel,
                      std::vector<std::unique_ptr<TInterface>>& items, const TRegistry& registry) {
    bool changed = false;
    ImGui::Text("%s", sectionLabel);

    int removeIndex = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        TInterface* item = items[i].get();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::CollapsingHeader(item->Name(), ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= item->DrawUi();
            if (ImGui::Button("Remove")) removeIndex = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        items[static_cast<size_t>(removeIndex)]->Deconstruct();
        items.erase(items.begin() + removeIndex);
        changed = true;
    }

    static int newIndex = 0; // one instantiation per TInterface -> separate state for exporters vs. generators
    std::vector<std::string> names = registry.Names();
    if (!names.empty()) {
        if (newIndex >= static_cast<int>(names.size())) newIndex = 0;
        ImGui::PushID(addComboLabel);
        if (ImGui::BeginCombo(addComboLabel, names[static_cast<size_t>(newIndex)].c_str())) {
            for (size_t i = 0; i < names.size(); ++i) {
                bool isSelected = static_cast<int>(i) == newIndex;
                if (ImGui::Selectable(names[i].c_str(), isSelected)) newIndex = static_cast<int>(i);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add")) {
            auto item = registry.Create(names[static_cast<size_t>(newIndex)]);
            if (item) {
                item->Construct();
                items.push_back(std::move(item));
                changed = true;
            }
        }
        ImGui::PopID();
    }

    return changed;
}

} // namespace

UiPanelResult DrawUiPanel(ShowConfig& config, SerializerRegistry& serializerRegistry,
                           const ExporterRegistry& exporterRegistry, const GeneratorRegistry& generatorRegistry,
                           unsigned int previewTextureId, bool artNetConnected) {
    UiPanelResult result;
    bool changed = false;

    // Pin the panel to fill the app's own window instead of floating as a movable/
    // resizable sub-window inside it - this is the only window this app has, so it
    // should just look like "the window", not a widget floating in one.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("HNode", nullptr,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus |
                      ImGuiWindowFlags_NoSavedSettings);

    // --- ArtNet status ---
    if (artNetConnected) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "ArtNet: receiving");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "ArtNet: no signal");
    }

    ImGui::Separator();

    // --- Preview ---
    if (previewTextureId != 0) {
        ImGui::Text("Preview");
        ImGui::Image((ImTextureID)(intptr_t)previewTextureId, ImVec2(320.0f, 180.0f));
        ImGui::Separator();
    }

    // --- Serializer (UIController.cs Start()) settings ---
    ImGui::Text("Serializer Settings");

    changed |= ImGui::InputInt("Serialize Universe Count", &config.serializeUniverseCount);
    changed |= ImGui::Checkbox("Invert Mask", &config.invertMask);
    changed |= ImGui::Checkbox("Auto Mask On Zero", &config.autoMaskOnZero);
    changed |= ImGui::InputText("Spout Output Name", &config.spoutOutputName);
    changed |= ImGui::InputInt("ArtNet Port", &config.artNetPort);
    changed |= ImGui::InputText("ArtNet Address", &config.artNetAddress);

    // --- Serializer selection (UIController.cs serializerDropdown) ---
    if (!config.serializer) config.serializer = serializerRegistry.Default();
    if (ImGui::BeginCombo("Serializer", config.serializer->Name())) {
        for (const auto& candidate : serializerRegistry.All()) {
            bool isSelected = candidate.get() == config.serializer;
            if (ImGui::Selectable(candidate->Name(), isSelected)) {
                config.serializer = candidate.get();
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    // --- Selected serializer's own settings (e.g. SerializerVRSL.cs's
    // ConstructUserInterface) ---
    ImGui::Text("%s Settings", config.serializer->Name());
    changed |= config.serializer->DrawUi();

    ImGui::Separator();

    // --- Generators (dynamic list, mirrors ShowConfiguration.cs's Generators) ---
    // Drawn before exporters to match the C# pipeline order (generators run before
    // the serializer each frame - see FrameRenderer::Render).
    changed |= DrawDynamicList("Generators", "Add Generator", config.generators, generatorRegistry);

    ImGui::Separator();

    // --- Exporters (dynamic list, mirrors ShowConfiguration.cs's Exporters +
    // Loader.cs's SetupDynamicUI add/remove/reorder InterfaceList) ---
    // Exporters tick every loop iteration regardless of `dirty` (see IExporter.h), so
    // an exporter-only UI change doesn't strictly need to mark the frame dirty - but
    // it's included in `changed` anyway since it's harmless and keeps the preview/
    // Spout output in sync immediately rather than waiting for the next tick.
    changed |= DrawDynamicList("Exporters", "Add Exporter", config.exporters, exporterRegistry);

    ImGui::Separator();

    // --- Save / Load ---
    if (ImGui::Button("Save")) {
        result.requestSave = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        result.requestLoad = true;
    }

    ImGui::End();

    result.configChanged = changed;
    return result;
}
