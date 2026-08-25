#ifndef DINGOO_PIE_SHARED_SERVICES_GUEST_PACKAGE_H
#define DINGOO_PIE_SHARED_SERVICES_GUEST_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Parsed import entry shared by APP and CC package containers.
struct GuestImportEntry
{
	uint32_t  offset;
	char*     name;
};

// Parsed export entry shared by APP and CC package containers.
struct GuestExportEntry
{
	uint32_t offset;
	char*    name;
};

// Resource entry discovered from ERPT metadata or packed resource tables.
struct GuestResourceEntry
{
	char*    name;
	uint32_t offset;
	uint32_t size;
	uint8_t  xorKey;
	uint8_t* decoded_data;
};

// Format-neutral in-memory representation used by APP and CC runtimes.
struct GuestPackage
{
	uint32_t           import_count;
	GuestImportEntry** import_data;
	uint32_t           export_count;
	GuestExportEntry** export_data;
	uint32_t           resource_count;
	GuestResourceEntry* resource_data;
	uint32_t           bin_size;
	void*              bin_data;
	uint32_t           file_size;
	uint8_t*           file_data;
	uint32_t           bin_entry;
	uint32_t           origin;
	uint32_t           prog_size;
	uint32_t           bin_bss;
};

extern GuestPackage* guestPackageCreate(FILE* file, uint32_t fileSize);
extern bool guestPackageProbeFileHeader(FILE* file, uint32_t fileSize);
extern void guestPackageDestroy(GuestPackage* package);
extern GuestResourceEntry* guestPackageFindResource(
	GuestPackage* package, const char* name);
extern void guestPackageTraceResourceCandidates(
	GuestPackage* package, const char* name);
extern const uint8_t* guestPackageResourceData(
	GuestPackage* package, GuestResourceEntry* entry);

#endif
