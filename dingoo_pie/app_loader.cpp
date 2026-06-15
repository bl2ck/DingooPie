#include "app_loader.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <strings.h>
#include "runtime_debug.h"

// Dingoo Technology .app containers are fixed-size little-endian chunks
// followed by raw MIPS code and optional resource tables. Several header words
// are still known only by observation, so they stay reserved instead of being
// guessed into public structure names.
#ifdef _WIN32
#pragma pack(1)
#endif
typedef struct {
	char     ident[4];
	uint8_t  unknown[20];
	uint8_t  padding[8];
#ifndef _WIN32
} __attribute__((__packed__)) _app_ccdl;
#else
} _app_ccdl;
#pragma pack()
#endif

static const _app_ccdl _app_ccdl_default = {
	{ 'C', 'C', 'D', 'L' },
	{
		0x00, 0x00, 0x01, 0x00,
		0x01, 0x00, 0x02, 0x00,
		0x04, 0x00, 0x00, 0x00,
		0x20, 0x09, 0x06, 0x24,
		0x19, 0x24, 0x42, 0x00
	},
	{
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	}
};


#ifdef _WIN32
#pragma pack(1)
#endif
typedef struct {
	char     ident[4];
	uint32_t unknown;
	uint32_t offset;
	uint32_t size;
	uint8_t  padding[16];
#ifndef _WIN32
} __attribute__((__packed__)) _app_impt;
#else
} _app_impt;
#pragma pack()
#endif

static const _app_impt _app_impt_default = {
	{ 'I', 'M', 'P', 'T' },
	0x00000008,
	0,
	0,
	{
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	}
};

#ifdef _WIN32
#pragma pack(1)
#endif
typedef struct {
	char     ident[4];
	uint32_t unknown;
	uint32_t offset;
	uint32_t size;
	uint8_t  padding[16];
#ifndef _WIN32
} __attribute__((__packed__)) _app_expt;
#else
} _app_expt;
#pragma pack()
#endif

static const _app_expt _app_expt_default = {
	{ 'E', 'X', 'P', 'T' },
	0x00000009,
	0,
	0,
	{
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00
	}
};


#ifdef _WIN32
#pragma pack(1)
#endif
typedef struct {
	char     ident[4];
	uint32_t unknown0;
	uint32_t offset;
	uint32_t size;
	uint32_t unknown1;
	uint32_t entry;
	uint32_t origin;
	uint32_t prog_size;
#ifndef _WIN32
} __attribute__((__packed__)) _app_rawd;
#else
} _app_rawd;
#pragma pack()
#endif

static const _app_rawd _app_rawd_default = {
	{ 'R', 'A', 'W', 'D' },
	0x00000001,
	0,
	0,
	0x00000000,
	0x00000000,
	0x80A00000,
	0
};


#ifdef _WIN32
#pragma pack(1)
#endif
typedef struct {
	uint32_t str_offset;
	uint32_t unknown[2];
	uint32_t offset;
#ifndef _WIN32
} __attribute__((__packed__)) _app_impt_entry;
#else
} _app_impt_entry;
#pragma pack()
#endif

static const _app_impt_entry _app_impt_entry_default = {
	0, { 0, 0x00020000 }, 0
};


#ifdef _WIN32
#pragma pack(1)
#endif
typedef struct {
	uint32_t str_offset;
	uint32_t unknown[2];
	uint32_t offset;
#ifndef _WIN32
} __attribute__((__packed__)) _app_expt_entry;
#else
} _app_expt_entry;
#pragma pack()
#endif

static const _app_expt_entry _app_expt_entry_default = {
	0, { 0, 0x00020000 }, 0
};




uintptr_t _app_strlen(const char* inString) {
	uintptr_t tempLen = strlen(inString);
	tempLen += (4 - (tempLen & 3));
	return tempLen;
}

