#include "cc/runtime/cc_crash_report.h"

#include "shared/platform/storage_services.h"

#include <stdio.h>

bool crashLogWriteCcFailure(const CcCrashLogContext& context,
    std::string* outFileName)
{
    const char* fileName = "DingooPie-cc-crash.log";
    FILE* file = platformOpenStorageFile(platformGetLogDirectory(), fileName, "wb");
    if (!file)
    {
        return false;
    }
    fprintf(file, "game=%s\nsha256=%s\nerror=%s\npc=0x%08x\ncpsr=0x%08x\n",
        context.gamePath ? context.gamePath : "",
        context.gameSha256 ? context.gameSha256 : "",
        context.error ? context.error : "",
        context.registers ? context.registers[15] : 0,
        context.cpsr);
    fclose(file);
    if (outFileName)
    {
        *outFileName = fileName;
    }
    return true;
}
