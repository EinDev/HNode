#pragma once
// ImGui settings panel covering the serializer-side fields UIController.cs exposes
// (Assets/UIController.cs) plus VRSL's own ConstructUserInterface controls
// (Assets/Plugin/Serializers/SerializerVRSL.cs). Deserializer/transcode fields are
// out of scope for Phase 1 (Spout input isn't implemented).
#include "../config/ShowConfig.h"

struct UiPanelResult {
    bool configChanged = false;  // any field edited this frame -> caller should mark dirty
    bool requestSave = false;    // Save button pressed -> caller should run the save flow
    bool requestLoad = false;    // Load button pressed -> caller should run the load flow
};

// Draws the panel as ImGui widgets for the current frame (call between
// ImGui::NewFrame() and ImGui::Render()). `config` is edited in place.
// `previewTextureId`, if non-zero, is shown as a live preview of the rendered frame
// (the GL texture FrameRenderer maintains).
UiPanelResult DrawUiPanel(ShowConfig& config, unsigned int previewTextureId, bool artNetConnected);
