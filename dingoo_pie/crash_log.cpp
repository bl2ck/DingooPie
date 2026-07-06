#include "crash_log.h"

#include "app_paths.h"
#include "platform_win32.h"

#include <capstone/capstone.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

static std::string crashLogTimestamp(void)
{
    time_t raw = time(NULL);
    struct tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &raw);
#else
    localtime_r(&raw, &localTime);
#endif

    char text[32] = {};
    strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &localTime);
    return text;
}

static unsigned long crashLogProcessId(void)
{
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static std::wstring crashLogPathNearExe(const wchar_t* fileName)
{
#ifdef _WIN32
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        path.resize(slash + 1);
    }
    path += fileName;
    return path;
#else
    return std::wstring(fileName);
#endif
}

static FILE* crashLogOpen(const std::string& timestamp, std::string* outFileName)
{
    char fileName[96] = {};
    snprintf(fileName, sizeof(fileName), "DingooPie-crash-%s-%lu.log",
        timestamp.c_str(), crashLogProcessId());
    fileName[sizeof(fileName) - 1] = '\0';
    if (outFileName)
    {
        *outFileName = fileName;
    }

#ifdef _WIN32
    std::wstring wideName = platformUtf8ToWide(fileName);
    return _wfopen(crashLogPathNearExe(wideName.c_str()).c_str(), L"w");
#else
    return fopen(fileName, "w");
#endif
}

static bool crashLogReadRegister(NativeRuntime* runtime, int reg, uint32_t* out)
{
    if (!out)
    {
        return false;
    }
    *out = 0;
    return runtime && nativeRuntimeReadRegister(runtime, reg, out) == RUNTIME_OK;
}

static void crashLogWriteUnderline(FILE* fp, char ch, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        fputc(ch, fp);
    }
    fputc('\n', fp);
}

static void crashLogWriteSection(FILE* fp, const char* title)
{
    fprintf(fp, "\n%s\n", title);
    crashLogWriteUnderline(fp, '-', strlen(title));
}

static void crashLogWriteField(FILE* fp, const char* key, const char* format, ...)
{
    // Keep crash reports script-friendly: one compact key=value field per line.
    fprintf(fp, "%s=", key);
    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);
    fputc('\n', fp);
}

static std::string crashLogFormatInstruction(NativeRuntime* runtime, uint32_t address)
{
    char text[192] = {};
    if (!address)
    {
        return "unavailable";
    }

    uint32_t encoding = 0;
    RuntimeError readErr = nativeRuntimeReadMemory(runtime, address, &encoding, sizeof(encoding));
    if (readErr != RUNTIME_OK)
    {
        snprintf(text, sizeof(text), "unreadable address=0x%08x err=%u (%s)",
            address, readErr, nativeRuntimeErrorString(readErr));
        return text;
    }

    csh handle = 0;
    if (cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32, &handle) != CS_ERR_OK)
    {
        snprintf(text, sizeof(text), "0x%08x: %08x  capstone-open-failed",
            address, encoding);
        return text;
    }

    cs_insn* insn = NULL;
    size_t count = cs_disasm(handle, (uint8_t*)&encoding, sizeof(encoding), address, 1, &insn);
    if (count > 0)
    {
        snprintf(text, sizeof(text), "0x%08x: %08x  %s %s",
            address, encoding, insn[0].mnemonic, insn[0].op_str);
        cs_free(insn, count);
    }
    else
    {
        snprintf(text, sizeof(text), "0x%08x: %08x  disasm-error",
            address, encoding);
    }

    cs_close(&handle);
    return text;
}

static void crashLogWriteAddressOffset(FILE* fp, const char* key,
    uint32_t address, uint32_t origin)
{
    if (origin && address >= origin)
    {
        crashLogWriteField(fp, key, "0x%08x", address - origin);
    }
    else
    {
        crashLogWriteField(fp, key, "unavailable");
    }
}

