#ifndef DINGOO_PIE_DEBUG_CONSOLE_H
#define DINGOO_PIE_DEBUG_CONSOLE_H

#include "shared/diagnostics/debug_log.h"

bool debugConsoleOpen(void);
void debugConsoleClose(void);
const char* debugLogFileName(void);
#ifdef _WIN32
const wchar_t* debugLogFileNameWide(void);
#endif

#endif
