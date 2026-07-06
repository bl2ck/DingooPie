#include "runtime_debug.h"
#include <time.h>
#include "emulated_memory.h"
#include <capstone/capstone.h>
#include <vector>

void runtimeDebugDumpRegistersToFile(NativeRuntime* runtime, FILE* fp);

const char* runtimeMemoryAccessName(RuntimeMemoryAccess type)
{
    // clang-format off
    switch (type)
    {
    case RUNTIME_MEM_READ:return "RUNTIME_MEM_READ";
    case RUNTIME_MEM_WRITE:return "RUNTIME_MEM_WRITE";
    case RUNTIME_MEM_FETCH:return "RUNTIME_MEM_FETCH";
    case RUNTIME_MEM_READ_UNMAPPED:return "RUNTIME_MEM_READ_UNMAPPED";
    case RUNTIME_MEM_WRITE_UNMAPPED:return "RUNTIME_MEM_WRITE_UNMAPPED";
    case RUNTIME_MEM_FETCH_UNMAPPED:return "RUNTIME_MEM_FETCH_UNMAPPED";
    case RUNTIME_MEM_WRITE_PROT:return "RUNTIME_MEM_WRITE_PROT";
    case RUNTIME_MEM_READ_PROT:return "RUNTIME_MEM_READ_PROT";
    case RUNTIME_MEM_FETCH_PROT:return "RUNTIME_MEM_FETCH_PROT";
    case RUNTIME_MEM_READ_AFTER:return "RUNTIME_MEM_READ_AFTER";
    }
    // clang-format on
    return "<error type>";
}

void runtimeDebugDumpStack(NativeRuntime* runtime, uint32_t stackStartAddress)
{
    if (!runtime)
    {
        printf("debug: no runtime for stack dump\n");
        return;
    }

    uint32_t v = 0;
    printf("==========================STACK=================================\n");
    if (nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &v) != RUNTIME_OK)
    {
        printf("debug: failed to read SP\n");
        printf("==============================================================\n");
        return;
    }
    if (stackStartAddress < v)
    {
        printf("debug: invalid stack range sp=0x%08x end=0x%08x\n", v, stackStartAddress);
        printf("==============================================================\n");
        return;
    }

    uint64_t stackBytes = (uint64_t)stackStartAddress - v;
    for (uint64_t offset = 0; offset < stackBytes && (uint64_t)v + offset <= 0xffffffffull; offset += 4u)
    {
        if ((offset % 16u) == 0)
        {
            if (offset)
            {
                printf("\n");
            }
            printf("0x%08x:\t", (uint32_t)((uint64_t)v + offset));
        }

        uint32_t word = 0;
        if (nativeRuntimeReadMemory(runtime, (uint32_t)((uint64_t)v + offset), &word, sizeof(word)) == RUNTIME_OK)
        {
            printf("%08x ", word);
        }
        else
        {
            printf("???????? ");
        }
    }
    printf("\n");

    printf("==============================================================\n");
}

void runtimeDebugDumpRegisters(NativeRuntime* runtime)
{
    runtimeDebugDumpRegistersToFile(runtime, stdout);
}

