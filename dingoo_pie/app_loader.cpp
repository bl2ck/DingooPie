#include "app_loader.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <strings.h>
#include "runtime_debug.h"

static const uint32_t APP_PACKED_RECORD_SIZE = 36;
static const uint32_t APP_PACKED_NAME_SIZE = 32;
static const uint32_t APP_PACKED_MAX_TABLES = 8;
static const uint32_t APP_PACKED_SCAN_ALIGNMENT = 0x1000;
static const uint32_t APP_PACKED_MIN_VALID_RATIO_NUM = 8;
static const uint32_t APP_PACKED_MIN_VALID_RATIO_DEN = 10;
static const uint32_t APP_PACKED_MIN_KNOWN_EXTENSIONS = 8;
static const uint32_t APP_PACKAGE_INDEX_MAX_TABLES = 8;
static const uint32_t APP_PACKAGE_INDEX_MAX_COUNT = 65535;
static const uint32_t APP_PACKAGE_INDEX_SAMPLE_COUNT = 256;
static const uint32_t APP_PACKAGE_INDEX_SCAN_ALIGNMENT = 0x1000;
static const uint32_t APP_PACKAGE_INDEX_MIN_VALID_RATIO_NUM = 8;
static const uint32_t APP_PACKAGE_INDEX_MIN_VALID_RATIO_DEN = 10;
static const uint32_t APP_PACKAGE_INDEX_MAX_NAME_SIZE = 256;

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

static bool app_package_resource_exists(app* inApp, uint32_t inOffset, uint32_t inSize)
{
	if (!inApp)
	{
		return false;
	}

	for (uint32_t i = 0; i < inApp->package_resource_count; ++i)
	{
		if (inApp->package_resource_data[i].offset == inOffset &&
			inApp->package_resource_data[i].size == inSize)
		{
			return true;
		}
	}
	return false;
}

static bool app_package_resource_append(app* inApp, const char* inName, uint32_t inOffset, uint32_t inSize)
{
	if (!inApp || !inName || !inName[0] || inSize == 0 ||
		app_package_resource_exists(inApp, inOffset, inSize))
	{
		return false;
	}

	app_package_resource_entry* resized = (app_package_resource_entry*)realloc(
		inApp->package_resource_data,
		sizeof(app_package_resource_entry) * (inApp->package_resource_count + 1));
	if (!resized)
	{
		assert(0);
		return false;
	}
	inApp->package_resource_data = resized;

	size_t nameLen = strlen(inName);
	app_package_resource_entry* entry =
		&inApp->package_resource_data[inApp->package_resource_count];
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
	inApp->package_resource_count++;
	return true;
}

static bool app_trace_resources_enabled(void)
{
	const char* value = getenv("DINGOO_PIE_TRACE_RESOURCES");
	return value && value[0] && strcmp(value, "0") != 0;
}

static bool app_resource_name_char_ok(uint8_t c)
{
	return c >= 0x20 && c != 0x7f;
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

	static const char* knownExtensions[] = {
		".ani",
		".bin",
		".bmp",
		".dat",
		".exe",
		".fnt",
		".fon",
		".fsm",
		".gif",
		".jpeg",
		".jpg",
		".log",
		".map",
		".mid",
		".midi",
		".mp3",
		".pak",
		".pcm",
		".png",
		".res",
		".s3dbsp",
		".s3ddat",
		".s3dpal",
		".s3dsty",
		".sai",
		".sau",
		".sbn",
		".sbp",
		".script",
		".sdf",
		".sdt",
		".sef",
		".soj",
		".spl",
		".spr",
		".sst",
		".stx",
		".txt",
		".war",
		".wav",
	};
	for (size_t i = 0; i < sizeof(knownExtensions) / sizeof(knownExtensions[0]); ++i)
	{
		if (strcmp(ext, knownExtensions[i]) == 0)
		{
			return true;
		}
	}
	return false;
}

typedef struct {
	uint32_t base;
	uint32_t count;
	uint32_t table_end;
	int score;
} packed_resource_table;

