# HNode Native (Phase 1)

A from-scratch native rewrite of the Unity app's core "ArtNet in -> VRSL serializer ->
Spout out" path (the flow documented in the root README's "HNode with OBS" section),
built with GLFW + Dear ImGui + raw OpenGL instead of Unity. The point of this rewrite
is idle GPU usage: the Unity build runs its full engine render loop continuously at
the target framerate even when nothing has changed, which is where its constant
background GPU cost comes from. This app instead only re-serializes DMX, re-uploads
the preview texture, and sends a new Spout frame when something actually changed (new
ArtNet data, or a settings edit) - see the render-on-change loop in `src/main.cpp`.

The Unity project (everything outside `native/`) is untouched and still builds/works
as before. This is an additional, independent app living alongside it.

## What's implemented
- ArtNet DMX receive over UDP (`src/artnet`), configurable address/port
- Per-universe DMX buffer + merge (`src/dmx`)
- Channel masking (`maskedChannels` / `invertMask` / `autoMaskOnZero`)
- The VRSL serializer (`src/serializers`), ported pixel-for-pixel from
  `Assets/Plugin/Serializers/SerializerVRSL.cs`
- CPU pixel buffer -> GL preview texture (`src/render`)
- Spout2 output, using the vendored SDK in `third_party/Spout` (`src/spout`)
- `.shwcfg` YAML load/save (`src/config`), field-compatible with the Unity app's
  `ShowConfiguration.cs` for the fields both share (see the comment at the top of
  `ShowConfig.h` for exactly which fields and simplifications - notably, DMX channel
  ranges only support plain integers or "universe.channel" notation, not the C#
  version's arithmetic-equation syntax)
- An ImGui settings panel (`src/ui`) covering the same serializer-side fields the
  Unity UI exposes, plus Save/Load via native file dialogs

## What's intentionally out of scope for Phase 1
Not dropped by accident - deferred to keep this port reviewable and land it in one
piece. None of the following exist yet:
- Spout **input** / deserialize (the `Transcode` path)
- The other 6 serializers (Binary, ColorBinary, FuralitySomna, MDMX, Spiral, Ternary)
- All 12 generators (Fade, Strobe, Remap x2, Snapshot, StaticValue, Text/SRT/LRC/ASS,
  Time, TwitchChat, OnTime, MAVLinkDrone) and the UI to add/remove/reorder them
- All exporters (FrameSnapshot, MIDIDMX, TextFile, TimeCode)
- The DMX preview/chroma-key window, stats overlay
- `--config-file=` CLI passthrough

## Building
Requires a Visual Studio C++ toolchain (Build Tools or full VS, any recent version
with the "Desktop development with C++" workload) and `git`. No CMake install needed.

```
native\vcpkg\vcpkg.exe install glfw3 "imgui[core,glfw-binding,opengl3-binding]" yaml-cpp --triplet x64-windows
```
(first time only - if `native\vcpkg` doesn't exist yet, clone it first:
`git clone https://github.com/microsoft/vcpkg native\vcpkg && native\vcpkg\bootstrap-vcpkg.bat`)

Then from `native/`:
```
build.bat
```
This produces `native\build\HNode.exe` (plus the `glfw3.dll`/`yaml-cpp.dll` it needs
alongside it - `build.bat` copies them automatically).

CI builds this on every push that touches `native/**` - see
`.github/workflows/native.yml`.

## Third-party code
`third_party/Spout` is vendored, unmodified source from the
[Spout2](https://github.com/leadedge/Spout2) SDK (`SPOUTSDK/SpoutGL`), used the same
way the Unity build's KlakSpout package uses it internally. See
`third_party/Spout/LICENSE`.
