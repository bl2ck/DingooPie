#include "execution_backend.h"

#include <string.h>

static bool backendNameEquals(const char* value, const char* expected)
{
    return value && strcmp(value, expected) == 0;
}

const char* executionBackendName(ExecutionBackend backend)
{
    switch (backend)
    {
    case EXECUTION_BACKEND_INTERPRETER:
        return "interpreter";
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

    if (backendNameEquals(value, "interpreter") || backendNameEquals(value, "native"))
    {
        return EXECUTION_BACKEND_INTERPRETER;
    }

    if (backendNameEquals(value, "ppsspp_irjit") || backendNameEquals(value, "irjit"))
    {
        return EXECUTION_BACKEND_PPSSPP_IRJIT;
    }

    if (recognized)
    {
        *recognized = false;
    }
    return EXECUTION_BACKEND_INTERPRETER;
}
