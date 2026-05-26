#pragma once

#include "Models.h"

#include <string>

namespace musuka {

class ConfigStore {
public:
    bool Load(AppConfig& config, std::wstring& warning);
    bool Save(const AppConfig& config, std::wstring& error) const;
};

} // namespace musuka

