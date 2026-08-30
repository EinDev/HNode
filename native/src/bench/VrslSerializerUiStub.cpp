// Stub definition of VrslSerializer::DrawUi(), used ONLY by the bench.exe build
// (native/build_bench.bat), never by the real app (which links the actual
// ImGui-based implementation in native/src/serializers/VrslSerializerUi.cpp instead -
// build.bat's source glob doesn't include this src/bench/ directory, so there's no
// risk of both definitions ending up in the same binary).
//
// DrawUi() is a virtual override, so *some* definition must exist wherever a
// VrslSerializer is actually instantiated (the vtable slot has to point somewhere),
// even though bench.exe's benchmark never calls it - this satisfies the linker
// without pulling ImGui/vcpkg into what's meant to be a zero-dependency build.
#include "../serializers/VrslSerializer.h"

bool VrslSerializer::DrawUi() { return false; }
