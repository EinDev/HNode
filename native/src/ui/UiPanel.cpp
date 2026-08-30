#include "UiPanel.h"

#include <cstdint>

#include "imgui.h"
#include "imgui_stdlib.h"

UiPanelResult DrawUiPanel(ShowConfig& config, unsigned int previewTextureId, bool artNetConnected) {
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

    ImGui::Separator();

    // --- VRSL serializer settings (SerializerVRSL.cs ConstructUserInterface) ---
    ImGui::Text("VRSL Settings");

    changed |= ImGui::Checkbox("Gamma Correction", &config.serializer.gammaCorrection);
    changed |= ImGui::Checkbox("RGB Grid Mode", &config.serializer.rgbGridMode);

    ImGui::Text("Output Config: %s", VrslSerializer::ToString(config.serializer.outputConfig));
    if (ImGui::Button("Cycle Output Config")) {
        config.serializer.outputConfig = VrslSerializer::CycleNext(config.serializer.outputConfig);
        changed = true;
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