static bool app_packed_table_probe(app* inApp, uint32_t base, uint32_t inSize, packed_resource_table* out)
{
	// Short-name packed tables have no magic value. Require several independent
	// signals so arbitrary bytes appended after RAWD are not exposed as files.
	if (!inApp || !inApp->file_data || base + 2 > inSize)
	{
		return false;
	}

	uint32_t count = app_read_u16(inApp->file_data, base);
	uint32_t tableEnd = base + 2 + count * APP_PACKED_RECORD_SIZE;
	if (count == 0 || count > 1024 || tableEnd > inSize)
	{
		return false;
	}

	uint32_t tableSize = 2 + count * APP_PACKED_RECORD_SIZE;
	uint32_t validNames = 0;
	uint32_t knownNames = 0;
	uint32_t validOffsets = 0;
	uint32_t lastOffset = tableSize;
	char nameBuf[APP_PACKED_NAME_SIZE + 1];

	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t rec = base + 2 + i * APP_PACKED_RECORD_SIZE;
		int nameLen = app_resource_name_len(inApp->file_data + rec, APP_PACKED_NAME_SIZE);
		uint32_t relOffset = app_read_u32(inApp->file_data, rec + APP_PACKED_NAME_SIZE);
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

	if (validNames < count * APP_PACKED_MIN_VALID_RATIO_NUM / APP_PACKED_MIN_VALID_RATIO_DEN ||
		validOffsets < count * APP_PACKED_MIN_VALID_RATIO_NUM / APP_PACKED_MIN_VALID_RATIO_DEN ||
		knownNames < APP_PACKED_MIN_KNOWN_EXTENSIONS)
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
	uint32_t best = packageEnd - table->base;
	for (uint32_t i = index + 1; i < table->count; ++i)
	{
		uint32_t rec = table->base + 2 + i * APP_PACKED_RECORD_SIZE;
		uint32_t relOffset = app_read_u32(inApp->file_data, rec + APP_PACKED_NAME_SIZE);
		if (relOffset > currentOffset && relOffset < best)
		{
			best = relOffset;
		}
	}
	return best;
}

