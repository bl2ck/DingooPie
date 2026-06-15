#include "runtime_debug.h"
#include <time.h>
#include "emulated_memory.h"
#include <capstone/capstone.h>

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
    uint32_t v;
    printf("==========================STACK=================================\n");
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &v); printf("0x%08x:\t", v);
    void *stack = toHostPtr(v);
    int i = 0;
    for (int j = 0; j < stackStartAddress - v; j += 4)
    {
        printf("%08x ", ((uint32_t*)stack)[++i]);
        if (j % 16 == 0)
        {
            printf("\n");
            printf("0x%08x:\t", v + j);
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
    uint32_t v;

    fprintf(fp, "==========================REG=================================\n");
    //nativeRuntimeReadRegister(runtime, RUNTIME_REG_ZERO, &v); printf("ZERO=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_AT, &v); fprintf(fp, "AT=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_V0, &v); fprintf(fp, "V0=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_V1, &v); fprintf(fp, "V1=%08X\t\n", v);

    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A0, &v); fprintf(fp, "A0=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A1, &v); fprintf(fp, "A1=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A2, &v); fprintf(fp, "A2=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_A3, &v); fprintf(fp, "A3=%08X\t\n", v);

    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S0, &v); fprintf(fp, "S0=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S1, &v); fprintf(fp, "S1=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S2, &v); fprintf(fp, "S2=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S3, &v); fprintf(fp, "S3=%08X\t\n", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S4, &v); fprintf(fp, "S4=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S5, &v); fprintf(fp, "S5=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S6, &v); fprintf(fp, "S6=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_S7, &v); fprintf(fp, "S7=%08X\t\n", v);

    nativeRuntimeReadRegister(runtime, RUNTIME_REG_LO, &v); fprintf(fp, "LO=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_HI, &v); fprintf(fp, "HI=%08X\t\n", v);

    nativeRuntimeReadRegister(runtime, RUNTIME_REG_PC, &v); fprintf(fp, "PC=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_SP, &v); fprintf(fp, "SP=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_FP, &v); fprintf(fp, "FP=%08X\t", v);
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &v); fprintf(fp, "RA=%08X\t\n", v);
    fprintf(fp, "==============================================================\n");
}

static void runtimeDebugDumpOneInstruction(NativeRuntime* runtime, uint32_t address)
{
    cs_insn* insn;
    uint32_t binary;
    size_t count;
    csh handle;
    uint32_t size = 4;

    if (cs_open(CS_ARCH_MIPS, CS_MODE_MIPS32, &handle) != CS_ERR_OK)
    {
        printf("debug cs_open() fail.");
        exit(1);
    }
    nativeRuntimeReadMemory(runtime, address, &binary, size);
    count = cs_disasm(handle, (uint8_t*)&binary, size, address, 1, &insn);
    if (count > 0)
    {
        for (int j = 0; j < count; j++)
        {
            printf("%08X:    %08x    %s\t%s\n", address, binary, insn[j].mnemonic, insn[j].op_str);
        }
    }
    else
    {
        printf("%08X:    %08x  -----------disasm-error----------- \n", address, binary);
    }

    cs_close(&handle);
}

void runtimeDebugDumpReturnDisassembly(NativeRuntime* runtime)
{
    uint32_t ra;
    uint32_t address = 0;
    printf("==========================DISASM==============================\n");
    nativeRuntimeReadRegister(runtime, RUNTIME_REG_RA, &ra); 

    address = ra - 256;
    while ((ra + 4) != address)
    {
        runtimeDebugDumpOneInstruction(runtime, address);
        address += 4;
    }

    printf("==============================================================\n");
}

void runtimeDebugDumpDisassemblyRange(NativeRuntime* runtime, uint32_t address, uint32_t bytes)
{
    printf("==========================DISASM RANGE 0x%08x=================\n", address);
    uint32_t end = address + bytes;
    while (address < end)
    {
        runtimeDebugDumpOneInstruction(runtime, address);
        address += 4;
    }
    printf("==============================================================\n");
}


void runtimeDebugDumpMemory(void * buffer, uint32_t count)
{
    uint8_t * d = (uint8_t*)buffer;
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
    std::string strLocale = setlocale(LC_ALL, "");
    const wchar_t* wchSrc = ws.c_str();
    size_t nDestSize = wcstombs(NULL, wchSrc, 0) + 1;
    char* chDest = new char[nDestSize];
    memset(chDest, 0, nDestSize);
    wcstombs(chDest, wchSrc, nDestSize);
    std::string strResult = chDest;
    delete[]chDest;
    setlocale(LC_ALL, strLocale.c_str());
    return strResult;
}

// Convert narrow strings through the active locale on non-Win32 fallback paths.
std::wstring String2WString(const std::string& s)
{
    std::string strLocale = setlocale(LC_ALL, "");
    const char* chSrc = s.c_str();
    size_t nDestSize = mbstowcs(NULL, chSrc, 0) + 1;
    wchar_t* wchDest = new wchar_t[nDestSize];
    wmemset(wchDest, 0, nDestSize);
    mbstowcs(wchDest, chSrc, nDestSize);
    std::wstring wstrResult = wchDest;
    delete[]wchDest;
    setlocale(LC_ALL, strLocale.c_str());
    return wstrResult;
}