static void crashLogWriteCrashLocation(FILE* fp, NativeRuntime* runtime,
    const CrashLogContext& context, uint32_t pc, uint32_t ra, uint32_t sp,
    uint32_t s4, uint32_t v0)
{
    std::string pcInstruction = crashLogFormatInstruction(runtime, pc);
    std::string raInstruction = ra == pc ? "same-as-pc" :
        crashLogFormatInstruction(runtime, ra);

    crashLogWriteSection(fp, "Crash Location");
    crashLogWriteField(fp, "pc", "0x%08x", pc);
    crashLogWriteAddressOffset(fp, "pc_offset", pc, context.origin);
    crashLogWriteField(fp, "pc_instruction", "%s", pcInstruction.c_str());
    crashLogWriteField(fp, "ra", "0x%08x", ra);
    crashLogWriteAddressOffset(fp, "ra_offset", ra, context.origin);
    crashLogWriteField(fp, "ra_instruction", "%s", raInstruction.c_str());
    crashLogWriteField(fp, "sp", "0x%08x", sp);
    crashLogWriteField(fp, "s4", "0x%08x", s4);
    crashLogWriteField(fp, "v0", "0x%08x", v0);
}

static void crashLogWriteApp(FILE* fp, const CrashLogContext& context)
{
    crashLogWriteSection(fp, "App");
    crashLogWriteField(fp, "app_path", "%s", context.appPath ? context.appPath : "");
    crashLogWriteField(fp, "app_main_path", "%s", context.appMainPath ? context.appMainPath : "");
    crashLogWriteField(fp, "app_sha256", "%s", context.appSha256 ? context.appSha256 : "");
    crashLogWriteField(fp, "app_entry", "0x%08x", context.appEntry);
    crashLogWriteField(fp, "boot_entry", "0x%08x", context.bootEntry);
    crashLogWriteField(fp, "origin", "0x%08x", context.origin);
    crashLogWriteField(fp, "app_size", "0x%08x", context.appSize);
}

static void crashLogWriteRegisters(FILE* fp, NativeRuntime* runtime)
{
    static const char* kNames[32] = {
        "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
    };

    crashLogWriteSection(fp, "Registers");
    for (int i = 0; i < 32; i += 4)
    {
        uint32_t values[4] = {};
        for (int j = 0; j < 4; ++j)
        {
            crashLogReadRegister(runtime, i + j, &values[j]);
        }
        fprintf(fp, "%s=0x%08x    %s=0x%08x    %s=0x%08x    %s=0x%08x\n",
            kNames[i], values[0],
            kNames[i + 1], values[1],
            kNames[i + 2], values[2],
            kNames[i + 3], values[3]);
    }

    uint32_t pc = 0;
    uint32_t hi = 0;
    uint32_t lo = 0;
    crashLogReadRegister(runtime, RUNTIME_REG_PC, &pc);
    crashLogReadRegister(runtime, RUNTIME_REG_HI, &hi);
    crashLogReadRegister(runtime, RUNTIME_REG_LO, &lo);
    fprintf(fp, "pc=0x%08x    hi=0x%08x    lo=0x%08x\n", pc, hi, lo);
}

static void crashLogWriteDisassemblyRange(FILE* fp, NativeRuntime* runtime, const char* label, uint32_t center)
{
    char title[64] = {};
    snprintf(title, sizeof(title), "Disassembly (%s)", label);
    crashLogWriteSection(fp, title);
    fprintf(fp, "center=0x%08x\n\n", center);
    if (!center)
    {
        fprintf(fp, "unavailable\n");
        return;
    }

    uint32_t start = center >= 0x20 ? center - 0x20 : center;
    uint32_t end = center + 0x40;
    csh handle = 0;
    if (cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32, &handle) != CS_ERR_OK)
    {
        fprintf(fp, "capstone-open-failed\n");
        return;
    }

    for (uint32_t address = start; address < end; address += 4)
    {
        uint32_t encoding = 0;
        RuntimeError readErr = nativeRuntimeReadMemory(runtime, address, &encoding, sizeof(encoding));
        if (readErr != RUNTIME_OK)
        {
            fprintf(fp, "%s %08x: unreadable err=%u (%s)\n",
                address == center ? "=>" : "  ",
                address, readErr, nativeRuntimeErrorString(readErr));
            continue;
        }

        cs_insn* insn = NULL;
        size_t count = cs_disasm(handle, (uint8_t*)&encoding, sizeof(encoding), address, 1, &insn);
        if (count > 0)
        {
            fprintf(fp, "%s %08x: %08x  %-8s %s\n",
                address == center ? "=>" : "  ",
                address,
                encoding,
                insn[0].mnemonic,
                insn[0].op_str);
            cs_free(insn, count);
        }
        else
        {
            fprintf(fp, "%s %08x: %08x  disasm-error\n",
                address == center ? "=>" : "  ", address, encoding);
        }
    }

    cs_close(&handle);
}

