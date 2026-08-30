#pragma once
// ImGui settings panel covering the serializer-side fields UIController.cs exposes
// (Assets/UIController.cs), a dropdown to pick among SerializerRegistry's serializers
// (matching UIController.cs's serializerDropdown), and the selected serializer's own
// DrawUi() controls (e.g. VRSL's ConstructUserInterface toggles). Deserializer/
// transcode fields are out of scope (Spout input isn't implemented).
#include "../config/ShowConfig.h"
#include "../serializers/SerializerRegistry.h"
#include "../exporters/MidiDmxExporter.h"

struct UiPanelResult {
    bool configChanged = false;       // any field edited this frame -> caller should mark dirty
    bool requestSave = false;         // Save button pressed -> caller should run the save flow
    bool requestLoad = false;         // Load button pressed -> caller should run the load flow
    bool requestMidiReconnect = false; // "Reconnect MIDI Device" pressed (MIDIDMX.cs ConstructUserInterface)
};

// Draws the panel as ImGui widgets for the current frame (call between
// ImGui::NewFrame() and ImGui::Render()). `config` is edited in place, including
// potentially reassigning `config.serializer` to a different instance owned by
// `registry` if the dropdown selection changes. `midi`'s editable fields
// (midiDeviceName/channelsPerUpdate/idleScanChannels) are edited in place too - the
// caller is responsible for actually calling midi.Reconnect() when
// requestMidiReconnect is set (mirrors MIDIDMX.cs's ConstructUserInterface "Reconnect
// MIDI Device" button, which just triggers MidiConnectDevice()). `previewTextureId`,
// if non-zero, is shown as a live preview of the rendered frame (the GL texture
// FrameRenderer maintains).
UiPanelResult DrawUiPanel(ShowConfig& config, SerializerRegistry& registry, MidiDmxExporter& midi,
                           unsigned int previewTextureId, bool artNetConnected);