static uint16_t app_read_u16(const uint8_t* data, uint32_t offset)
{
	return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static uint32_t app_read_u32(const uint8_t* data, uint32_t offset)
{
	return (uint32_t)data[offset] |
		((uint32_t)data[offset + 1] << 8) |
		((uint32_t)data[offset + 2] << 16) |
		((uint32_t)data[offset + 3] << 24);
}

static bool app_resource_append(app* inApp, const char* inName, uint32_t inOffset, uint32_t inSize, uint8_t xorKey)
{
	if (!inApp || !inName || !inName[0] || inSize == 0)
	{
		return false;
	}

	app_resource_entry* resized = (app_resource_entry*)realloc(
		inApp->resource_data,
		sizeof(app_resource_entry) * (inApp->resource_count + 1));
	if (!resized)
	{
		assert(0);
		return false;
	}
	inApp->resource_data = resized;

	size_t nameLen = strlen(inName);
	app_resource_entry* entry = &inApp->resource_data[inApp->resource_count];
	memset(entry, 0x00, sizeof(*entry));
	entry->name = (char*)malloc(nameLen + 1);
	if (!entry->name)
	{
		assert(0);
		return false;
	}
	memcpy(entry->name, inName, nameLen + 1);
	entry->offset = inOffset;
	entry->size = inSize;
	entry->xor_key = xorKey;
	inApp->resource_count++;
	return true;
}

static bool app_trace_resources_enabled(void)
{
	const char* value = getenv("DINGOO_PIE_TRACE_RESOURCES");
	return value && value[0] && strcmp(value, "0") != 0;
}

static bool app_resource_name_char_ok(uint8_t c)
{
	return c >= 0x20 && c <= 0x7e;
}

static int app_resource_name_len(const uint8_t* data, uint32_t maxLen)
{
	for (uint32_t i = 0; i < maxLen; ++i)
	{
		if (data[i] == 0)
		{
			return (int)i;
		}
		if (!app_resource_name_char_ok(data[i]))
		{
			return -1;
		}
	}
	return -1;
}

static bool app_resource_known_extension(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (!dot)
	{
		return false;
	}

	char ext[16] = { 0 };
	size_t len = strlen(dot);
	if (len >= sizeof(ext))
	{
		return false;
	}
	for (size_t i = 0; i < len; ++i)
	{
		ext[i] = (char)tolower((unsigned char)dot[i]);
	}

	return strcmp(ext, ".war") == 0 ||
		strcmp(ext, ".pcm") == 0 ||
		strcmp(ext, ".dat") == 0 ||
		strcmp(ext, ".txt") == 0 ||
		strcmp(ext, ".log") == 0;
}

typedef struct {
	uint32_t base;
	uint32_t count;
	uint32_t table_end;
	int score;
} packed_resource_table;

static bool app_packed_table_probe(app* inApp, uint32_t base, uint32_t inSize, packed_resource_table* out)
{
	if (!inApp || !inApp->file_data || base + 2 > inSize)
	{
		return false;
	}

	uint32_t count = app_read_u16(inApp->file_data, base);
	const uint32_t recordSize = 36;
	const uint32_t nameSize = 32;
	uint32_t tableEnd = base + 2 + count * recordSize;
	if (count == 0 || count > 1024 || tableEnd > inSize)
	{
		return false;
	}

	uint32_t tableSize = 2 + count * recordSize;
	uint32_t validNames = 0;
	uint32_t knownNames = 0;
	uint32_t validOffsets = 0;
	uint32_t lastOffset = tableSize;
	char nameBuf[33];

	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t rec = base + 2 + i * recordSize;
		int nameLen = app_resource_name_len(inApp->file_data + rec, nameSize);
		uint32_t relOffset = app_read_u32(inApp->file_data, rec + nameSize);
		if (relOffset >= tableSize && base + relOffset < inSize)
		{
			validOffsets++;
			if (relOffset >= lastOffset)
			{
				lastOffset = relOffset;
			}
		}

		if (nameLen <= 0)
		{
			continue;
		}

		memset(nameBuf, 0x00, sizeof(nameBuf));
		memcpy(nameBuf, inApp->file_data + rec, (size_t)nameLen);
		validNames++;
		if (app_resource_known_extension(nameBuf))
		{
			knownNames++;
		}
	}

	if (validNames < count * 8 / 10 || validOffsets < count * 8 / 10 || knownNames < 8)
	{
		return false;
	}

	out->base = base;
	out->count = count;
	out->table_end = tableEnd;
	out->score = (int)knownNames;
	return true;
}

static uint32_t app_packed_next_offset(app* inApp, const packed_resource_table* table, uint32_t index, uint32_t currentOffset, uint32_t packageEnd)
{
	const uint32_t recordSize = 36;
	const uint32_t nameSize = 32;
	uint32_t best = packageEnd - table->base;
	for (uint32_t i = index + 1; i < table->count; ++i)
	{
		uint32_t rec = table->base + 2 + i * recordSize;
		uint32_t relOffset = app_read_u32(inApp->file_data, rec + nameSize);
		if (relOffset > currentOffset && relOffset < best)
		{
			best = relOffset;
		}
	}
	return best;
}

