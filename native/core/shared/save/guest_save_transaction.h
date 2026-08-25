#ifndef DINGOO_PIE_SHARED_SAVE_GUEST_SAVE_TRANSACTION_H
#define DINGOO_PIE_SHARED_SAVE_GUEST_SAVE_TRANSACTION_H

#include <stdio.h>
#include <string>

FILE* guestSaveOpenFile(const std::string& directory, const char* name,
    const char* mode, bool* transactional);
int guestSaveCloseFile(const std::string& directory, const char* name,
    FILE* file, bool transactional);

#endif
