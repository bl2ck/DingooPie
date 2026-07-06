#ifndef DINGOO_PIE_PLATFORM_WIN32_H
#define DINGOO_PIE_PLATFORM_WIN32_H

#include <string>

std::string platformCommandLineAppPath(int argc, char* argv[]);
std::string platformSelectAppPath(void);
std::string platformSelectAppPathLocalized(const wchar_t* title, const wchar_t* filter);
bool platformFileExists(const std::string& path);
bool platformProbeAppHeader(const std::string& path);
bool platformChangeToAppDirectory(const std::string& appPath);
void platformBeginHighResolutionTiming(void);
void platformEndHighResolutionTiming(void);
std::string platformWideToUtf8(const std::wstring& text);
std::wstring platformUtf8ToWide(const std::string& text);
unsigned long platformWin32NormalizeChildStyle(const wchar_t* className, unsigned long style);

#endif
