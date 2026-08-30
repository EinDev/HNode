// VrslSerializer::DrawUi() only - split out of VrslSerializer.cpp so that file (the
// actual DMX->pixel algorithm) stays free of any ImGui/vcpkg dependency. See the
// comment at the top of VrslSerializer.cpp for why that matters (the CI performance
// benchmark links VrslSerializer.cpp directly, with zero external dependencies).
#include "VrslSerializer.h"
#include "imgui.h"

bool VrslSerializer::DrawUi() {
    bool changed = false;
    changed |= ImGui::Checkbox("Gamma Correction", &gammaCorrection);
    changed |= ImGui::Checkbox("RGB Grid Mode", &rgbGridMode);
    ImGui::Text("Output Config: %s", ToString(outputConfig));
    if (ImGui::Button("Cycle Output Config")) {
        outputConfig = CycleNext(outputConfig);
        changed = true;
    }
    return changed;
}
