#ifndef DINGOO_PIE_SHARED_SERVICES_GUEST_FILESYSTEM_H
#define DINGOO_PIE_SHARED_SERVICES_GUEST_FILESYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include "shared/services/guest_package.h"

enum GuestFileType
{
    GUEST_FILE_TYPE_FILE = 0,
    GUEST_FILE_TYPE_MEMORY
};

struct GuestFile
{
    uint32_t type;
    uint32_t data;
    uint32_t eof;
};

struct GuestMemoryFile
{
    uint32_t base;
    uint32_t size;
    uint32_t offset;
    uint32_t read;
    uint32_t write;
    uint32_t alloc;
};

extern void fsys_set_guest_package(GuestPackage* guestPackage);
extern void fsys_reset_guest_package(GuestPackage* guestPackage);
extern void fsys_set_game_identity(const char* sha256Hex);
extern void fsys_set_game_name(const char* gameName);
extern void fsys_set_save_directory(const char* directory);
extern bool fsys_saw_suspicious_open_failure(void);
extern bool fsys_saw_successful_save_write(void);
extern uint32_t fsys_fopen(const char* name, const char* mode);
extern uint32_t vm_fread(void* ptr, uint32_t size, uint32_t count, uint32_t stream);
extern uint32_t fsys_fclose(uint32_t stream);
extern uint32_t fsys_fseek(uint32_t stream, uint32_t offset, uint32_t origin);
extern uint32_t fsys_ftell(uint32_t stream);
extern uint32_t fsys_fwrite(void* ptr, uint32_t size, uint32_t count, uint32_t stream);
extern uint32_t fsys_feof(uint32_t stream);
extern bool fsys_read_cached(uint32_t stream, uint32_t size, uint32_t count, const uint8_t** data, uint32_t* bytesRead, uint32_t* itemsRead);
extern bool fsys_seek_cached(uint32_t stream, uint32_t offset, uint32_t origin, uint32_t* ret);
extern bool fsys_stream_is_guest_package(uint32_t stream);
extern bool fsys_stream_is_external_file(uint32_t stream);
extern GuestResourceEntry* fsys_stream_resource(uint32_t stream);
extern uint32_t fsys_stream_position(uint32_t stream);
extern const char* fsys_stream_request_name(uint32_t stream);
extern void fsys_record_load_to_guest(
    uint32_t stream,
    uint32_t guestAddress,
    const void* hostData,
    uint32_t positionBefore);
extern void fsys_begin_fast_hle_call(void);
extern void fsys_end_fast_hle_call(void);
extern void fsys_set_profile_enabled(bool enabled);

#endif
