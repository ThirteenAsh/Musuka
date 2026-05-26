#pragma once

#include "Models.h"

#include <string>

namespace musuka {

class DesktopScanner {
public:
    bool ScanAndPrepare(AppConfig& config, std::wstring& error, std::wstring& warning);

private:
    void InitializeObjectImages(DesktopObject& object, std::wstring& warning);
};

} // namespace musuka

