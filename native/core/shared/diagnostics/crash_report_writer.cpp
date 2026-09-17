#include "shared/diagnostics/crash_report_writer.h"

#include "shared/platform/storage_services.h"

#include <stdarg.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__ANDROID__)
#include <sys/sysinfo.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

static const size_t kCrashReportSeparatorWidth = 72;

static void crashReportLocalTime(time_t raw, struct tm* output)
{
#if defined(_WIN32)
    localtime_s(output, &raw);
#else
    localtime_r(&raw, output);
#endif
}

static void crashReportUtcTime(time_t raw, struct tm* output)
{
#if defined(_WIN32)
    gmtime_s(output, &raw);
#else
    gmtime_r(&raw, output);
#endif
}

static uint64_t crashReportProcessId(void)
{
#if defined(_WIN32)
    return (uint64_t)GetCurrentProcessId();
#else
    return (uint64_t)getpid();
#endif
}

static uint64_t crashReportThreadId(void)
{
#if defined(_WIN32)
    return (uint64_t)GetCurrentThreadId();
#elif defined(__ANDROID__)
    return (uint64_t)gettid();
#else
    return 0;
#endif
}

static const char* crashReportArchitecture(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm32";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

static const char* crashReportCompiler(void)
{
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#elif defined(_MSC_VER)
    return "msvc";
#else
    return "unknown";
#endif
}

static void crashReportWriteUnderline(FILE* file, char character)
{
    for (size_t index = 0; index < kCrashReportSeparatorWidth; ++index)
    {
        fputc(character, file);
    }
    fputc('\n', file);
}

void crashReportWriteSection(FILE* file, const char* title)
{
    if (!file)
    {
        return;
    }
    fprintf(file, "\n[%s]\n", title ? title : "Unknown");
    crashReportWriteUnderline(file, '-');
}

void crashReportWriteField(FILE* file, const char* key, const char* format, ...)
{
    if (!file || !key || !format)
    {
        return;
    }

    char value[4096] = {};
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(value, sizeof(value), format, arguments);
    va_end(arguments);
    value[sizeof(value) - 1] = '\0';

    const char* truncationMarker = "...<truncated>";
    if (length < 0)
    {
        snprintf(value, sizeof(value), "<format-error>");
    }
    else if ((size_t)length >= sizeof(value))
    {
        size_t markerLength = strlen(truncationMarker);
        size_t markerOffset = sizeof(value) - markerLength - 1;
        memcpy(value + markerOffset, truncationMarker, markerLength + 1);
    }

    fprintf(file, "%s=", key);
    for (const unsigned char* current = (const unsigned char*)value;
        *current; ++current)
    {
        if (*current == '\n')
        {
            fputs("\\n", file);
        }
        else if (*current == '\r')
        {
            fputs("\\r", file);
        }
        else if (*current == '\t')
        {
            fputs("\\t", file);
        }
        else if (*current < 0x20u)
        {
            fprintf(file, "\\x%02x", *current);
        }
        else
        {
            fputc(*current, file);
        }
    }
    fputc('\n', file);
}

void crashReportWriteBoolean(FILE* file, const char* key, bool value)
{
    crashReportWriteField(file, key, value ? "true" : "false");
}

void crashReportWriteAddressOffset(FILE* file, const char* key,
    uint32_t address, uint32_t origin)
{
    if (origin && address >= origin)
    {
        crashReportWriteField(file, key, "0x%08x", address - origin);
    }
    else
    {
        crashReportWriteField(file, key, "unavailable");
    }
}

void crashReportWriteArmRegisters(FILE* file, const uint32_t* registers)
{
    crashReportWriteSection(file, "ARM Registers");
    for (uint32_t index = 0; index < 16; ++index)
    {
        char key[8] = {};
        snprintf(key, sizeof(key), "r%u", index);
        crashReportWriteField(file, key, "0x%08x",
            registers ? registers[index] : 0);
    }
}

#if defined(__ANDROID__)
static std::string crashReportAndroidProperty(const char* name)
{
    char value[PROP_VALUE_MAX] = {};
    return name && __system_property_get(name, value) > 0 ? value : "unavailable";
}
#endif

static void crashReportWriteSystem(FILE* file)
{
    crashReportWriteSection(file, "System");
#if defined(_WIN32)
    SYSTEM_INFO systemInfo = {};
    GetNativeSystemInfo(&systemInfo);
    crashReportWriteField(file, "platform", "windows");
    typedef LONG (WINAPI* RtlGetVersionFunction)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFunction rtlGetVersion = ntdll ?
        (RtlGetVersionFunction)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion && rtlGetVersion(&version) == 0)
    {
        crashReportWriteField(file, "windows_major", "%u",
            (unsigned int)version.dwMajorVersion);
        crashReportWriteField(file, "windows_minor", "%u",
            (unsigned int)version.dwMinorVersion);
        crashReportWriteField(file, "windows_build", "%u",
            (unsigned int)version.dwBuildNumber);
    }
    crashReportWriteField(file, "processor_count", "%u",
        (unsigned int)systemInfo.dwNumberOfProcessors);
    crashReportWriteField(file, "page_size", "%u",
        (unsigned int)systemInfo.dwPageSize);
    crashReportWriteField(file, "allocation_granularity", "%u",
        (unsigned int)systemInfo.dwAllocationGranularity);
    MEMORYSTATUSEX memoryStatus = {};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus))
    {
        crashReportWriteField(file, "physical_memory_total", "%llu",
            (unsigned long long)memoryStatus.ullTotalPhys);
        crashReportWriteField(file, "physical_memory_available", "%llu",
            (unsigned long long)memoryStatus.ullAvailPhys);
    }