static void app_parse_packed_table(app* inApp, const packed_resource_table* table, uint32_t packageEnd)
{
	const uint32_t recordSize = 36;
	const uint32_t nameSize = 32;
	char nameBuf[33];

	for (uint32_t i = 0; i < table->count; ++i)
	{
		uint32_t rec = table->base + 2 + i * recordSize;
		int nameLen = app_resource_name_len(inApp->file_data + rec, nameSize);
		uint32_t relOffset = app_read_u32(inApp->file_data, rec + nameSize);
		if (nameLen <= 0 || relOffset < (table->table_end - table->base) || table->base + relOffset >= packageEnd)
		{
			continue;
		}

		uint32_t nextOffset = app_packed_next_offset(inApp, table, i, relOffset, packageEnd);
		if (nextOffset <= relOffset || table->base + nextOffset > packageEnd)
		{
			continue;
		}

		memset(nameBuf, 0x00, sizeof(nameBuf));
		memcpy(nameBuf, inApp->file_data + rec, (size_t)nameLen);
		app_resource_append(inApp, nameBuf, table->base + relOffset, nextOffset - relOffset, 0);
	}
}

static void app_parse_packed_resources(app* inApp, uint32_t rawEnd, uint32_t inSize)
{
	const uint32_t maxTables = 8;
	packed_resource_table tables[maxTables];
	uint32_t tableCount = 0;
	uint32_t scan = ALIGN(rawEnd, 0x1000);

	for (uint32_t base = scan; base + 2 < inSize && tableCount < maxTables; base += 0x1000)
	{
		packed_resource_table table;
		if (app_packed_table_probe(inApp, base, inSize, &table))
		{
			tables[tableCount++] = table;
		}
	}

	for (uint32_t i = 0; i < tableCount; ++i)
	{
		uint32_t packageEnd = (i + 1 < tableCount) ? tables[i + 1].base : inSize;
		app_parse_packed_table(inApp, &tables[i], packageEnd);
	}

	if (tableCount > 0)
	{
		printf("app-loader: packed_resource_tables=%u resources=%u\n", tableCount, inApp->resource_count);
	}
}

static int app_resource_name_equal(const char* a, const char* b)
{
	if (!a || !b)
	{
		return 0;
	}

	while (a[0] == '.' && (a[1] == '\\' || a[1] == '/'))
	{
		a += 2;
	}
	while (b[0] == '.' && (b[1] == '\\' || b[1] == '/'))
	{
		b += 2;
	}
	while (*a == '\\' || *a == '/')
	{
		a++;
	}
	while (*b == '\\' || *b == '/')
	{
		b++;
	}

	while (*a && *b)
	{
		char ca = (*a == '/') ? '\\' : *a;
		char cb = (*b == '/') ? '\\' : *b;
		ca = (char)tolower((unsigned char)ca);
		cb = (char)tolower((unsigned char)cb);
		if (ca != cb)
		{
			return 0;
		}
		a++;
		b++;
	}

	return *a == 0 && *b == 0;
}

static const char* app_resource_basename(const char* name)
{
	if (!name)
	{
		return NULL;
	}

	const char* base = name;
	for (const char* p = name; *p; ++p)
	{
		if (*p == '\\' || *p == '/' || *p == ':')
		{
			base = p + 1;
		}
	}
	return base;
}

