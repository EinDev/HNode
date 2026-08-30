#pragma once
// Factory for exporter types - unlike SerializerRegistry (persistent, single-selection
// instances), exporters are a dynamic list (see IExporter.h): ShowConfig owns zero or
// more independently-configured instances, so this registry only knows how to name
// the available types and construct a fresh instance of one, mirroring Loader.cs's
// `GetAllInterfaceImplementations<IExporter>()` reflection scan plus
// `Activator.CreateInstance(type)` in the add-exporter UI callback.
#include <memory>
#include <string>
#include <vector>
#include "IExporter.h"

class ExporterRegistry {
public:
    // Names of all exporter types available to add, in UI/menu order.
    std::vector<std::string> Names() const;

    // Constructs a fresh instance by name (exact match against each type's Name()).
    // Returns nullptr if `name` isn't a known exporter type.
    std::unique_ptr<IExporter> Create(const std::string& name) const;
};
