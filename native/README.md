# HNode Native

A from-scratch native rewrite of the Unity app, built with GLFW + Dear ImGui + raw
OpenGL instead of Unity. The point of this rewrite is idle GPU usage: the Unity build
runs its full engine render loop continuously at the target framerate even when
nothing has changed, which is where its constant background GPU cost comes from. This
app instead only re-serializes DMX, re-uploads the preview texture, and sends a new
Spout frame when something actually changed (new ArtNet data, a settings edit, or an
animated generator's per-frame tick - see `IGenerator.h`) - see the render-on-change
loop in `src/main.cpp`. Exporters are the one thing that intentionally ticks every
loop iteration regardless (see `IExporter.h`'s cadence note) - some (MIDIDMX's
watchdog, a future TimeCodeExporter) need a steady heartbeat independent of DMX churn.

The Unity project (everything outside `native/`) is untouched and still builds/works
as before. This is an additional, independent app living alongside it.

## What's implemented
- ArtNet DMX receive over UDP (`src/artnet`), configurable address/port
- Per-universe DMX buffer + merge (`src/dmx`)
- Channel masking (`maskedChannels` / `invertMask` / `autoMaskOnZero`)
- All 7 serializers (`src/serializers`), selectable at runtime via `ISerializer` +
  `SerializerRegistry`: VRSL, Binary, ColorBinary, Ternary, Spiral, MDMX (aka
  BinaryStageFlight), FuralitySomna
- 9 of the 12 generators (`src/generators`), as a dynamic add/remove list via
  `IGenerator` + `GeneratorRegistry`: StaticValue, Remap, RemapOnDemand, Snapshot,
  Fade, Strobe, DMXPacket, Text, Time. Animated generators (Fade/Strobe/Time) keep the
  render-on-change loop ticking at `targetFramerate` while active instead of freezing
  when ArtNet goes idle - see `IGenerator.h`.
- 2 of the 4 exporters (`src/exporters`), as a dynamic add/remove list via `IExporter`
  + `ExporterRegistry`: MIDIDMX (VRC-MIDIDMX protocol over winmm, replacing DryWetMidi)
  and TextFileExporter
- CPU pixel buffer -> GL preview texture (`src/render`)
- Spout2 output, using the vendored SDK in `third_party/Spout` (`src/spout`)
- `.shwcfg` YAML load/save (`src/config`), field-compatible with the Unity app's
  `ShowConfiguration.cs` for the fields both share (see the comment at the top of
  `ShowConfig.h` for exactly which fields and simplifications - notably, DMX channel
  ranges only support plain integers or "universe.channel" notation, not the C#
  version's arithmetic-equation syntax). Each serializer/generator/exporter persists
  its own fields via `ReadYaml`/`WriteYaml`.
- An ImGui settings panel (`src/ui`) covering the serializer-side fields the Unity UI
  exposes, dropdowns/add-remove lists for serializers/generators/exporters, Save/Load
  via native file dialogs (`src/ui/FileDialog.h`)
- `--config-file=` CLI passthrough (`Loader.cs`'s `ReadCLIConfigFile`)

## What's intentionally out of scope (not dropped by accident)
- Spout **input** / deserialize (the `Transcode` path) - `DeserializeChannel` isn't
  implemented on any serializer
- 3 generators: SRT/LRC/ASS subtitle-file playback, TwitchChat (IRC), OnTime (HTTP),
  MAVLinkDrone (its own mini-project: FTP protocol, trajectories, show files)
- 2 exporters: FrameSnapshotExporter (UDP command -> PNG snapshot), TimeCodeExporter
  (MIDI Time Code input -> UDP broadcast)
- The DMX preview/chroma-key window, stats overlay
- FuralitySomnaSerializer's `mergedChannels` field has no UI and isn't persisted to
  YAML yet (config-only in the C# original too, just not wired up here)

## Building
Requires a Visual Studio C++ toolchain (Build Tools or full VS, any recent version
with the "Desktop development with C++" workload) and `git`. No CMake install needed.

```
native\vcpkg\vcpkg.exe install glfw3 "imgui[core,glfw-binding,opengl3-binding]" yaml-cpp nlohmann-json --triplet x64-windows
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