app* app_create(FILE* tempFile, uint32_t inSize)
{
	int i = 0;
	app* tempApp = (app*)malloc(sizeof(app));
	if (!tempApp)
	{
		return NULL;
	}

	memset(tempApp, 0x00, sizeof(app));

	tempApp->file_size = inSize;
	tempApp->file_data = (uint8_t*)malloc(inSize);
	if (!tempApp->file_data)
	{
		assert(0);
	}
	fseek(tempFile, 0, SEEK_SET);
	fread(tempApp->file_data, inSize, 1, tempFile);
	fseek(tempFile, 0, SEEK_SET);

	_app_ccdl tempCCDL;
	_app_impt tempIMPT;
	_app_expt tempEXPT;
	_app_rawd tempRAWD;
	_app_impt tempERPT;

	fread(&tempCCDL, sizeof(_app_ccdl), 1, tempFile);
	fread(&tempIMPT, sizeof(_app_impt), 1, tempFile);
	fread(&tempEXPT, sizeof(_app_expt), 1, tempFile);
	fread(&tempRAWD, sizeof(_app_rawd), 1, tempFile);
	fread(&tempERPT, sizeof(_app_impt), 1, tempFile);

	// read Import Table
	fseek(tempFile, tempIMPT.offset, SEEK_SET);
	_app_impt_entry tempIHeader = { 0, { 0, 0 }, 0 };
	fread(&tempIHeader, sizeof(_app_impt_entry), 1, tempFile);
	tempApp->import_count = tempIHeader.str_offset;
	_app_impt_entry *tempIEntry = (_app_impt_entry*)malloc(sizeof(_app_impt_entry) * tempApp->import_count);
	if (!tempIEntry)
	{
		assert(0);
	}
	for (i = 0; i < tempApp->import_count; i++) {
		fread(&tempIEntry[i], sizeof(_app_impt_entry), 1, tempFile);
	}

	tempApp->import_data = (app_import_entry**)malloc(sizeof(app_import_entry*) * tempApp->import_count);

	// read Import Strings
	for (i = 0; i < tempApp->import_count; i++)
	{
		app_import_entry* entry = (app_import_entry*)malloc(sizeof(app_import_entry));
		if (!entry)
		{
			assert(0);
		}
		memset(entry, 0x00, sizeof(app_import_entry));
		entry->offset = tempIEntry[i].offset;

		entry->name = (char*)malloc(32);
		if (!entry->name)
		{
			assert(0);
		}
		memset(entry->name, 0x00, 32);
		for (int j = 0; j < 32; j++)
		{
			fread(entry->name + j, 1, 1, tempFile);
			if (entry->name[j] == '\0')
			{
				int padding_len = (4 - ((j + 1) % 4)) % 4;
				fread(entry->name + j + 1, padding_len, 1, tempFile);
				break;
			}
		}

		tempApp->import_data[i] = entry;
	}


	// read Export Table
	fseek(tempFile, tempEXPT.offset, SEEK_SET);
	_app_expt_entry tempEHeader = { 0, { 0, 0 }, 0 };
	fread(&tempEHeader, sizeof(_app_expt_entry), 1, tempFile);
	tempApp->export_count = tempEHeader.str_offset;
	_app_expt_entry* tempEEntry = (_app_expt_entry*)malloc(sizeof(_app_expt_entry) * tempApp->export_count);
	if (!tempEEntry)
	{
		assert(0);
	}
	for (i = 0; i < tempApp->export_count; i++) {
		fread(&tempEEntry[i], sizeof(_app_expt_entry), 1, tempFile);
	}
	tempApp->export_data = (app_export_entry**)malloc(sizeof(app_export_entry*) * tempApp->export_count);
	// read Export Strings
	for (i = 0; i < tempApp->export_count; i++)
	{
		app_export_entry* entry = (app_export_entry*)malloc(sizeof(app_export_entry));
		if (!entry)
		{
			assert(0);
		}
		memset(entry, 0x00, sizeof(app_export_entry));
		entry->offset = tempEEntry[i].offset;

		entry->name = (char*)malloc(32);
		if (!entry->name)
		{
			assert(0);
		}
		memset(entry->name, 0x00, 32);
		for (int j = 0; j < 32; j++)
		{
			fread(entry->name + j, 1, 1, tempFile);
			if (entry->name[j] == '\0')
			{
				int padding_len = (4 - ((j + 1) % 4)) % 4;
				fread(entry->name + j + 1, padding_len, 1, tempFile);
				break;
			}
		}

		tempApp->export_data[i] = entry;
	}

	// read Binary Data
	fseek(tempFile, tempRAWD.offset, SEEK_SET);
	tempApp->bin_size = tempRAWD.size;

	//4k ALIGN
	uint32_t memory_align = ALIGN(tempRAWD.prog_size, 4096);
	tempApp->bin_data = malloc(memory_align);
	if (!tempApp->bin_data)
	{
		assert(0);
	}
	memset(tempApp->bin_data, 0x00, memory_align);
	fread(tempApp->bin_data, tempApp->bin_size, 1, tempFile);

	tempApp->bin_size = memory_align;

	tempApp->bin_entry = tempRAWD.entry;
	tempApp->bin_bss = tempRAWD.prog_size - tempRAWD.size;
	tempApp->origin = tempRAWD.origin;
	tempApp->prog_size = tempRAWD.prog_size;

	if (memcmp(tempERPT.ident, "ERPT", 4) == 0 && tempERPT.offset + 4 <= inSize)
	{
		uint32_t count = app_read_u32(tempApp->file_data, tempERPT.offset);
		const uint32_t recordSize = 0x1fc;
		const uint32_t nameSize = 0x1f4;
		uint32_t tableEnd = tempERPT.offset + 4 + count * recordSize;
		if (count > 0 && count < 4096 && tableEnd <= inSize)
		{
			for (i = 0; i < (int)count; ++i)
			{
				uint32_t rec = tempERPT.offset + 4 + i * recordSize;
				uint32_t size = app_read_u32(tempApp->file_data, rec + nameSize);
				uint32_t relOffset = app_read_u32(tempApp->file_data, rec + nameSize + 4);
				uint32_t dataOffset = tempERPT.offset + relOffset;
				char* name = (char*)(tempApp->file_data + rec);
				size_t nameLen = strnlen(name, nameSize);
				if (nameLen == 0 || dataOffset > inSize || size > inSize - dataOffset)
				{
					continue;
				}

				char nameBuf[0x1f5];
				memset(nameBuf, 0x00, sizeof(nameBuf));
				memcpy(nameBuf, name, nameLen);
				app_resource_append(tempApp, nameBuf, dataOffset, size, 0x40);
				if (app_trace_resources_enabled())
				{
					printf("app-loader: trace-resource erpt name=%s offset=0x%08x size=0x%08x xor=0x40\n",
						nameBuf, dataOffset, size);
				}
			}

			printf("app-loader: erpt resources=%u\n", tempApp->resource_count);
		}
	}

	if (tempApp->resource_count == 0)
	{
		app_parse_packed_resources(tempApp, tempRAWD.offset + tempRAWD.size, inSize);
	}

	return tempApp;
}

