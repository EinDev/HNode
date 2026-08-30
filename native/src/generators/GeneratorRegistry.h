#pragma once
// Factory for generator types - dynamic list, same rationale as exporters/ExporterRegistry.h.
#include <memory>
#include <string>
#include <vector>
#include "IGenerator.h"

class GeneratorRegistry {
public:
    std::vector<std::string> Names() const;
    std::unique_ptr<IGenerator> Create(const std::string& name) const;
};
