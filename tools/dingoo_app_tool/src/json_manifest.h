#pragma once

#include "app_format.h"

#include <string>

namespace dingoo {

struct Manifest {
    AppImage image;
    std::string originalImagePath;
    std::string rawPayloadPath;
};

std::string writeManifest(const AppImage& image, const std::string& originalImagePath, const std::string& rawPayloadPath);
Manifest readManifest(const std::string& text);

} // namespace dingoo