bool app_probe_file_header(FILE* file, uint32_t fileSize)
{
	if (!file || fileSize < sizeof(_app_ccdl) + sizeof(_app_impt) + sizeof(_app_expt) + sizeof(_app_rawd))
	{
		return false;
	}

	_app_ccdl ccdl;
	_app_impt impt;
	_app_expt expt;
	_app_rawd rawd;
	fseek(file, 0, SEEK_SET);
	if (fread(&ccdl, sizeof(ccdl), 1, file) != 1 ||
		fread(&impt, sizeof(impt), 1, file) != 1 ||
		fread(&expt, sizeof(expt), 1, file) != 1 ||
		fread(&rawd, sizeof(rawd), 1, file) != 1)
	{
		fseek(file, 0, SEEK_SET);
		return false;
	}
	fseek(file, 0, SEEK_SET);

	return memcmp(ccdl.ident, "CCDL", 4) == 0 &&
		memcmp(impt.ident, "IMPT", 4) == 0 &&
		memcmp(expt.ident, "EXPT", 4) == 0 &&
		memcmp(rawd.ident, "RAWD", 4) == 0 &&
		impt.offset < fileSize &&
		expt.offset < fileSize &&
		rawd.offset < fileSize &&
		rawd.size > 0 &&
		rawd.entry != 0 &&
		rawd.prog_size >= rawd.size;
}


void app_delete(app* inApp) {
	if(inApp == NULL)
		return;

	uintptr_t i;

	if(inApp->import_data != NULL) {
		for(i = 0; i < inApp->import_count; i++)
			free(inApp->import_data[i]);
		free(inApp->import_data);
	}

	if(inApp->export_data != NULL) {
		for(i = 0; i < inApp->export_count; i++)
			free(inApp->export_data[i]);
		free(inApp->export_data);
	}

	if (inApp->resource_data != NULL) {
		for (i = 0; i < inApp->resource_count; i++)
		{
			free(inApp->resource_data[i].name);
			free(inApp->resource_data[i].decoded_data);
		}
		free(inApp->resource_data);
	}

	if(inApp->bin_data != NULL)
		free(inApp->bin_data);

	if (inApp->file_data != NULL)
		free(inApp->file_data);

	free(inApp);
}

