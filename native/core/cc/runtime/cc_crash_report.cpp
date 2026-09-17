#include "cc/runtime/cc_crash_report.h"

#include "shared/diagnostics/crash_report_writer.h"

#include <stdio.h>

bool crashLogWriteCcFailure(const CcCrashLogContext& context,
    std::string* outFileName)
{
    FILE* file = NULL;
    if (!crashReportOpenForGame(
        context.gamePath ? context.gamePath : "",
        "cc-runtime-failure", outFileName, &file))
    {
        return false;
    }

    const uint32_t* registers = context.registers;
    const uint32_t pc = registers ? registers[15] : 0;
    const uint32_t lr = registers ? registers[14] : 0;
    const uint32_t sp = registers ? registers[13] : 0;
    const bool unsupportedInstructionValid = context.unsupportedPc != 0 ||
        context.unsupportedInstruction != 0;
    const bool faultValid = context.faultSize != 0;
    const uint64_t faultEnd = (uint64_t)context.faultAddress + context.faultSize;
    const bool lastImportValid = (context.lastImport && context.lastImport[0]) ||
        context.lastImportPc != 0 || context.lastImportReturnAddress != 0;

    crashReportWriteSection(file, "Summary");
    crashReportWriteField(file, "error", "%s", context.error ? context.error : "");
    crashReportWriteField(file, "backend", "%s", context.backend ? context.backend : "");

    crashReportWriteSection(file, "Crash Location");
    crashReportWriteBoolean(file, "register_snapshot_valid", registers != NULL);
    crashReportWriteField(file, "pc", "0x%08x", pc);
    crashReportWriteField(file, "lr", "0x%08x", lr);
    crashReportWriteField(file, "sp", "0x%08x", sp);
    crashReportWriteField(file, "cpsr", "0x%08x", context.cpsr);
    crashReportWriteBoolean(file, "unsupported_instruction_valid",
        unsupportedInstructionValid);
    crashReportWriteField(file, "unsupported_instruction", "0x%08x", context.unsupportedInstruction);
    crashReportWriteField(file, "unsupported_pc", "0x%08x", context.unsupportedPc);
    crashReportWriteBoolean(file, "fault_valid", faultValid);
    crashReportWriteField(file, "fault_address", "0x%08x", context.faultAddress);
    crashReportWriteField(file, "fault_size", "%u", context.faultSize);
    if (faultValid)
    {
        crashReportWriteField(file, "fault_end_exclusive", "0x%llx",
            (unsigned long long)faultEnd);
        crashReportWriteField(file, "fault_access", "%s",
            context.faultFetch ? "fetch" : (context.faultWrite ? "write" : "read"));
    }
    else
    {
        crashReportWriteField(file, "fault_end_exclusive", "unavailable");
        crashReportWriteField(file, "fault_access", "unavailable");
    }

    crashReportWriteArmRegisters(file, registers);

    crashReportWriteSection(file, "CC Runtime");
    crashReportWriteField(file, "game_path", "%s", context.gamePath ? context.gamePath : "");
    crashReportWriteField(file, "game_sha256", "%s", context.gameSha256 ? context.gameSha256 : "");
    crashReportWriteField(file, "save_directory", "%s",
        context.saveDirectory ? context.saveDirectory : "");
    crashReportWriteField(file, "instructions", "%llu", (unsigned long long)context.instructions);
    crashReportWriteField(file, "import_calls", "%u", context.importCalls);
    crashReportWriteField(file, "unknown_imports", "%u", context.unknownImports);
    crashReportWriteField(file, "frames_submitted", "%u", context.framesSubmitted);
    crashReportWriteField(file, "tasks_created", "%u", context.tasksCreated);
    crashReportWriteBoolean(file, "last_import_valid", lastImportValid);
    crashReportWriteField(file, "last_import", "%s", context.lastImport ? context.lastImport : "");
    crashReportWriteField(file, "last_import_pc", "0x%08x", context.lastImportPc);
    crashReportWriteField(file, "last_import_return", "0x%08x", context.lastImportReturnAddress);
    crashReportWriteBoolean(file, "failed_task_valid", context.failedTaskValid);
    crashReportWriteField(file, "failed_task_index", "%u", context.failedTaskIndex);
    crashReportWriteField(file, "failed_task_entry", "0x%08x", context.failedTaskEntry);
    crashReportWriteField(file, "failed_task_stack", "0x%08x", context.failedTaskStack);
    crashReportWriteField(file, "failed_task_priority", "%u", context.failedTaskPriority);
    crashReportWriteField(file, "failed_task_delay_ticks", "%u", context.failedTaskDelayTicks);

    return crashReportClose(file);
}
