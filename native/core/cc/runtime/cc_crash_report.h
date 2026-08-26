#ifndef DINGOO_PIE_CC_RUNTIME_CC_CRASH_REPORT_H
#define DINGOO_PIE_CC_RUNTIME_CC_CRASH_REPORT_H

#include <stdint.h>
#include <string>

struct CcCrashLogContext
{
    const char* gamePath;
    const char* gameSha256;
    const char* saveDirectory;
    const char* error;
    const char* backend;
    const uint32_t* registers;
    uint32_t cpsr;
    uint32_t unsupportedInstruction;
    uint64_t instructions;
    uint32_t importCalls;
    uint32_t unknownImports;
    uint32_t framesSubmitted;
    uint32_t tasksCreated;
    uint32_t faultAddress;
    uint32_t faultSize;
    uint32_t unsupportedPc;
    uint32_t lastImportPc;
    uint32_t lastImportReturnAddress;
    uint32_t failedTaskIndex;
    uint32_t failedTaskEntry;
    uint32_t failedTaskStack;
    uint32_t failedTaskPriority;
    uint32_t failedTaskDelayTicks;
    bool faultWrite;
    bool faultFetch;
    const char* lastImport;
};

bool crashLogWriteCcFailure(
    const CcCrashLogContext& context,
    std::string* outFileName);

#endif