app_resource_entry* app_find_resource(app* inApp, const char* inName)
{
	if (!inApp || !inName)
	{
		return NULL;
	}

	for (uint32_t i = 0; i < inApp->resource_count; ++i)
	{
		if (app_resource_name_equal(inApp->resource_data[i].name, inName))
		{
			return &inApp->resource_data[i];
		}
	}

	const char* requestBase = app_resource_basename(inName);
	if (requestBase && requestBase[0] && requestBase != inName)
	{
		for (uint32_t i = 0; i < inApp->resource_count; ++i)
		{
			const char* resourceBase = app_resource_basename(inApp->resource_data[i].name);
			if (app_resource_name_equal(resourceBase, requestBase))
			{
				return &inApp->resource_data[i];
			}
		}
	}

	return NULL;
}

void app_trace_resource_candidates(app* inApp, const char* inName)
{
	if (!app_trace_resources_enabled() || !inApp || !inName)
	{
		return;
	}

	const char* requestBase = app_resource_basename(inName);
	for (uint32_t i = 0; i < inApp->resource_count; ++i)
	{
		const char* resourceBase = app_resource_basename(inApp->resource_data[i].name);
		if (app_resource_name_equal(inApp->resource_data[i].name, inName) ||
			(requestBase && resourceBase && app_resource_name_equal(resourceBase, requestBase)))
		{
			printf("app-loader: trace-resource candidate request=%s name=%s offset=0x%08x size=0x%08x xor=0x%02x\n",
				inName,
				inApp->resource_data[i].name,
				inApp->resource_data[i].offset,
				inApp->resource_data[i].size,
				inApp->resource_data[i].xor_key);
		}
	}
}

const uint8_t* app_resource_data(app* inApp, app_resource_entry* inEntry)
{
	if (!inApp || !inEntry || inEntry->offset > inApp->file_size || inEntry->size > inApp->file_size - inEntry->offset)
	{
		return NULL;
	}

	if (!inEntry->xor_key)
	{
		return inApp->file_data + inEntry->offset;
	}

	if (!inEntry->decoded_data)
	{
		inEntry->decoded_data = (uint8_t*)malloc(inEntry->size);
		if (!inEntry->decoded_data)
		{
			assert(0);
			return NULL;
		}

		const uint8_t* src = inApp->file_data + inEntry->offset;
		for (uint32_t i = 0; i < inEntry->size; ++i)
		{
			inEntry->decoded_data[i] = src[i] ^ inEntry->xor_key;
		}
	}

	return inEntry->decoded_data;
}



bool app_import_add(app* inApp, const char* inName, uint32_t inOffset) {
	if((inApp == NULL) || (inName == NULL))
		return false;

	app_import_entry* tempEntry = (app_import_entry*)malloc(sizeof(app_import_entry) + strlen(inName) + 1);
	if(tempEntry == NULL)
		return false;
	tempEntry->offset = inOffset;
	tempEntry->name = (char*)((uintptr_t)tempEntry + sizeof(app_import_entry));
	strcpy(tempEntry->name, inName);

	app_import_entry** tempRealloc = (app_import_entry**)realloc(inApp->import_data, (sizeof(app_import_entry*) * (inApp->import_count + 1)));
	if(tempRealloc == NULL) {
		free(tempEntry);
		return false;
	}

	inApp->import_data = tempRealloc;
	inApp->import_data[inApp->import_count] = tempEntry;
	inApp->import_count++;

	return true;
}

bool app_export_add(app* inApp, const char* inName, uint32_t inOffset) {
	if((inApp == NULL) || (inName == NULL))
		return false;

	app_export_entry* tempEntry = (app_export_entry*)malloc(sizeof(app_export_entry) + strlen(inName) + 1);
	if(tempEntry == NULL)
		return false;
	tempEntry->offset = inOffset;
	tempEntry->name = (char*)((uintptr_t)tempEntry + sizeof(app_export_entry));
	strcpy(tempEntry->name, inName);

	app_export_entry** tempRealloc = (app_export_entry**)realloc(inApp->export_data, (sizeof(app_export_entry*) * (inApp->export_count + 1)));
	if(tempRealloc == NULL) {
		free(tempEntry);
		return false;
	}

	inApp->export_data = tempRealloc;
	inApp->export_data[inApp->export_count] = tempEntry;
	inApp->export_count++;

	return true;
}



