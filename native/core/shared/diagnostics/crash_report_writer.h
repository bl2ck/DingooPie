#ifndef DINGOO_PIE_SHARED_DIAGNOSTICS_CRASH_REPORT_WRITER_H
#define DINGOO_PIE_SHARED_DIAGNOSTICS_CRASH_REPORT_WRITER_H

#include <stdint.h>
#include <stdio.h>
#include <string>

bool crashReportOpenForGame(
    const std::string& gamePath,
    const char* kind,
    std::string* outFileName,
    FILE** outFile);
void crashReportWriteSection(FILE* file, const char* title);
void crashReportWriteField(FILE* file, const char* key, const char* format, ...);
void crashReportWriteBoolean(FILE* file, const char* key, bool value);
void crashReportWriteAddressOffset(FILE* file, const char* key,
    uint32_t address, uint32_t origin);
void crashReportWriteArmRegisters(FILE* file, const uint32_t* registers);
bool crashReportClose(FILE* file);

#endif
