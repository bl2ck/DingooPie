#include "cc/runtime/cc_crash_report.h"

#include "shared/platform/storage_services.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

static void crashLogWriteUnderline(FILE* file, char character, size_t length)
{
    for (size_t index = 0; index < length; ++index)
    {
        fputc(character, file);
    }
    fputc('\n', file);
}

static void crashLogWriteSection(FILE* file, const char* title)
{
    fprintf(file, "\n%s\n", title);
    crashLogWriteUnderline(file, '-', strlen(title));
}

static void crashLogWriteField(FILE* file, const char* key, const char* format, ...)
{
    fprintf(file, "%s=", key);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(file, format, arguments);
    va_end(arguments);
    fputc('\n', file);
}

bool crashLogWriteCcFailure(const CcCrashLogContext& context,
    std::string* outFileName)
{
    const char* fileName = "DingooPie-cc-crash.log";
    FILE* file = platformOpenStorageFile(platformGetLogDirectory(), fileName, "wb");
    if (!file)
    {
        return false;
    }
    setvbuf(file, NULL, _IONBF, 0);

    const std::string timestamp = crashLogTimestamp();
    const uint32_t* registers = context.registers;
    const uint32_t pc = registers ? registers[15] : 0;
    const uint32_t lr = registers ? registers[14] : 0;
    const uint32_t sp = registers ? registers[13] : 0;

    fprintf(file, "DingooPie Crash Report\n");
    crashLogWriteUnderline(file, '=', strlen("DingooPie Crash Report"));

    crashLogWriteSection(file, "Summary");
    crashLogWriteField(file, "timestamp", "%s", timestamp.c_str());
    crashLogWriteField(file, "kind", "cc-runtime-failure");
    crashLogWriteField(file, "error", "%s", context.error ? context.error : "");
    crashLogWriteField(file, "backend", "%s", context.backend ? context.backend : "");

    crashLogWriteSection(file, "Crash Location");
    crashLogWriteField(file, "pc", "0x%08x", pc);
    crashLogWriteField(file, "lr", "0x%08x", lr);
    crashLogWriteField(file, "sp", "0x%08x", sp);
    crashLogWriteField(file, "cpsr", "0x%08x", context.cpsr);
    crashLogWriteField(file, "unsupported_instruction", "0x%08x", context.unsupportedInstruction);
    crashLogWriteField(file, "unsupported_pc", "0x%08x", context.unsupportedPc);
    crashLogWriteField(file, "fault_address", "0x%08x", context.faultAddress);
    crashLogWriteField(file, "fault_size", "%u", context.faultSize);
    crashLogWriteField(file, "fault_access", "%s", context.faultFetch ? "fetch" :
        (context.faultWrite ? "write" : "read"));

    crashLogWriteSection(file, "ARM Registers");
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            const uint32_t index = row * 4 + column;
            fprintf(file, "r%-2u=0x%08x%s", index,
                registers ? registers[index] : 0,
                column == 3 ? "\n" : "  ");
        }
    }

    crashLogWriteSection(file, "CC Runtime");
    crashLogWriteField(file, "game_path", "%s", context.gamePath ? context.gamePath : "");
    crashLogWriteField(file, "game_sha256", "%s", context.gameSha256 ? context.gameSha256 : "");
    crashLogWriteField(file, "instructions", "%llu", (unsigned long long)context.instructions);
    crashLogWriteField(file, "import_calls", "%u", context.importCalls);
    crashLogWriteField(file, "unknown_imports", "%u", context.unknownImports);
    crashLogWriteField(file, "frames_submitted", "%u", context.framesSubmitted);
    crashLogWriteField(file, "tasks_created", "%u", context.tasksCreated);
    crashLogWriteField(file, "last_import", "%s", context.lastImport ? context.lastImport : "");
    crashLogWriteField(file, "last_import_pc", "0x%08x", context.lastImportPc);
    crashLogWriteField(file, "last_import_return", "0x%08x", context.lastImportReturnAddress);
    crashLogWriteField(file, "failed_task_index", "%u", context.failedTaskIndex);
    crashLogWriteField(file, "failed_task_entry", "0x%08x", context.failedTaskEntry);
    crashLogWriteField(file, "failed_task_stack", "0x%08x", context.failedTaskStack);
    crashLogWriteField(file, "failed_task_priority", "%u", context.failedTaskPriority);
    crashLogWriteField(file, "failed_task_delay_ticks", "%u", context.failedTaskDelayTicks);

    fclose(file);
    if (outFileName)
    {
        *outFileName = fileName;
    }
    return true;
}
