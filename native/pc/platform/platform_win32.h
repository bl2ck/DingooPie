#ifndef DINGOO_PIE_PLATFORM_WIN32_H
#define DINGOO_PIE_PLATFORM_WIN32_H

#include <string>
#include <vector>
#include <stdio.h>
#include <stdint.h>

std::vector<std::string> platformCommandLineArguments(int argc, char* argv[]);
std::string platformSelectAppPath(void);
std::string platformSelectAppPathLocalized(const wchar_t* title, const wchar_t* filter);
bool platformFileExists(const std::string& path);
FILE* platformOpenHostFile(const std::string& path, const char* mode);
bool platformProbeGameHeader(const std::string& path);
bool platformChangeToAppDirectory(const std::string& appPath);
bool platformChangeToGameDirectory(const std::string& gamePath);
FILE* platformOpenGameFile(const std::string& path);
FILE* platformOpenGameSiblingFile(const std::string& gamePath, const std::string& fileName);
std::string platformGetAppSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity);
std::string platformGetCcSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity);
std::string platformGetLogDirectory(void);
bool platformIsPrivateStorageDirectory(const std::string& directoryUri);
FILE* platformOpenStorageFile(const std::string& directoryUri,
    const std::string& fileName, const char* mode);
bool platformDeleteStorageFile(const std::string& directoryUri, const std::string& fileName);
uint64_t platformGetStorageFileModifiedTime(const std::string& directoryUri,
    const std::string& fileName);
void platformBeginHighResolutionTiming(void);
void platformEndHighResolutionTiming(void);
std::string platformWideToUtf8(const std::wstring& text);
std::wstring platformUtf8ToWide(const std::string& text);
unsigned long platformWin32NormalizeChildStyle(const wchar_t* className, unsigned long style);

#endif