static void crashLogWriteMemory(FILE* fp, NativeRuntime* runtime, const char* label, uint32_t address, size_t bytes)
{
    char title[64] = {};
    snprintf(title, sizeof(title), "Memory (%s)", label);
    crashLogWriteSection(fp, title);
    fprintf(fp, "address=0x%08x    size=0x%zx\n\n", address, bytes);
    if (!address)
    {
        fprintf(fp, "unavailable\n");
        return;
    }

    uint8_t buffer[128] = {};
    if (bytes > sizeof(buffer))
    {
        bytes = sizeof(buffer);
    }

    RuntimeError err = nativeRuntimeReadMemory(runtime, address, buffer, bytes);
    if (err != RUNTIME_OK)
    {
        fprintf(fp, "unreadable err=%u (%s)\n", err, nativeRuntimeErrorString(err));
        return;
    }

    for (size_t offset = 0; offset < bytes; offset += 16)
    {
        size_t lineBytes = bytes - offset;
        if (lineBytes > 16)
        {
            lineBytes = 16;
        }
        fprintf(fp, "%08x  ", (uint32_t)(address + offset));
        for (size_t i = 0; i < lineBytes; ++i)
        {
            fprintf(fp, " %02x", buffer[offset + i]);
        }
        fprintf(fp, "\n");
    }
}

bool crashLogWriteGuestFailure(
    NativeRuntime* runtime,
    RuntimeError err,
    const CrashLogContext& context,
    std::string* outFileName)
{
    std::string timestamp = crashLogTimestamp();
    FILE* fp = crashLogOpen(timestamp, outFileName);
    if (!fp)
    {
        return false;
    }

    setvbuf(fp, NULL, _IONBF, 0);

    uint32_t pc = 0;
    uint32_t ra = 0;
    uint32_t sp = 0;
    uint32_t s4 = 0;
    uint32_t v0 = 0;
    crashLogReadRegister(runtime, RUNTIME_REG_PC, &pc);
    crashLogReadRegister(runtime, RUNTIME_REG_RA, &ra);
    crashLogReadRegister(runtime, RUNTIME_REG_SP, &sp);
    crashLogReadRegister(runtime, RUNTIME_REG_S4, &s4);
    crashLogReadRegister(runtime, RUNTIME_REG_V0, &v0);

    fprintf(fp, "DingooPie Crash Report\n");
    crashLogWriteUnderline(fp, '=', strlen("DingooPie Crash Report"));

    crashLogWriteSection(fp, "Summary");
    crashLogWriteField(fp, "timestamp", "%s", timestamp.c_str());
    crashLogWriteField(fp, "kind", "guest-runtime-failure");
    crashLogWriteField(fp, "error", "%u (%s)", err, nativeRuntimeErrorString(err));
    crashLogWriteField(fp, "backend", "%s", executionBackendName(context.backend));
    crashLogWriteField(fp, "compat_profile", "%s", context.compatProfile ? context.compatProfile : "");

    crashLogWriteCrashLocation(fp, runtime, context, pc, ra, sp, s4, v0);
    crashLogWriteDisassemblyRange(fp, runtime, "pc", pc);
    if (ra && ra != pc)
    {
        crashLogWriteDisassemblyRange(fp, runtime, "ra", ra);
    }
    crashLogWriteRegisters(fp, runtime);
    crashLogWriteMemory(fp, runtime, "sp", sp, 0x80);
    crashLogWriteMemory(fp, runtime, "s4-object", s4, 0x40);
    crashLogWriteMemory(fp, runtime, "v0-callback", v0, 0x80);
    crashLogWriteApp(fp, context);

    fclose(fp);
    return true;
}