void _fprint_string(const char* inString, FILE* inStream) {
	uintptr_t tempLen = strlen(inString);
	fwrite(inString, 1, tempLen, inStream);
	uintptr_t i;
	for(i = 0; i < (4 - (tempLen & 3)); i++)
		fwrite(&inString[tempLen], 1, 1, inStream);
}

bool app_save(app* inApp, const char* inPath) {
	if((inApp == NULL) || (inPath == NULL))
		return false;

	uintptr_t i;

	_app_ccdl tempCCDL = _app_ccdl_default;
	_app_impt tempIMPT = _app_impt_default;
	_app_expt tempEXPT = _app_expt_default;
	_app_rawd tempRAWD = _app_rawd_default;

	uint32_t tempPadding[4] = { 0, 0, 0, 0 };

	tempIMPT.offset = sizeof(_app_ccdl) + sizeof(_app_impt) + sizeof(_app_expt) + sizeof(_app_rawd);

	tempIMPT.size = (sizeof(_app_impt_entry) * (inApp->import_count + 1));
	for(i = 0; i < inApp->import_count; i++)
		tempIMPT.size += _app_strlen(inApp->import_data[i]->name);
	uintptr_t tempIPad = (16 - (tempIMPT.size & 15)) & 15;
	tempIMPT.size += tempIPad;


	tempEXPT.offset = tempIMPT.offset + tempIMPT.size;
	tempEXPT.size = (sizeof(_app_expt_entry) * (inApp->export_count + 1));
	for(i = 0; i < inApp->export_count; i++)
		tempEXPT.size += _app_strlen(inApp->export_data[i]->name);
	uintptr_t tempEPad = (16 - (tempEXPT.size & 15)) & 15;
	tempEXPT.size += tempEPad;

	tempRAWD.offset = tempEXPT.offset + tempEXPT.size;
	tempRAWD.size   = (inApp->bin_size + 15) & 0xFFFFFFF0;
	tempRAWD.origin = 0x80A00000;
	tempRAWD.entry  = inApp->bin_entry;
	tempRAWD.prog_size = tempRAWD.size + inApp->bin_bss;

	FILE* tempFile = fopen(inPath, "wb");
	if(tempFile == NULL)
		return false;

	fwrite(&tempCCDL, sizeof(_app_ccdl), 1, tempFile);
	fwrite(&tempIMPT, sizeof(_app_impt), 1, tempFile);
	fwrite(&tempEXPT, sizeof(_app_expt), 1, tempFile);
	fwrite(&tempRAWD, sizeof(_app_rawd), 1, tempFile);
	// Write Import Table
	_app_impt_entry tempIHeader = { inApp->import_count, { 0, 0 }, 0 };
	fwrite(&tempIHeader, sizeof(_app_impt_entry), 1, tempFile);
	_app_impt_entry tempIEntry = _app_impt_entry_default;
	for(i = 0; i < inApp->import_count; i++) {
		tempIEntry.offset = inApp->import_data[i]->offset;
		fwrite(&tempIEntry, sizeof(_app_impt_entry), 1, tempFile);
		tempIEntry.str_offset += _app_strlen(inApp->import_data[i]->name);
	}

	// Write Import Strings
	for(i = 0; i < inApp->import_count; i++)
		_fprint_string(inApp->import_data[i]->name, tempFile);

	// Write Import Whitespace
	fwrite(tempPadding, 1, tempIPad, tempFile);



	// Write Export Table
	_app_expt_entry tempEHeader = { inApp->export_count, { 0, 0 }, 0 };
	fwrite(&tempEHeader, sizeof(_app_expt_entry), 1, tempFile);
	_app_expt_entry tempEEntry = _app_expt_entry_default;
	for(i = 0; i < inApp->export_count; i++) {
		tempEEntry.offset = inApp->export_data[i]->offset;
		fwrite(&tempEEntry, sizeof(_app_expt_entry), 1, tempFile);
		tempEEntry.str_offset += _app_strlen(inApp->export_data[i]->name);
	}

	// Write Export Strings
	for(i = 0; i < inApp->export_count; i++)
		_fprint_string(inApp->export_data[i]->name, tempFile);

	// Write Export Whitespace
	fwrite(tempPadding, 1, tempEPad, tempFile);



	// Write Binary Data
	fwrite(inApp->bin_data, 1, inApp->bin_size, tempFile);
	fwrite(tempPadding, 1, ((16 - (inApp->bin_size & 15)) & 15), tempFile);

	fclose(tempFile);
	return true;
}

