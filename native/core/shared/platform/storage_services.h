#ifndef DINGOO_PIE_SHARED_PLATFORM_STORAGE_SERVICES_H
#define DINGOO_PIE_SHARED_PLATFORM_STORAGE_SERVICES_H

#include <string>
#include <stdio.h>

bool platformFileExists(const std::string& path);
bool platformChangeToGameDirectory(const std::string& gamePath);
FILE* platformOpenGameFile(const std::string& path);
FILE* platformOpenGameSiblingFile(const std::string& gamePath, const std::string& fileName);
std::string platformGetAppSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity);
std::string platformGetCcSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity);
std::string platformGetLogDirectory(void);
bool platformIsPrivateStorageDirectory(const std::string& directoryUri);
FILE* platformOpenStorageFile(const std::string& directoryUri, const std::string& fileName, const char* mode);
bool platformDeleteStorageFile(const std::string& directoryUri, const std::string& fileName);
uint64_t platformGetStorageFileModifiedTime(const std::string& directoryUri,
    const std::string& fileName);
std::string platformWideToUtf8(const std::wstring& text);

#endif
