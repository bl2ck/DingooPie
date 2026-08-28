#pragma once

#include "shared/services/guest_package.h"

#include <string>
#include <vector>

struct RuntimePackageResourceLookup
{
    std::string name;
    uint32_t offset;
    uint32_t size;
};

void buildAppPackageResourceLookup(const GuestPackage* package,
    std::vector<RuntimePackageResourceLookup>* output);
