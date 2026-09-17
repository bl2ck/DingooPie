#include "app/runtime/app_crash_report.h"

#include "shared/diagnostics/crash_report_writer.h"

#include <capstone/capstone.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

static bool crashLogReadRegister(NativeRuntime* runtime, int reg, uint32_t* out)
{
    if (!out)
    {
        return false;
    }
    *out = 0;
    return runtime && nativeRuntimeReadRegister(runtime, reg, out) == RUNTIME_OK;
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

static void crashLogWriteCrashLocation(FILE* fp, NativeRuntime* runtime,
    const CrashLogContext& context, uint32_t pc, uint32_t ra, uint32_t sp,
    uint32_t s4, uint32_t v0)
{
    std::string pcInstruction = crashLogFormatInstruction(runtime, pc);
    std::string raInstruction = ra == pc ? "same-as-pc" :
        crashLogFormatInstruction(runtime, ra);

    uint64_t appEnd = (uint64_t)context.origin + context.appSize;
    crashReportWriteSection(fp, "Crash Location");
    crashReportWriteField(fp, "pc", "0x%08x", pc);
    crashReportWriteAddressOffset(fp, "pc_offset", pc, context.origin);
    crashReportWriteBoolean(fp, "pc_in_app",
        pc >= context.origin && (uint64_t)pc < appEnd);
    crashReportWriteField(fp, "pc_instruction", "%s", pcInstruction.c_str());
    crashReportWriteField(fp, "ra", "0x%08x", ra);
    crashReportWriteAddressOffset(fp, "ra_offset", ra, context.origin);
    crashReportWriteBoolean(fp, "ra_in_app",
        ra >= context.origin && (uint64_t)ra < appEnd);
    crashReportWriteField(fp, "ra_instruction", "%s", raInstruction.c_str());
    crashReportWriteField(fp, "sp", "0x%08x", sp);
    crashReportWriteField(fp, "s4", "0x%08x", s4);
    crashReportWriteField(fp, "v0", "0x%08x", v0);
}

static void crashLogWriteApp(FILE* fp, const CrashLogContext& context)
{
    uint64_t appEnd = (uint64_t)context.origin + context.appSize;
    crashReportWriteSection(fp, "App");
    crashReportWriteField(fp, "app_path", "%s", context.appPath ? context.appPath : "");
    crashReportWriteField(fp, "app_main_path", "%s", context.appMainPath ? context.appMainPath : "");
    crashReportWriteField(fp, "app_sha256", "%s", context.appSha256 ? context.appSha256 : "");
    crashReportWriteField(fp, "save_directory", "%s",
        context.saveDirectory ? context.saveDirectory : "");
    crashReportWriteField(fp, "app_entry", "0x%08x", context.appEntry);
    crashReportWriteAddressOffset(fp, "app_entry_offset", context.appEntry, context.origin);
    crashReportWriteField(fp, "boot_entry", "0x%08x", context.bootEntry);
    crashReportWriteAddressOffset(fp, "boot_entry_offset", context.bootEntry, context.origin);
    crashReportWriteField(fp, "origin", "0x%08x", context.origin);
    crashReportWriteField(fp, "app_size", "0x%08x", context.appSize);
    crashReportWriteField(fp, "app_end_exclusive", "0x%llx",
        (unsigned long long)appEnd);
}

static void crashLogWriteRegisters(FILE* fp, NativeRuntime* runtime)
{
    static const char* kNames[32] = {
        "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
    };

    crashReportWriteSection(fp, "MIPS Registers");
    bool snapshotComplete = true;
    for (int i = 0; i < 32; ++i)
    {
        uint32_t value = 0;
        snapshotComplete = crashLogReadRegister(runtime, i, &value) &&
            snapshotComplete;
        crashReportWriteField(fp, kNames[i], "0x%08x", value);
    }

    uint32_t pc = 0;
    uint32_t hi = 0;
    uint32_t lo = 0;
    snapshotComplete = crashLogReadRegister(runtime, RUNTIME_REG_PC, &pc) &&
        snapshotComplete;
    snapshotComplete = crashLogReadRegister(runtime, RUNTIME_REG_HI, &hi) &&
        snapshotComplete;
    snapshotComplete = crashLogReadRegister(runtime, RUNTIME_REG_LO, &lo) &&
        snapshotComplete;
    crashReportWriteField(fp, "pc", "0x%08x", pc);
    crashReportWriteField(fp, "hi", "0x%08x", hi);
    crashReportWriteField(fp, "lo", "0x%08x", lo);
    crashReportWriteBoolean(fp, "snapshot_complete", snapshotComplete);
}

static void crashLogWriteDisassemblyRange(FILE* fp, NativeRuntime* runtime, const char* label, uint32_t center)
{
    char title[64] = {};
    snprintf(title, sizeof(title), "Disassembly (%s)", label);
    crashReportWriteSection(fp, title);
    crashReportWriteField(fp, "center", "0x%08x", center);
    if (!center)
    {
        crashReportWriteField(fp, "status", "unavailable");
        return;
    }

    uint32_t start = center >= 0x20 ? center - 0x20 : center;
    uint32_t end = center + 0x40;
    csh handle = 0;
    if (cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32, &handle) != CS_ERR_OK)
    {
        crashReportWriteField(fp, "status", "capstone-open-failed");
        return;
    }
    crashReportWriteField(fp, "start", "0x%08x", start);
    crashReportWriteField(fp, "end_exclusive", "0x%08x", end);
    crashReportWriteField(fp, "status", "available");

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
    crashReportWriteSection(fp, title);
    crashReportWriteField(fp, "address", "0x%08x", address);
    crashReportWriteField(fp, "requested_size", "0x%zx", bytes);
    if (!address)
    {
        crashReportWriteField(fp, "status", "unavailable");
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
        crashReportWriteField(fp, "status", "unreadable");
        crashReportWriteField(fp, "read_error_code", "%u", err);
        crashReportWriteField(fp, "read_error_name", "%s",
            nativeRuntimeErrorString(err));
        return;
    }
    crashReportWriteField(fp, "status", "available");
    crashReportWriteField(fp, "dump_size", "0x%zx", bytes);

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
    FILE* fp = NULL;
    if (!crashReportOpenForGame(
        context.appPath ? context.appPath : "",
        "guest-runtime-failure", outFileName, &fp))
    {
        return false;
    }

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

    crashReportWriteSection(fp, "Summary");
    crashReportWriteField(fp, "error", "%u (%s)", err,
        nativeRuntimeErrorString(err));
    crashReportWriteField(fp, "error_code", "%u", err);
    crashReportWriteField(fp, "error_name", "%s",
        nativeRuntimeErrorString(err));
    crashReportWriteField(fp, "backend", "%s",
        executionBackendName(context.backend));
    crashReportWriteField(fp, "compat_profile", "%s",
        context.compatProfile ? context.compatProfile : "");

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

    return crashReportClose(fp);
}