void runtimeDebugDumpRegistersToFile(NativeRuntime* runtime, FILE* fp)
{
    if (!fp)
    {
        fp = stdout;
    }

    auto regValue = [runtime](int reg) -> uint32_t {
        uint32_t v = 0;
        if (runtime)
        {
            nativeRuntimeReadRegister(runtime, reg, &v);
        }
        return v;
    };

    fprintf(fp, "==========================REG=================================\n");
    fprintf(fp, "AT=%08X\t", regValue(RUNTIME_REG_AT));
    fprintf(fp, "V0=%08X\t", regValue(RUNTIME_REG_V0));
    fprintf(fp, "V1=%08X\t\n", regValue(RUNTIME_REG_V1));

    fprintf(fp, "A0=%08X\t", regValue(RUNTIME_REG_A0));
    fprintf(fp, "A1=%08X\t", regValue(RUNTIME_REG_A1));
    fprintf(fp, "A2=%08X\t", regValue(RUNTIME_REG_A2));
    fprintf(fp, "A3=%08X\t\n", regValue(RUNTIME_REG_A3));

    fprintf(fp, "S0=%08X\t", regValue(RUNTIME_REG_S0));
    fprintf(fp, "S1=%08X\t", regValue(RUNTIME_REG_S1));
    fprintf(fp, "S2=%08X\t", regValue(RUNTIME_REG_S2));
    fprintf(fp, "S3=%08X\t\n", regValue(RUNTIME_REG_S3));
    fprintf(fp, "S4=%08X\t", regValue(RUNTIME_REG_S4));
    fprintf(fp, "S5=%08X\t", regValue(RUNTIME_REG_S5));
    fprintf(fp, "S6=%08X\t", regValue(RUNTIME_REG_S6));
    fprintf(fp, "S7=%08X\t\n", regValue(RUNTIME_REG_S7));

    fprintf(fp, "LO=%08X\t", regValue(RUNTIME_REG_LO));
    fprintf(fp, "HI=%08X\t\n", regValue(RUNTIME_REG_HI));

    fprintf(fp, "PC=%08X\t", regValue(RUNTIME_REG_PC));
    fprintf(fp, "SP=%08X\t", regValue(RUNTIME_REG_SP));
    fprintf(fp, "FP=%08X\t", regValue(RUNTIME_REG_FP));
    fprintf(fp, "RA=%08X\t\n", regValue(RUNTIME_REG_RA));
    fprintf(fp, "==============================================================\n");
}

static bool runtimeDebugOpenDisassembler(csh* handle)
{
    if (!handle || cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32, handle) != CS_ERR_OK)
    {
        printf("debug: cs_open failed\n");
        return false;
    }
    return true;
}

static void runtimeDebugDumpOneInstruction(csh handle, NativeRuntime* runtime, uint32_t address)
{
    cs_insn* insn = NULL;
    uint32_t binary = 0;
    const uint32_t size = 4;

    if (!runtime || nativeRuntimeReadMemory(runtime, address, &binary, size) != RUNTIME_OK)
    {
        printf("%08X:    --------  -----------unmapped----------- \n", address);
        return;
    }

    size_t count = cs_disasm(handle, (uint8_t*)&binary, size, address, 1, &insn);
    if (count > 0)
    {
        for (size_t j = 0; j < count; j++)
        {
            printf("%08X:    %08x    %s\t%s\n", address, binary, insn[j].mnemonic, insn[j].op_str);
        }
        cs_free(insn, count);
    }
    else
    {
        printf("%08X:    %08x  -----------disasm-error----------- \n", address, binary);
    }
}

void runtimeDebugDumpReturnDisassembly(NativeRuntime* runtime)
{
    uint32_t ra = 0;
    printf("==========================DISASM==============================\n");
    if (!runtime || nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra) != RUNTIME_OK)
    {
        printf("debug: failed to read RA\n");
        printf("==============================================================\n");
        return;
    }

    csh handle = 0;
    if (!runtimeDebugOpenDisassembler(&handle))
    {
        printf("==============================================================\n");
        return;
    }

    uint64_t address = ra >= 256u ? (uint64_t)ra - 256u : 0;
    uint64_t end = (uint64_t)ra + 4u;
    if (end > 0x100000000ull)
    {
        end = 0x100000000ull;
    }
    while (address < end)
    {
        runtimeDebugDumpOneInstruction(handle, runtime, (uint32_t)address);
        address += 4u;
    }
    cs_close(&handle);

    printf("==============================================================\n");
}

