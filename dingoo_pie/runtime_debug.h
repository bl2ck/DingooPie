#ifndef DINGOO_PIE_RUNTIME_DEBUG_H
#define DINGOO_PIE_RUNTIME_DEBUG_H

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common_types.h"

#include "native_runtime.h"

#include <locale.h>
#include <string>

using namespace std;

#ifndef EM_PORT_API
#define EM_PORT_API(rettype) rettype
#endif

#ifndef NULL
#include <stddef.h>
#endif

#ifndef offsetof
#define offsetof(type, field) ((size_t) & ((type *)0)->field)
#endif
#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifdef __x86_64__
#define PRId "I64d"
#define PRIX "I64X"
#elif __i386__
#define PRId "d"
#define PRIX "X"
#endif

#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

#define MAKERGB565(r, g, b) (uint16_t)(((uint32_t)(r >> 3) << 11) | ((uint32_t)(g >> 2) << 5) | ((uint32_t)(b >> 3)))
#define PIXEL565R(v) ((((uint32_t)v >> 11) << 3) & 0xff)
#define PIXEL565G(v) ((((uint32_t)v >> 5) << 2) & 0xff)
#define PIXEL565B(v) (((uint32_t)v << 3) & 0xff)

int wstrlen(char* txt);
void cpsrToStr(uint32_t v, char* out);
const char* memTypeStr(RuntimeMemoryAccess type);
void dumpREG(NativeRuntime* runtime);
void dumpStackCall(NativeRuntime* runtime, uint32_t stack_start_address);
void dumpAsm(NativeRuntime* runtime);
void dumpAsmRange(NativeRuntime* runtime, uint32_t address, uint32_t bytes);
void dumpREG2File(NativeRuntime* runtime, FILE* fp);
void dumpMemStr(void* ptr, size_t len);
void dumpMem(void* buffer, uint32_t count);
char* getSplitStr(char* str, char split, int n);

void toHexString(void* buff, int count, char* out);

uint32_t copyWstrToMrp(char* str);
uint32_t copyStrToMrp(char* str);
void printScreen(char* filename, uint16_t* buf);

int64_t get_uptime_ms(void);
int64_t get_time_ms(void);

std::string WString2String(const std::wstring& ws);
std::wstring String2WString(const std::string& s);

#endif

