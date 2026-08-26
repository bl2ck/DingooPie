#include "shared/execution/execution_backend.h"

#include <string.h>

static bool executionValueEquals(const char* value, const char* expected)
{
    return value && strcmp(value, expected) == 0;
}

const char* executionBackendName(ExecutionBackend backend)
{
    switch (backend)
    {
    case EXECUTION_BACKEND_COMPATIBILITY:
        return "compatibility";
    case EXECUTION_BACKEND_PPSSPP_IRJIT:
        return "ppsspp_irjit";
    default:
        return "unknown";
    }
}

ExecutionBackend executionBackendFromName(const char* value, bool* recognized)
{
    if (recognized)
    {
        *recognized = true;
    }

    if (!value || !value[0])
    {
        return EXECUTION_BACKEND_PPSSPP_IRJIT;
    }

    if (executionValueEquals(value, "compatibility") ||
        executionValueEquals(value, "interpreter") ||
        executionValueEquals(value, "native"))
    {
        return EXECUTION_BACKEND_COMPATIBILITY;
    }

    if (executionValueEquals(value, "ppsspp_irjit") || executionValueEquals(value, "irjit"))
    {
        return EXECUTION_BACKEND_PPSSPP_IRJIT;
    }

    if (recognized)
    {
        *recognized = false;
    }
    return EXECUTION_BACKEND_COMPATIBILITY;
}

const char* runtimeExecutionModeName(RuntimeExecutionMode mode)
{
    return mode == RUNTIME_EXECUTION_MODE_COMPATIBILITY ? "compatibility" : "auto";
}

const char* runtimeExecutionModeConfigValue(RuntimeExecutionMode mode)
{
    return mode == RUNTIME_EXECUTION_MODE_COMPATIBILITY ? "compatibility" : "";
}

RuntimeExecutionMode runtimeExecutionModeFromName(const char* value, bool* recognized)
{
    if (recognized)
    {
        *recognized = true;
    }

    if (!value || !value[0] || executionValueEquals(value, "auto"))
    {
        return RUNTIME_EXECUTION_MODE_AUTOMATIC;
    }

    if (executionValueEquals(value, "compatibility") ||
        executionValueEquals(value, "interpreter") ||
        executionValueEquals(value, "native"))
    {
        return RUNTIME_EXECUTION_MODE_COMPATIBILITY;
    }

    if (recognized)
    {
        *recognized = false;
    }
    return RUNTIME_EXECUTION_MODE_AUTOMATIC;
}

bool runtimeExecutionModeUsesOptimizedBackend(RuntimeExecutionMode mode)
{
    return mode == RUNTIME_EXECUTION_MODE_AUTOMATIC;
}
