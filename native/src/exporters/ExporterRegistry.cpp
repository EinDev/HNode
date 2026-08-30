#include "ExporterRegistry.h"

#include "MidiDmxExporter.h"
#include "TextFileExporter.h"

std::vector<std::string> ExporterRegistry::Names() const {
    return {"MIDIDMX", "TextFileExporter"};
}

std::unique_ptr<IExporter> ExporterRegistry::Create(const std::string& name) const {
    if (name == "MIDIDMX") return std::make_unique<MidiDmxExporter>();
    if (name == "TextFileExporter") return std::make_unique<TextFileExporter>();
    return nullptr;
}
