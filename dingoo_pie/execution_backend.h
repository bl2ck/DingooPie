#ifndef DINGOO_PIE_EXECUTION_BACKEND_H
#define DINGOO_PIE_EXECUTION_BACKEND_H

enum ExecutionBackend
{
    EXECUTION_BACKEND_INTERPRETER = 0,
    EXECUTION_BACKEND_PPSSPP_IRJIT = 1
};

const char* executionBackendName(ExecutionBackend backend);
ExecutionBackend executionBackendFromName(const char* value, bool* recognized);

#endif