#elif defined(__ANDROID__)
    struct utsname kernel = {};
    bool hasKernel = uname(&kernel) == 0;
    crashReportWriteField(file, "platform", "android");
    crashReportWriteField(file, "android_sdk", "%s",
        crashReportAndroidProperty("ro.build.version.sdk").c_str());
    crashReportWriteField(file, "android_release", "%s",
        crashReportAndroidProperty("ro.build.version.release").c_str());
    crashReportWriteField(file, "device_manufacturer", "%s",
        crashReportAndroidProperty("ro.product.manufacturer").c_str());
    crashReportWriteField(file, "device_model", "%s",
        crashReportAndroidProperty("ro.product.model").c_str());
    crashReportWriteField(file, "device_name", "%s",
        crashReportAndroidProperty("ro.product.device").c_str());
    crashReportWriteField(file, "process_abi", "%s", crashReportArchitecture());
    long processorCount = sysconf(_SC_NPROCESSORS_ONLN);
    long pageSize = sysconf(_SC_PAGESIZE);
    crashReportWriteField(file, "processor_count", "%ld", processorCount);
    crashReportWriteField(file, "page_size", "%ld", pageSize);
    struct sysinfo memoryInfo = {};
    if (sysinfo(&memoryInfo) == 0)
    {
        crashReportWriteField(file, "physical_memory_total", "%llu",
            (unsigned long long)memoryInfo.totalram * memoryInfo.mem_unit);
        crashReportWriteField(file, "physical_memory_available", "%llu",
            (unsigned long long)memoryInfo.freeram * memoryInfo.mem_unit);
    }
    crashReportWriteField(file, "kernel_system", "%s",
        hasKernel ? kernel.sysname : "unavailable");
    crashReportWriteField(file, "kernel_release", "%s",
        hasKernel ? kernel.release : "unavailable");
    crashReportWriteField(file, "build_fingerprint", "%s",
        crashReportAndroidProperty("ro.build.fingerprint").c_str());
#else
    struct utsname systemInfo = {};
    bool hasSystemInfo = uname(&systemInfo) == 0;
    crashReportWriteField(file, "platform", "posix");
    crashReportWriteField(file, "kernel_system", "%s",
        hasSystemInfo ? systemInfo.sysname : "unavailable");
    crashReportWriteField(file, "kernel_release", "%s",
        hasSystemInfo ? systemInfo.release : "unavailable");
#endif
}

bool crashReportOpenForGame(
    const std::string& gamePath,
    const char* kind,
    std::string* outFileName,
    FILE** outFile)
{
    if (!outFile)
    {
        return false;
    }
    *outFile = NULL;

    time_t raw = time(NULL);
    struct tm localTime = {};
    struct tm utcTime = {};
    crashReportLocalTime(raw, &localTime);
    crashReportUtcTime(raw, &utcTime);

    char fileTimestamp[32] = {};
    char localTimestamp[40] = {};
    char utcTimestamp[40] = {};
    strftime(fileTimestamp, sizeof(fileTimestamp), "%Y%m%d-%H%M%S", &localTime);
    strftime(localTimestamp, sizeof(localTimestamp), "%Y-%m-%dT%H:%M:%S", &localTime);
    strftime(utcTimestamp, sizeof(utcTimestamp), "%Y-%m-%dT%H:%M:%SZ", &utcTime);

    char fileName[128] = {};
    snprintf(fileName, sizeof(fileName), "DingooPie-crash-%s-%s-%llu.log",
        kind ? kind : "unknown", fileTimestamp,
        (unsigned long long)crashReportProcessId());
    fileName[sizeof(fileName) - 1] = '\0';

    FILE* file = platformOpenGameSiblingFile(gamePath, fileName, "wb");
    if (!file)
    {
        return false;
    }
    setvbuf(file, NULL, _IONBF, 0);
    if (outFileName)
    {
        *outFileName = fileName;
    }
    *outFile = file;

    fprintf(file, "DingooPie Crash Report\n");
    crashReportWriteUnderline(file, '=');
    crashReportWriteSection(file, "Report");
    crashReportWriteField(file, "format_version", "2");
    crashReportWriteField(file, "kind", "%s", kind ? kind : "unknown");
    crashReportWriteField(file, "timestamp_local", "%s", localTimestamp);
    crashReportWriteField(file, "timestamp_utc", "%s", utcTimestamp);
    crashReportWriteField(file, "unix_time", "%llu", (unsigned long long)raw);
    crashReportWriteField(file, "process_id", "%llu",
        (unsigned long long)crashReportProcessId());
    crashReportWriteField(file, "thread_id", "%llu",
        (unsigned long long)crashReportThreadId());
    crashReportWriteField(file, "file_name", "%s", fileName);
    crashReportWriteField(file, "storage_target", "game_directory");
    crashReportWriteField(file, "game_path", "%s", gamePath.c_str());
    crashReportWriteField(file, "architecture", "%s", crashReportArchitecture());
    crashReportWriteField(file, "pointer_bits", "%u", (unsigned int)(sizeof(void*) * 8u));
    crashReportWriteField(file, "endianness", "%s",
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        "big"
#else
        "little"
#endif
    );
    crashReportWriteField(file, "compiler", "%s", crashReportCompiler());
    crashReportWriteField(file, "build_date", "%s", __DATE__);
    crashReportWriteField(file, "build_time", "%s", __TIME__);
    crashReportWriteSystem(file);
    return true;
}

bool crashReportClose(FILE* file)
{
    if (!file)
    {
        return false;
    }
    crashReportWriteSection(file, "Report Completion");
    crashReportWriteField(file, "write_complete", "true");
    bool flushed = fflush(file) == 0;
    bool closed = fclose(file) == 0;
    return flushed && closed;
}
