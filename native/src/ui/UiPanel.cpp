#include "UiPanel.h"

#include <cstdint>

#include "imgui.h"
#include "imgui_stdlib.h"

UiPanelResult DrawUiPanel(ShowConfig& config, SerializerRegistry& serializerRegistry,
                           const ExporterRegistry& exporterRegistry, unsigned int previewTextureId,
                           bool artNetConnected) {
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

    // --- Exporters (dynamic list, mirrors ShowConfiguration.cs's Exporters +
    // Loader.cs's SetupDynamicUI add/remove/reorder InterfaceList) ---
    ImGui::Text("Exporters");

    int removeIndex = -1;
    for (size_t i = 0; i < config.exporters.size(); ++i) {
        IExporter* exporter = config.exporters[i].get();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::CollapsingHeader(exporter->Name(), ImGuiTreeNodeFlags_DefaultOpen)) {
            exporter->DrawUi();
            if (ImGui::Button("Remove")) removeIndex = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        // Deconstruct() before dropping the instance, matching Loader.cs's Delete<T>
        // closure (item.DeconstructUserInterface() + item.Deconstruct() before RemoveAt).
        config.exporters[static_cast<size_t>(removeIndex)]->Deconstruct();
        config.exporters.erase(config.exporters.begin() + removeIndex);
    }

    static int newExporterIndex = 0;
    std::vector<std::string> exporterNames = exporterRegistry.Names();
    if (!exporterNames.empty()) {
        if (newExporterIndex >= static_cast<int>(exporterNames.size())) newExporterIndex = 0;
        if (ImGui::BeginCombo("Add Exporter", exporterNames[static_cast<size_t>(newExporterIndex)].c_str())) {
            for (size_t i = 0; i < exporterNames.size(); ++i) {
                bool isSelected = static_cast<int>(i) == newExporterIndex;
                if (ImGui::Selectable(exporterNames[i].c_str(), isSelected)) newExporterIndex = static_cast<int>(i);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add")) {
            auto exporter = exporterRegistry.Create(exporterNames[static_cast<size_t>(newExporterIndex)]);
            if (exporter) {
                exporter->Construct(); // matches Loader.cs's Add<T> closure calling generator.Construct()
                config.exporters.push_back(std::move(exporter));
            }
        }
    }

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
