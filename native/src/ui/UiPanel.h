#pragma once
// ImGui settings panel covering the serializer-side fields UIController.cs exposes
// (Assets/UIController.cs), a dropdown to pick among SerializerRegistry's serializers
// (matching UIController.cs's serializerDropdown), and the selected serializer's own
// DrawUi() controls (e.g. VRSL's ConstructUserInterface toggles). Deserializer/
// transcode fields are out of scope (Spout input isn't implemented).
#include "../config/ShowConfig.h"
#include "../serializers/SerializerRegistry.h"
#include "../exporters/ExporterRegistry.h"
#include "../generators/GeneratorRegistry.h"

struct UiPanelResult {
    bool configChanged = false;  // any field edited this frame -> caller should mark dirty
    bool requestSave = false;    // Save button pressed -> caller should run the save flow
    bool requestLoad = false;    // Load button pressed -> caller should run the load flow
};

// Draws the panel as ImGui widgets for the current frame (call between
// ImGui::NewFrame() and ImGui::Render()). `config` is edited in place, including
// potentially reassigning `config.serializer` to a different instance owned by
// `serializerRegistry` if the dropdown selection changes, and adding/removing
// entries in `config.exporters` (mirrors Loader.cs's SetupDynamicUI Add/Delete
// closures, including calling the exporter's own Construct()/Deconstruct() at the
// moment it's added/removed - the caller does not need to do this separately).
// `previewTextureId`, if non-zero, is shown as a live preview of the rendered frame
// (the GL texture FrameRenderer maintains).
UiPanelResult DrawUiPanel(ShowConfig& config, SerializerRegistry& serializerRegistry,
                           const ExporterRegistry& exporterRegistry, const GeneratorRegistry& generatorRegistry,
                           unsigned int previewTextureId, bool artNetConnected);