static void app_parse_packed_table(app* inApp, const packed_resource_table* table, uint32_t packageEnd)
{
	char nameBuf[APP_PACKED_NAME_SIZE + 1];

	for (uint32_t i = 0; i < table->count; ++i)
	{
		uint32_t rec = table->base + 2 + i * APP_PACKED_RECORD_SIZE;
		int nameLen = app_resource_name_len(inApp->file_data + rec, APP_PACKED_NAME_SIZE);
		uint32_t relOffset = app_read_u32(inApp->file_data, rec + APP_PACKED_NAME_SIZE);
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
	packed_resource_table tables[APP_PACKED_MAX_TABLES];
	uint32_t tableCount = 0;
	uint32_t scan = ALIGN(rawEnd, APP_PACKED_SCAN_ALIGNMENT);

	for (uint32_t base = scan; base + 2 < inSize && tableCount < APP_PACKED_MAX_TABLES; base += APP_PACKED_SCAN_ALIGNMENT)
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

typedef struct {
	uint32_t base;
	uint32_t count;
	uint32_t count_size;
	uint32_t name_size;
	uint32_t record_size;
	uint32_t table_end;
	int score;
} package_resource_table;

typedef struct {
	char* name;
	uint32_t offset;
} package_resource_record;

static bool app_resource_name_has_path_signal(const char* name)
{
	if (!name)
	{
		return false;
	}
	return strchr(name, '\\') != NULL ||
		strchr(name, '/') != NULL ||
		strchr(name, ':') != NULL;
}

static int app_package_index_name_len(const uint8_t* data, uint32_t maxLen)
{
	for (uint32_t i = 0; i < maxLen; ++i)
	{
		if (data[i] == 0)
		{
			for (uint32_t j = i + 1; j < maxLen; ++j)
			{
				if (data[j] != 0)
				{
					return -1;
				}
			}
			return (int)i;
		}
		if (!app_resource_name_char_ok(data[i]))
		{
			return -1;
		}
	}
	return -1;
}

static uint32_t app_package_index_read_count(const uint8_t* data, uint32_t base, uint32_t countSize)
{
	return countSize == 2 ? app_read_u16(data, base) : app_read_u32(data, base);
}

static bool app_package_index_probe(
	app* inApp,
	uint32_t base,
	uint32_t inSize,
	uint32_t countSize,
	uint32_t nameSize,
	package_resource_table* out)
{
	if (!inApp || !inApp->file_data || !out ||
		countSize == 0 || nameSize == 0 ||
		base + countSize + nameSize + 4 > inSize)
	{
		return false;
	}

	uint32_t recordSize = nameSize + 4;
	uint32_t count = app_package_index_read_count(inApp->file_data, base, countSize);
	uint64_t tableSize64 = (uint64_t)countSize + (uint64_t)count * recordSize;
	if (count < 4 || count > APP_PACKAGE_INDEX_MAX_COUNT ||
		tableSize64 > UINT32_MAX || (uint64_t)base + tableSize64 > inSize)
	{
		return false;
	}

	uint32_t tableSize = (uint32_t)tableSize64;
	uint32_t sampleCount = count < APP_PACKAGE_INDEX_SAMPLE_COUNT ?
		count : APP_PACKAGE_INDEX_SAMPLE_COUNT;
	uint32_t validNames = 0;
	uint32_t knownNames = 0;
	uint32_t pathNames = 0;
	uint32_t validOffsets = 0;
	uint32_t orderedOffsets = 0;
	uint32_t lastOffset = tableSize;
	char nameBuf[APP_PACKAGE_INDEX_MAX_NAME_SIZE + 1];

	for (uint32_t i = 0; i < sampleCount; ++i)
	{
		uint32_t rec = base + countSize + i * recordSize;
		int nameLen = app_package_index_name_len(inApp->file_data + rec, nameSize);
		uint32_t relOffset = app_read_u32(inApp->file_data, rec + nameSize);
		if (relOffset >= tableSize && (uint64_t)base + relOffset < inSize)
		{
			validOffsets++;
			if (relOffset >= lastOffset)
			{
				orderedOffsets++;
			}
			lastOffset = relOffset;
		}

		if (nameLen <= 0 || (uint32_t)nameLen > APP_PACKAGE_INDEX_MAX_NAME_SIZE)
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
		if (app_resource_name_has_path_signal(nameBuf))
		{
			pathNames++;
		}
	}

	uint32_t minValid =
		sampleCount * APP_PACKAGE_INDEX_MIN_VALID_RATIO_NUM /
		APP_PACKAGE_INDEX_MIN_VALID_RATIO_DEN;
	if (minValid == 0)
	{
		minValid = 1;
	}
	uint32_t minSignals = sampleCount < 8 ? sampleCount : 8;
	if (validNames < minValid ||
		validOffsets < minValid ||
		orderedOffsets < minValid ||
		(knownNames < minSignals && pathNames < minSignals))
	{
		return false;
	}

	out->base = base;
	out->count = count;
	out->count_size = countSize;
	out->name_size = nameSize;
	out->record_size = recordSize;
	out->table_end = base + tableSize;
	out->score = (int)(validNames * 2 + validOffsets + orderedOffsets +
		knownNames * 4 + pathNames);
	return true;
}

static bool app_package_index_probe_base(app* inApp, uint32_t base, uint32_t inSize, package_resource_table* out)
{
	static const uint32_t countSizes[] = { 2, 4 };
	static const uint32_t nameSizes[] = { 64, 32, 128, 256 };
	bool found = false;
	package_resource_table best;
	memset(&best, 0x00, sizeof(best));

	for (uint32_t i = 0; i < sizeof(countSizes) / sizeof(countSizes[0]); ++i)
	{
		for (uint32_t j = 0; j < sizeof(nameSizes) / sizeof(nameSizes[0]); ++j)
		{
			package_resource_table candidate;
			if (app_package_index_probe(inApp, base, inSize, countSizes[i], nameSizes[j], &candidate) &&
				(!found || candidate.score > best.score))
			{
				best = candidate;
				found = true;
			}
		}
	}

	if (found && out)
	{
		*out = best;
	}
	return found;
}

static void app_package_index_add_candidate(
	package_resource_table* tables,
	uint32_t* tableCount,
	const package_resource_table* candidate)
{
	if (!tables || !tableCount || !candidate)
	{
		return;
	}

	for (uint32_t i = 0; i < *tableCount; ++i)
	{
		if (candidate->base > tables[i].base && candidate->base < tables[i].table_end)
		{
			return;
		}
		if (tables[i].base == candidate->base)
		{
			if (candidate->score > tables[i].score)
			{
				tables[i] = *candidate;
			}
			return;
		}
	}

	if (*tableCount < APP_PACKAGE_INDEX_MAX_TABLES)
	{
		tables[(*tableCount)++] = *candidate;
	}
}

static int app_package_resource_offset_compare(const void* a, const void* b)
{
	const app_package_resource_entry* left = (const app_package_resource_entry*)a;
	const app_package_resource_entry* right = (const app_package_resource_entry*)b;
	if (left->offset < right->offset)
	{
		return -1;
	}
	if (left->offset > right->offset)
	{
		return 1;
	}
	return 0;
}

static int app_package_record_offset_compare(const void* a, const void* b)
{
	const package_resource_record* left = (const package_resource_record*)a;
	const package_resource_record* right = (const package_resource_record*)b;
	if (left->offset < right->offset)
	{
		return -1;
	}
	if (left->offset > right->offset)
	{
		return 1;
	}
	return 0;
}

static void app_parse_package_table(app* inApp, const package_resource_table* table, uint32_t packageEnd)
{
	if (!inApp || !table || table->count == 0 || packageEnd <= table->table_end)
	{
		return;
	}

	package_resource_record* records = (package_resource_record*)malloc(
		sizeof(package_resource_record) * table->count);
	if (!records)
	{
		assert(0);
		return;
	}
	memset(records, 0x00, sizeof(package_resource_record) * table->count);

	uint32_t validCount = 0;
	char nameBuf[APP_PACKAGE_INDEX_MAX_NAME_SIZE + 1];
	for (uint32_t i = 0; i < table->count; ++i)
	{
		uint32_t rec = table->base + table->count_size + i * table->record_size;
		int nameLen = app_package_index_name_len(inApp->file_data + rec, table->name_size);
		uint32_t relOffset = app_read_u32(inApp->file_data, rec + table->name_size);
		if (nameLen <= 0 ||
			(uint32_t)nameLen > APP_PACKAGE_INDEX_MAX_NAME_SIZE ||
			relOffset < table->table_end - table->base ||
			(uint64_t)table->base + relOffset >= packageEnd)
		{
			continue;
		}

		memset(nameBuf, 0x00, sizeof(nameBuf));
		memcpy(nameBuf, inApp->file_data + rec, (size_t)nameLen);
		records[validCount].name = (char*)malloc((size_t)nameLen + 1);
		if (!records[validCount].name)
		{
			assert(0);
			continue;
		}
		memcpy(records[validCount].name, nameBuf, (size_t)nameLen + 1);
		records[validCount].offset = table->base + relOffset;
		validCount++;
	}

	if (validCount > 1)
	{
		qsort(records, validCount, sizeof(records[0]), app_package_record_offset_compare);
	}

	for (uint32_t i = 0; i < validCount; ++i)
	{
		uint32_t nextOffset = packageEnd;
		for (uint32_t j = i + 1; j < validCount; ++j)
		{
			if (records[j].offset > records[i].offset)
			{
				nextOffset = records[j].offset;
				break;
			}
		}
		if (nextOffset > records[i].offset)
		{
			app_package_resource_append(
				inApp,
				records[i].name,
				records[i].offset,
				nextOffset - records[i].offset);
		}
		free(records[i].name);
	}

	free(records);
}

static void app_parse_package_resource_indexes_at(app* inApp, uint32_t rawEnd, uint32_t inSize)
{
	package_resource_table tables[APP_PACKAGE_INDEX_MAX_TABLES];
	uint32_t tableCount = 0;
	memset(tables, 0x00, sizeof(tables));

	package_resource_table table;
	if (app_package_index_probe_base(inApp, rawEnd, inSize, &table))
	{
		app_package_index_add_candidate(tables, &tableCount, &table);
	}

	uint32_t scan = ALIGN(rawEnd, APP_PACKAGE_INDEX_SCAN_ALIGNMENT);
	for (uint32_t base = scan;
		base + 6 < inSize && tableCount < APP_PACKAGE_INDEX_MAX_TABLES;
		base += APP_PACKAGE_INDEX_SCAN_ALIGNMENT)
	{
		if (base == rawEnd)
		{
			continue;
		}
		if (app_package_index_probe_base(inApp, base, inSize, &table))
		{
			app_package_index_add_candidate(tables, &tableCount, &table);
		}
	}

	for (uint32_t i = 0; i < tableCount; ++i)
	{
		uint32_t packageEnd = (i + 1 < tableCount) ? tables[i + 1].base : inSize;
		app_parse_package_table(inApp, &tables[i], packageEnd);
	}

	if (inApp->package_resource_count > 1)
	{
		qsort(inApp->package_resource_data,
			inApp->package_resource_count,
			sizeof(inApp->package_resource_data[0]),
			app_package_resource_offset_compare);
	}
	if (tableCount > 0)
	{
		printf("app-loader: package_resource_tables=%u package_resources=%u\n",
			tableCount, inApp->package_resource_count);
	}
}

void app_parse_package_resource_indexes(app* inApp)
{
	if (!inApp || !inApp->file_data || inApp->package_resource_index_scanned)
	{
		return;
	}

	inApp->package_resource_index_scanned = true;
	if (inApp->package_resource_scan_start >= inApp->file_size)
	{
		return;
	}
	app_parse_package_resource_indexes_at(
		inApp,
		inApp->package_resource_scan_start,
		inApp->file_size);
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

	// Read fixed import/export headers first, then their packed string tables.
	fseek(tempFile, tempIMPT.offset, SEEK_SET);
	_app_impt_entry tempIHeader = { 0, { 0, 0 }, 0 };
	fread(&tempIHeader, sizeof(_app_impt_entry), 1, tempFile);
	tempApp->import_count = tempIHeader.str_offset;
	_app_impt_entry* tempIEntry = (_app_impt_entry*)malloc(sizeof(_app_impt_entry) * tempApp->import_count);
	if (!tempIEntry)
	{
		assert(0);
	}
	for (i = 0; i < tempApp->import_count; i++)
	{
		fread(&tempIEntry[i], sizeof(_app_impt_entry), 1, tempFile);
	}

	tempApp->import_data = (app_import_entry**)malloc(sizeof(app_import_entry*) * tempApp->import_count);
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
	fseek(tempFile, tempEXPT.offset, SEEK_SET);
	_app_expt_entry tempEHeader = { 0, { 0, 0 }, 0 };
	fread(&tempEHeader, sizeof(_app_expt_entry), 1, tempFile);
	tempApp->export_count = tempEHeader.str_offset;
	_app_expt_entry* tempEEntry = (_app_expt_entry*)malloc(sizeof(_app_expt_entry) * tempApp->export_count);
	if (!tempEEntry)
	{
		assert(0);
	}
	for (i = 0; i < tempApp->export_count; i++)
	{
		fread(&tempEEntry[i], sizeof(_app_expt_entry), 1, tempFile);
	}
	tempApp->export_data = (app_export_entry**)malloc(sizeof(app_export_entry*) * tempApp->export_count);
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

	// RAWD contains the executable image; allocate the full guest program size
	// so BSS is zero-filled immediately after the stored bytes.
	fseek(tempFile, tempRAWD.offset, SEEK_SET);
	tempApp->bin_size = tempRAWD.size;

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
	tempApp->package_resource_scan_start = tempRAWD.offset + tempRAWD.size;

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

void app_delete(app* inApp)
{
	if (inApp == NULL)
	{
		return;
	}

	uintptr_t i;

	if (inApp->import_data != NULL)
	{
		for (i = 0; i < inApp->import_count; i++)
		{
			free(inApp->import_data[i]);
		}
		free(inApp->import_data);
	}

	if (inApp->export_data != NULL)
	{
		for (i = 0; i < inApp->export_count; i++)
		{
			free(inApp->export_data[i]);
		}
		free(inApp->export_data);
	}

	if (inApp->resource_data != NULL)
	{
		for (i = 0; i < inApp->resource_count; i++)
		{
			free(inApp->resource_data[i].name);
			free(inApp->resource_data[i].decoded_data);
		}
		free(inApp->resource_data);
	}

	if (inApp->package_resource_data != NULL)
	{
		for (i = 0; i < inApp->package_resource_count; i++)
		{
			free(inApp->package_resource_data[i].name);
		}
		free(inApp->package_resource_data);
	}

	if (inApp->bin_data != NULL)
	{
		free(inApp->bin_data);
	}

	if (inApp->file_data != NULL)
	{
		free(inApp->file_data);
	}

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
