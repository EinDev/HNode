#include "ExporterRegistry.h"

#include "MidiDmxExporter.h"
#include "TextFileExporter.h"
#include "FrameSnapshotExporter.h"
#include "TimeCodeExporter.h"

std::vector<std::string> ExporterRegistry::Names() const {
    return {"MIDIDMX", "TextFileExporter", "FrameSnapshotExporter", "TimeCodeExporter"};
}

std::unique_ptr<IExporter> ExporterRegistry::Create(const std::string& name) const {
    if (name == "MIDIDMX") return std::make_unique<MidiDmxExporter>();
    if (name == "TextFileExporter") return std::make_unique<TextFileExporter>();
    if (name == "FrameSnapshotExporter") return std::make_unique<FrameSnapshotExporter>();
    if (name == "TimeCodeExporter") return std::make_unique<TimeCodeExporter>();
    return nullptr;
}
