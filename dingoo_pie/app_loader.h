#ifndef DINGOO_PIE_APP_LOADER_H
#define DINGOO_PIE_APP_LOADER_H


#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Parsed Dingoo Technology .app import table entry.
typedef struct {
	uint32_t  offset;
	char*     name;
} app_import_entry;

// Parsed Dingoo Technology .app export table entry.
typedef struct {
	uint32_t offset;
	char*    name;
} app_export_entry;

// Resource entry discovered from ERPT metadata or packed resource tables.
typedef struct {
	char*    name;
	uint32_t offset;
	uint32_t size;
	uint8_t  xor_key;
	uint8_t* decoded_data;
} app_resource_entry;

// In-memory representation of a Dingoo Technology .app container.
typedef struct {
	uint32_t           import_count;
	app_import_entry** import_data;
	uint32_t           export_count;
	app_export_entry** export_data;
	uint32_t           resource_count;
	app_resource_entry* resource_data;
	uint32_t           bin_size;
	void*              bin_data;
	uint32_t           file_size;
	uint8_t*           file_data;
	uint32_t           bin_entry;
	uint32_t		   origin;
	uint32_t		   prog_size;
	uint32_t           bin_bss;
} app;



extern app* app_create(FILE* file, uint32_t fileSize);
extern bool app_probe_file_header(FILE* file, uint32_t fileSize);
extern void app_delete(app* inApp);
extern app_resource_entry* app_find_resource(app* inApp, const char* inName);
extern void app_trace_resource_candidates(app* inApp, const char* inName);
extern const uint8_t* app_resource_data(app* inApp, app_resource_entry* inEntry);

extern bool app_import_add(app* inApp, const char* inName, uint32_t inOffset);
extern bool app_export_add(app* inApp, const char* inName, uint32_t inOffset);

extern bool app_save(app* inApp, const char* inPath);


#endif

