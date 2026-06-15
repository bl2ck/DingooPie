#ifndef DINGOO_PIE_GUEST_FILESYSTEM_H
#define DINGOO_PIE_GUEST_FILESYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include "app_loader.h"
#include "native_runtime.h"

typedef enum {
    _file_type_file,
    _file_type_mem
} _file_type_e;

typedef struct {
    uint32_t/* _file_type_e */ type;
    uint32_t/* void* */ data;
    uint32_t /*bool*/ eof;
} _file_t;

typedef struct {
    uint32_t /*uintptr_t*/ base;
	uint32_t /* void* */size;
	uint32_t /*uintptr_t*/ offset;
    uint32_t /*bool*/      read, write;
    uint32_t /*bool*/      alloc;
} _file_mem_t;


typedef void FSYS_FILE;
extern void fsys_set_app(app* inApp);
extern void fsys_set_app_identity(const char* sha256Hex);
extern uint32_t fsys_fopen(const char* name, const char* mode);
extern uint32_t vm_fread(void* ptr, uint32_t size, uint32_t count, uint32_t stream);
extern uint32_t fsys_fclose(uint32_t stream);
extern uint32_t fsys_fseek(uint32_t stream, uint32_t offset, uint32_t origin);
extern uint32_t fsys_ftell(uint32_t stream);
extern uint32_t fsys_fwrite(void* ptr, uint32_t size, uint32_t count, uint32_t stream);
extern uint32_t fsys_feof(uint32_t stream);
extern bool fsys_read_cached(uint32_t stream, uint32_t size, uint32_t count, const uint8_t** data, uint32_t* bytesRead, uint32_t* itemsRead);
extern bool fsys_seek_cached(uint32_t stream, uint32_t offset, uint32_t origin, uint32_t* ret);
extern void fsys_begin_fast_hle_call(void);
extern void fsys_end_fast_hle_call(void);
extern void fsys_set_profile_enabled(bool enabled);

#endif

