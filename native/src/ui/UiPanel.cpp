#include "UiPanel.h"

#include <cstdint>

#include "imgui.h"
#include "imgui_stdlib.h"

UiPanelResult DrawUiPanel(ShowConfig& config, SerializerRegistry& registry, MidiDmxExporter& midi,
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
    if (!config.serializer) config.serializer = registry.Default();
    if (ImGui::BeginCombo("Serializer", config.serializer->Name())) {
        for (const auto& candidate : registry.All()) {
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

    // --- MIDIDMX exporter (Assets/Plugin/Exporters/MIDIDMX.cs ConstructUserInterface) ---
    ImGui::Text("MIDIDMX Exporter");
    ImGui::TextColored(midi.IsConnected() ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                        midi.IsConnected() ? "MIDI: connected" : "MIDI: disconnected");
    ImGui::InputText("MIDI Device", &midi.midiDeviceName);
    ImGui::InputInt("Channels Per Update", &midi.channelsPerUpdate);
    ImGui::InputInt("Idle Scan Channels", &midi.idleScanChannels);
    if (ImGui::Button("Reconnect MIDI Device")) {
        result.requestMidiReconnect = true;
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