void runtimeDebugDumpDisassemblyRange(NativeRuntime* runtime, uint32_t address, uint32_t bytes)
{
    printf("==========================DISASM RANGE 0x%08x=================\n", address);
    csh handle = 0;
    if (!runtimeDebugOpenDisassembler(&handle))
    {
        printf("==============================================================\n");
        return;
    }

    uint64_t current = address;
    uint64_t end = (uint64_t)address + bytes;
    if (end > 0x100000000ull)
    {
        end = 0x100000000ull;
    }
    while (current < end)
    {
        runtimeDebugDumpOneInstruction(handle, runtime, (uint32_t)current);
        current += 4u;
    }
    cs_close(&handle);
    printf("==============================================================\n");
}

void runtimeDebugDumpMemory(void* buffer, uint32_t count)
{
    uint8_t* d = (uint8_t*)buffer;
    for (int i = 0; i < count; ++i)
    {
        printf("%02x ", d[i]);
        if ((i + 1)%16 == 0)
        {
            printf("\n");
        }
    }

    printf("\n");
}

void runtimeBytesToHexString(void* buff, int count, char* out)
{
    int i = 0;
    for (; i < count; i++)
    {
        sprintf(out + 3*i, "%02x ", (uint8_t)((char*)buff)[i]);
    }
}

const char* memTypeStr(RuntimeMemoryAccess type)
{
    return runtimeMemoryAccessName(type);
}

void dumpStackCall(NativeRuntime* runtime, uint32_t stack_start_address)
{
    runtimeDebugDumpStack(runtime, stack_start_address);
}

void dumpREG(NativeRuntime* runtime)
{
    runtimeDebugDumpRegisters(runtime);
}

void dumpREG2File(NativeRuntime* runtime, FILE* fp)
{
    runtimeDebugDumpRegistersToFile(runtime, fp);
}

void dumpAsm(NativeRuntime* runtime)
{
    runtimeDebugDumpReturnDisassembly(runtime);
}

void dumpAsmRange(NativeRuntime* runtime, uint32_t address, uint32_t bytes)
{
    runtimeDebugDumpDisassemblyRange(runtime, address, bytes);
}

void dumpMem(void* buffer, uint32_t count)
{
    runtimeDebugDumpMemory(buffer, count);
}

void toHexString(void* buff, int count, char* out)
{
    runtimeBytesToHexString(buff, count, out);
}

// Convert host wide strings through the active locale on non-Win32 fallback paths.
std::string WString2String(const std::wstring& ws)
{
    const char* previous = setlocale(LC_ALL, NULL);
    std::string strLocale = previous ? previous : "";
    setlocale(LC_ALL, "");
    const wchar_t* wchSrc = ws.c_str();
    size_t converted = wcstombs(NULL, wchSrc, 0);
    if (converted == (size_t)-1)
    {
        setlocale(LC_ALL, strLocale.c_str());
        std::string fallback;
        fallback.reserve(ws.size());
        for (size_t i = 0; i < ws.size(); ++i)
        {
            fallback.push_back(ws[i] <= 0xff ? (char)ws[i] : '?');
        }
        return fallback;
    }
    std::vector<char> chDest(converted + 1u, 0);
    wcstombs(&chDest[0], wchSrc, chDest.size());
    std::string strResult = &chDest[0];
    setlocale(LC_ALL, strLocale.c_str());
    return strResult;
}

// Convert narrow strings through the active locale on non-Win32 fallback paths.
std::wstring String2WString(const std::string& s)
{
    const char* previous = setlocale(LC_ALL, NULL);
    std::string strLocale = previous ? previous : "";
    setlocale(LC_ALL, "");
    const char* chSrc = s.c_str();
    size_t converted = mbstowcs(NULL, chSrc, 0);
    if (converted == (size_t)-1)
    {
        setlocale(LC_ALL, strLocale.c_str());
        std::wstring fallback;
        fallback.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            fallback.push_back((wchar_t)(unsigned char)s[i]);
        }
        return fallback;
    }
    std::vector<wchar_t> wchDest(converted + 1u, 0);
    mbstowcs(&wchDest[0], chSrc, wchDest.size());
    std::wstring wstrResult = &wchDest[0];
    setlocale(LC_ALL, strLocale.c_str());
    return wstrResult;
}
