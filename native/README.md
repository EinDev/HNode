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
watchdog, TimeCodeExporter's UDP broadcast) need a steady heartbeat independent of DMX
churn.

The Unity project (everything outside `native/`) is untouched and still builds/works
as before. This is an additional, independent app living alongside it.

## What's implemented
- ArtNet DMX receive over UDP (`src/artnet`), configurable address/port, plus
  auto-discovery: answers `ArtPoll` with a real `ArtPollReply` (unicast) so consoles
  like QLC+ find HNode on the network without typing in its IP - new behavior, not a
  port (neither the Unity app nor the ArtNet-Unity library it depends on does this)
- Per-universe DMX buffer + merge (`src/dmx`)
- Channel masking (`maskedChannels` / `invertMask` / `autoMaskOnZero`)
- All 7 serializers (`src/serializers`), selectable at runtime via `ISerializer` +
  `SerializerRegistry`: VRSL, Binary, ColorBinary, Ternary, Spiral, MDMX (aka
  BinaryStageFlight), FuralitySomna
- All 14 generators (`src/generators`), as a dynamic add/remove list via `IGenerator`
  + `GeneratorRegistry`: StaticValue, Remap, RemapOnDemand, Snapshot, Fade, Strobe,
  DMXPacket, Text, Time, SRT, LRC, ASS, TwitchChat (anonymous read-only IRC,
  hand-rolled against raw Winsock - no equivalent to the C# reference's
  Lexone.UnityTwitchChat dependency), OnTime (polls the local OnTime show-timer app's
  HTTP API via WinHTTP from a background thread, unlike the C# reference's
  blocking-per-frame HTTP call), and MAVLinkDroneNetwork (`src/generators/mavlink`) -
  simulates a MAVLink drone-show network (Skybrush-server-style) over UDP: HEARTBEAT/
  GPS/SYS_STATUS telemetry, capability/parameter/mission/FTP command handling, and
  per-drone position (+ LED color or pyro state) packed into DMX. Uses the vendored
  `mavlink/c_library_v2` headers (`third_party/mavlink`) instead of hand-rolling
  packet framing/CRC tables - verified end-to-end against a real `pymavlink` client
  (heartbeat, commands/ACK, capability negotiation, param set/read, and FTP with a
  bit-exact CRC32 match). Show-file/trajectory/light-program/pyro playback (the
  "FTP protocol, trajectories, show files" mini-project mentioned below) isn't
  implemented yet - FTP uploads are accepted and buffered but not parsed, so drones
  currently only report GPS-set positions, not scripted show trajectories. Animated
  generators (Fade/Strobe/Time/SRT/LRC/ASS/TwitchChat/OnTime/MAVLinkDroneNetwork) keep
  the render-on-change loop ticking at `targetFramerate` while active instead of
  freezing when ArtNet goes idle - see `IGenerator.h`.
- All 4 exporters (`src/exporters`), as a dynamic add/remove list via `IExporter` +
  `ExporterRegistry`: MIDIDMX (VRC-MIDIDMX protocol over winmm, replacing DryWetMidi),
  TextFileExporter, FrameSnapshotExporter (UDP JSON command -> PNG snapshot via
  vendored stb_image_write, JSON responses via nlohmann-json), and TimeCodeExporter
  (MIDI Time Code input via winmm - both quarter-frame and full-frame SysEx - rebroadcast
  as a UDP packet on 5 fixed loopback ports; framerate is parsed but never transmitted,
  matching the C# original's dead code there)
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
- MAVLinkDroneNetwork's show-file playback: the FTP upload path, show-file container
  parsing, trajectory (Bezier) decoding, light-program bytecode, and pyro event
  decoding are all still unimplemented - see `src/generators/mavlink/Drone.h`'s
  `showFile_` (always null for now). The MAVLink transport/telemetry/command layer
  itself is implemented (see above).
- The DMX preview/chroma-key window
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

## Performance baseline
`build_bench.bat` builds a standalone benchmark (`native/src/bench/BenchMain.cpp`)
measuring the DMX merge + VRSL serialize hot path - the same code
`FrameRenderer::Render()` runs every dirty frame, minus the final GL texture upload.
It has zero external dependencies (no vcpkg needed), so it's fast to build and runs
as its own parallel CI job on every push. Because CI runners are shared,
variable-speed machines, the benchmark times a fixed calibration workload on the same
machine in the same run and reports the *ratio* of actual-work-time to
calibration-time - track that ratio over time (visible in each run's job summary),
not the raw millisecond figures, which only mean something when comparing runs on one
fixed machine.

## Third-party code
`third_party/Spout` is vendored, unmodified source from the
[Spout2](https://github.com/leadedge/Spout2) SDK (`SPOUTSDK/SpoutGL`), used the same
way the Unity build's KlakSpout package uses it internally. See
`third_party/Spout/LICENSE`.
