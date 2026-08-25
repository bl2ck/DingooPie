#include "shared/services/guest_package.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <strings.h>
#include <mutex>

static uint32_t alignUp(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static const uint32_t kPackedRecordSize = 36;
static const uint32_t kPackedNameSize = 32;
static const uint32_t kMaxPackedTables = 8;
static const uint32_t kPackedScanAlignment = 0x1000;
static const uint32_t kPackedMinValidRatioNumerator = 8;
static const uint32_t kPackedMinValidRatioDenominator = 10;
static const uint32_t kPackedMinKnownExtensions = 8;
static const uint32_t kLegacyRecordSize = 0x408;
static const uint32_t kLegacyNameSize = 0x400;
static const uint32_t kMaxLegacyRecords = 256;
static std::mutex s_resourceDecodeMutex;

// Dingoo Technology APP/CC package containers are fixed-size little-endian chunks
// followed by executable code and optional resource tables. Several header words
// are still known only by observation, so they stay reserved instead of being
// guessed into public structure names.
struct __attribute__((__packed__)) PackageCcdlHeader
{
	char ident[4];
	uint8_t unknown[20];
	uint8_t padding[8];
};

struct __attribute__((__packed__)) PackageImportHeader
{
	char ident[4];
	uint32_t unknown;
	uint32_t offset;
	uint32_t size;
	uint8_t padding[16];
};

struct __attribute__((__packed__)) PackageExportHeader
{
	char ident[4];
	uint32_t unknown;
	uint32_t offset;
	uint32_t size;
	uint8_t padding[16];
};

struct __attribute__((__packed__)) PackageRawHeader
{
	char ident[4];
	uint32_t unknown0;
	uint32_t offset;
	uint32_t size;
	uint32_t unknown1;
	uint32_t entry;
	uint32_t origin;
	uint32_t programSize;
};

struct __attribute__((__packed__)) PackageImportRecord
{
	uint32_t stringOffset;
	uint32_t unknown[2];
	uint32_t offset;
};

struct __attribute__((__packed__)) PackageExportRecord
{
	uint32_t stringOffset;
	uint32_t unknown[2];
	uint32_t offset;
};

static uint16_t readPackageU16(const uint8_t* data, uint32_t offset)
{
	return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static uint32_t readPackageU32(const uint8_t* data, uint32_t offset)
{
	return (uint32_t)data[offset] |
		((uint32_t)data[offset + 1] << 8) |
		((uint32_t)data[offset + 2] << 16) |
		((uint32_t)data[offset + 3] << 24);
}

static bool appendPackageResource(GuestPackage* inApp, const char* inName, uint32_t inOffset, uint32_t inSize, uint8_t xorKey)
{
	if (!inApp || !inName || !inName[0] || inSize == 0)
	{
		return false;
	}

	GuestResourceEntry* resized = (GuestResourceEntry*)realloc(
		inApp->resource_data,
		sizeof(GuestResourceEntry) * (inApp->resource_count + 1));
	if (!resized)
	{
		assert(0);
		return false;
	}
	inApp->resource_data = resized;

	size_t nameLen = strlen(inName);
	GuestResourceEntry* entry = &inApp->resource_data[inApp->resource_count];
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
	entry->xorKey = xorKey;
	inApp->resource_count++;
	return true;
}

static bool resourceTraceEnabled(void)
{
	const char* value = getenv("DINGOO_PIE_TRACE_RESOURCES");
	return value && value[0] && strcmp(value, "0") != 0;
}

static bool isResourceNameCharacterValid(uint8_t c)
{
	return c >= 0x20 && c != 0x7f;
}

static int resourceNameLength(const uint8_t* data, uint32_t maxLen)
{
	for (uint32_t i = 0; i < maxLen; ++i)
	{
		if (data[i] == 0)
		{
			return (int)i;
		}
		if (!isResourceNameCharacterValid(data[i]))
		{
			return -1;
		}
	}
	return -1;
}

static bool hasKnownResourceExtension(const char* name)
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
		".mkf",
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
		".wad",
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

static uint32_t probeLegacyResourceTable(GuestPackage* inApp,
	uint32_t base, uint32_t inSize, bool append)
{
	if (!inApp || base > inSize - 4)
	{
		return 0;
	}
	uint32_t count = readPackageU32(inApp->file_data, base);
	if (count == 0 || count > kMaxLegacyRecords)
	{
		return 0;
	}
	uint64_t tableEnd64 = (uint64_t)base + 4u +
		(uint64_t)count * kLegacyRecordSize;
	if (tableEnd64 > inSize)
	{
		return 0;
	}
	uint32_t tableEnd = (uint32_t)tableEnd64;
	uint32_t dataEnd = tableEnd;
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t record = base + 4u + i * kLegacyRecordSize;
		uint32_t size = readPackageU32(inApp->file_data, record);
		uint32_t relativeOffset = readPackageU32(inApp->file_data, record + 4u);
		int nameLength = resourceNameLength(
			inApp->file_data + record + 8u, kLegacyNameSize);
		if (size == 0 || nameLength <= 0 || nameLength >= 256)
		{
			return 0;
		}
		char name[256] = { 0 };
		memcpy(name, inApp->file_data + record + 8u, (size_t)nameLength);
		if (!hasKnownResourceExtension(name) ||
			relativeOffset > inSize - tableEnd ||
			size > inSize - tableEnd - relativeOffset)
		{
			return 0;
		}
		uint32_t dataOffset = tableEnd + relativeOffset;
		if (append && !appendPackageResource(inApp, name, dataOffset, size, 0))
		{
			return 0;
		}
		dataEnd = dataOffset + size > dataEnd ? dataOffset + size : dataEnd;
	}
	return dataEnd;
}

static void parseLegacyResources(GuestPackage* inApp,
	uint32_t base, uint32_t inSize)
{
	uint32_t cursor = base;
	uint32_t tables = 0;
	while (cursor <= inSize - 4u && tables < 256u)
	{
		uint32_t dataEnd = probeLegacyResourceTable(inApp, cursor, inSize, false);
		if (!dataEnd)
		{
			++cursor;
			continue;
		}
		if (!probeLegacyResourceTable(inApp, cursor, inSize, true))
		{
			break;
		}
		++tables;
		cursor = dataEnd;
	}
	if (tables)
	{
		printf("guest-package: legacy resources=%u tables=%u\n",
			inApp->resource_count, tables);
	}
}

struct PackedResourceTable
{
	uint32_t base;
	uint32_t count;
	uint32_t tableEnd;
	int score;
};

static bool probePackedResourceTable(GuestPackage* inApp, uint32_t base, uint32_t inSize, PackedResourceTable* out)
{
	// Short-name packed tables have no magic value. Require several independent
	// signals so arbitrary bytes appended after RAWD are not exposed as files.
	if (!inApp || !inApp->file_data || inSize < 2 || base > inSize - 2)
	{
		return false;
	}

	uint32_t count = readPackageU16(inApp->file_data, base);
	uint32_t tableSize = 2 + count * kPackedRecordSize;
	uint64_t tableEnd = (uint64_t)base + tableSize;
	if (count == 0 || count > 1024 || tableEnd > inSize)
	{
		return false;
	}

	uint32_t validNames = 0;
	uint32_t knownNames = 0;
	uint32_t validOffsets = 0;
	uint32_t lastOffset = tableSize;
	char nameBuf[kPackedNameSize + 1];

	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t rec = base + 2 + i * kPackedRecordSize;
		int nameLen = resourceNameLength(inApp->file_data + rec, kPackedNameSize);
		uint32_t relOffset = readPackageU32(inApp->file_data, rec + kPackedNameSize);
		if (relOffset >= tableSize && (uint64_t)base + relOffset < inSize)
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
		if (hasKnownResourceExtension(nameBuf))
		{
			knownNames++;
		}
	}

	if (validNames < count * kPackedMinValidRatioNumerator / kPackedMinValidRatioDenominator ||
		validOffsets < count * kPackedMinValidRatioNumerator / kPackedMinValidRatioDenominator ||
		knownNames < kPackedMinKnownExtensions)
	{
		return false;
	}

	out->base = base;
	out->count = count;
	out->tableEnd = (uint32_t)tableEnd;
	out->score = (int)knownNames;
	return true;
}

static uint32_t nextPackedResourceOffset(GuestPackage* inApp, const PackedResourceTable* table, uint32_t index, uint32_t currentOffset, uint32_t packageEnd)
{
	uint32_t best = packageEnd - table->base;
	for (uint32_t i = index + 1; i < table->count; ++i)
	{
		uint32_t rec = table->base + 2 + i * kPackedRecordSize;
		uint32_t relOffset = readPackageU32(inApp->file_data, rec + kPackedNameSize);
		if (relOffset > currentOffset && relOffset < best)
		{
			best = relOffset;
		}
	}
	return best;
}

static void parsePackedResourceTable(GuestPackage* inApp, const PackedResourceTable* table, uint32_t packageEnd)
{
	char nameBuf[kPackedNameSize + 1];

	for (uint32_t i = 0; i < table->count; ++i)
	{
		uint32_t rec = table->base + 2 + i * kPackedRecordSize;
		int nameLen = resourceNameLength(inApp->file_data + rec, kPackedNameSize);
		uint32_t relOffset = readPackageU32(inApp->file_data, rec + kPackedNameSize);
		if (nameLen <= 0 ||
			relOffset < (table->tableEnd - table->base) ||
			(uint64_t)table->base + relOffset >= packageEnd)
		{
			continue;
		}

		uint32_t nextOffset = nextPackedResourceOffset(inApp, table, i, relOffset, packageEnd);
		if (nextOffset <= relOffset || (uint64_t)table->base + nextOffset > packageEnd)
		{
			continue;
		}

		memset(nameBuf, 0x00, sizeof(nameBuf));
		memcpy(nameBuf, inApp->file_data + rec, (size_t)nameLen);
		appendPackageResource(inApp, nameBuf, table->base + relOffset, nextOffset - relOffset, 0);
	}
}

static void parsePackedResources(GuestPackage* inApp, uint32_t rawEnd, uint32_t inSize)
{
	PackedResourceTable tables[kMaxPackedTables];
	uint32_t tableCount = 0;
	uint32_t scan = alignUp(rawEnd, kPackedScanAlignment);

	uint32_t base = scan;
	while (inSize > 2 && base < inSize - 2 && tableCount < kMaxPackedTables)
	{
		PackedResourceTable table;
		if (probePackedResourceTable(inApp, base, inSize, &table))
		{
			tables[tableCount++] = table;
		}
		if (base > UINT32_MAX - kPackedScanAlignment)
		{
			break;
		}
		base += kPackedScanAlignment;
	}

	for (uint32_t i = 0; i < tableCount; ++i)
	{
		uint32_t packageEnd = (i + 1 < tableCount) ? tables[i + 1].base : inSize;
		parsePackedResourceTable(inApp, &tables[i], packageEnd);
	}

	if (tableCount > 0)
	{
		printf("guest-package: PackedResourceTables=%u resources=%u\n", tableCount, inApp->resource_count);
	}
}

static int resourceNamesEqual(const char* a, const char* b)
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

static const char* resourceBaseName(const char* name)
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

GuestPackage* guestPackageCreate(FILE* tempFile, uint32_t inSize)
{
	if (!tempFile || !guestPackageProbeFileHeader(tempFile, inSize))
	{
		return NULL;
	}

	uint32_t i = 0;
	GuestPackage* tempApp = (GuestPackage*)malloc(sizeof(GuestPackage));
	if (!tempApp)
	{
		return NULL;
	}

	memset(tempApp, 0x00, sizeof(GuestPackage));
	PackageImportRecord* tempIEntry = NULL;
	PackageExportRecord* tempEEntry = NULL;
	auto failLoad = [&]() -> GuestPackage*
	{
		free(tempIEntry);
		free(tempEEntry);
		guestPackageDestroy(tempApp);
		return NULL;
	};

	tempApp->file_size = inSize;
	tempApp->file_data = (uint8_t*)malloc(inSize);
	if (!tempApp->file_data)
	{
		guestPackageDestroy(tempApp);
		return NULL;
	}
	if (fseek(tempFile, 0, SEEK_SET) != 0 ||
		fread(tempApp->file_data, inSize, 1, tempFile) != 1 ||
		fseek(tempFile, 0, SEEK_SET) != 0)
	{
		guestPackageDestroy(tempApp);
		return NULL;
	}

	PackageCcdlHeader tempCCDL = {};
	PackageImportHeader tempIMPT = {};
	PackageExportHeader tempEXPT = {};
	PackageRawHeader tempRAWD = {};
	PackageImportHeader tempERPT = {};

	if (fread(&tempCCDL, sizeof(PackageCcdlHeader), 1, tempFile) != 1 ||
		fread(&tempIMPT, sizeof(PackageImportHeader), 1, tempFile) != 1 ||
		fread(&tempEXPT, sizeof(PackageExportHeader), 1, tempFile) != 1 ||
		fread(&tempRAWD, sizeof(PackageRawHeader), 1, tempFile) != 1)
	{
		return failLoad();
	}
	(void)fread(&tempERPT, sizeof(PackageImportHeader), 1, tempFile);

	// Read fixed import/export headers first, then their packed string tables.
	if (tempIMPT.offset > inSize - sizeof(PackageImportRecord) ||
		fseek(tempFile, tempIMPT.offset, SEEK_SET) != 0)
	{
		return failLoad();
	}
	PackageImportRecord tempIHeader = { 0, { 0, 0 }, 0 };
	if (fread(&tempIHeader, sizeof(PackageImportRecord), 1, tempFile) != 1)
	{
		return failLoad();
	}
	tempApp->import_count = tempIHeader.stringOffset;
	uint64_t importEntriesEnd = (uint64_t)tempIMPT.offset + sizeof(tempIHeader) +
		(uint64_t)tempApp->import_count * sizeof(PackageImportRecord);
	if (importEntriesEnd > inSize)
	{
		return failLoad();
	}
	if (tempApp->import_count > 0)
	{
		tempIEntry = (PackageImportRecord*)malloc(sizeof(PackageImportRecord) * tempApp->import_count);
		tempApp->import_data = (GuestImportEntry**)calloc(tempApp->import_count,
			sizeof(GuestImportEntry*));
		if (!tempIEntry || !tempApp->import_data)
		{
			return failLoad();
		}
		for (i = 0; i < tempApp->import_count; i++)
		{
			if (fread(&tempIEntry[i], sizeof(PackageImportRecord), 1, tempFile) != 1)
			{
				return failLoad();
			}
		}
	}

	for (i = 0; i < tempApp->import_count; i++)
	{
		GuestImportEntry* entry = (GuestImportEntry*)calloc(1, sizeof(GuestImportEntry));
		if (!entry)
		{
			return failLoad();
		}
		tempApp->import_data[i] = entry;
		entry->offset = tempIEntry[i].offset;

		uint64_t importSectionEnd = (uint64_t)tempIMPT.offset + tempIMPT.size;
		uint64_t nameOffset = importEntriesEnd + tempIEntry[i].stringOffset;
		if (importSectionEnd > inSize || nameOffset >= importSectionEnd)
		{
			return failLoad();
		}
		const uint8_t* nameStart = tempApp->file_data + nameOffset;
		const void* nameEnd = memchr(nameStart, '\0',
			(size_t)(importSectionEnd - nameOffset));
		if (!nameEnd)
		{
			return failLoad();
		}
		size_t nameLength = (const uint8_t*)nameEnd - nameStart;
		entry->name = (char*)malloc(nameLength + 1);
		if (!entry->name)
		{
			return failLoad();
		}
		memcpy(entry->name, nameStart, nameLength + 1);
	}
	free(tempIEntry);
	tempIEntry = NULL;

	if (tempEXPT.offset > inSize - sizeof(PackageExportRecord) ||
		fseek(tempFile, tempEXPT.offset, SEEK_SET) != 0)
	{
		return failLoad();
	}
	PackageExportRecord tempEHeader = { 0, { 0, 0 }, 0 };
	if (fread(&tempEHeader, sizeof(PackageExportRecord), 1, tempFile) != 1)
	{
		return failLoad();
	}
	tempApp->export_count = tempEHeader.stringOffset;
	uint64_t exportEntriesEnd = (uint64_t)tempEXPT.offset + sizeof(tempEHeader) +
		(uint64_t)tempApp->export_count * sizeof(PackageExportRecord);
	if (exportEntriesEnd > inSize)
	{
		return failLoad();
	}
	if (tempApp->export_count > 0)
	{
		tempEEntry = (PackageExportRecord*)malloc(sizeof(PackageExportRecord) * tempApp->export_count);
		tempApp->export_data = (GuestExportEntry**)calloc(tempApp->export_count,
			sizeof(GuestExportEntry*));
		if (!tempEEntry || !tempApp->export_data)
		{
			return failLoad();
		}
		for (i = 0; i < tempApp->export_count; i++)
		{
			if (fread(&tempEEntry[i], sizeof(PackageExportRecord), 1, tempFile) != 1)
			{
				return failLoad();
			}
		}
	}
	for (i = 0; i < tempApp->export_count; i++)
	{
		GuestExportEntry* entry = (GuestExportEntry*)calloc(1, sizeof(GuestExportEntry));
		if (!entry)
		{
			return failLoad();
		}
		tempApp->export_data[i] = entry;
		entry->offset = tempEEntry[i].offset;

		uint64_t exportSectionEnd = (uint64_t)tempEXPT.offset + tempEXPT.size;
		uint64_t nameOffset = exportEntriesEnd + tempEEntry[i].stringOffset;
		if (exportSectionEnd > inSize || nameOffset >= exportSectionEnd)
		{
			return failLoad();
		}
		const uint8_t* nameStart = tempApp->file_data + nameOffset;
		const void* nameEnd = memchr(nameStart, '\0',
			(size_t)(exportSectionEnd - nameOffset));
		if (!nameEnd)
		{
			return failLoad();
		}
		size_t nameLength = (const uint8_t*)nameEnd - nameStart;
		entry->name = (char*)malloc(nameLength + 1);
		if (!entry->name)
		{
			return failLoad();
		}
		memcpy(entry->name, nameStart, nameLength + 1);
	}
	free(tempEEntry);
	tempEEntry = NULL;

	// RAWD contains the executable image; allocate the full guest program size
	// so BSS is zero-filled immediately after the stored bytes.
	if (tempRAWD.offset > inSize || tempRAWD.size > inSize - tempRAWD.offset ||
		tempRAWD.programSize > UINT32_MAX - 4095 ||
		fseek(tempFile, tempRAWD.offset, SEEK_SET) != 0)
	{
		return failLoad();
	}
	tempApp->bin_size = tempRAWD.size;

	uint32_t memory_align = alignUp(tempRAWD.programSize, 4096);
	tempApp->bin_data = malloc(memory_align);
	if (!tempApp->bin_data)
	{
		return failLoad();
	}
	memset(tempApp->bin_data, 0x00, memory_align);
	if (fread(tempApp->bin_data, tempApp->bin_size, 1, tempFile) != 1)
	{
		return failLoad();
	}

	tempApp->bin_size = memory_align;

	tempApp->bin_entry = tempRAWD.entry;
	tempApp->bin_bss = tempRAWD.programSize - tempRAWD.size;
	tempApp->origin = tempRAWD.origin;
	tempApp->prog_size = tempRAWD.programSize;

	if (memcmp(tempERPT.ident, "ERPT", 4) == 0 && inSize >= 4 && tempERPT.offset <= inSize - 4)
	{
		uint32_t count = readPackageU32(tempApp->file_data, tempERPT.offset);
		const uint32_t recordSize = 0x1fc;
		const uint32_t nameSize = 0x1f4;
		if (count > 0 && count < 4096)
		{
			uint64_t tableEnd = (uint64_t)tempERPT.offset + 4u + (uint64_t)count * recordSize;
			if (tableEnd <= inSize)
			{
				for (i = 0; i < count; ++i)
				{
					uint32_t rec = tempERPT.offset + 4 + i * recordSize;
					uint32_t size = readPackageU32(tempApp->file_data, rec + nameSize);
					uint32_t relOffset = readPackageU32(tempApp->file_data, rec + nameSize + 4);
					if (relOffset > UINT32_MAX - tempERPT.offset)
					{
						continue;
					}
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
					appendPackageResource(tempApp, nameBuf, dataOffset, size, 0x40);
					if (resourceTraceEnabled())
					{
						printf("guest-package: trace-resource erpt name=%s offset=0x%08x size=0x%08x xor=0x40\n",
							nameBuf, dataOffset, size);
					}
				}

				printf("guest-package: erpt resources=%u\n", tempApp->resource_count);
			}
		}
	}

	if (tempApp->resource_count == 0)
	{
		uint32_t legacyBase = inSize >= 0x20 ?
			readPackageU32(tempApp->file_data, 0x1c) : 0;
		if (legacyBase < inSize)
		{
			parseLegacyResources(tempApp, legacyBase, inSize);
		}
		if (tempApp->resource_count == 0)
		{
			parsePackedResources(tempApp,
				tempRAWD.offset + tempRAWD.size, inSize);
		}
	}

	return tempApp;
}

bool guestPackageProbeFileHeader(FILE* file, uint32_t fileSize)
{
	if (!file || fileSize < sizeof(PackageCcdlHeader) + sizeof(PackageImportHeader) + sizeof(PackageExportHeader) + sizeof(PackageRawHeader))
	{
		return false;
	}

	PackageCcdlHeader ccdl;
	PackageImportHeader impt;
	PackageExportHeader expt;
	PackageRawHeader rawd;
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
		rawd.programSize >= rawd.size;
}

void guestPackageDestroy(GuestPackage* inApp)
{
	std::lock_guard<std::mutex> lock(s_resourceDecodeMutex);
	if (inApp == NULL)
	{
		return;
	}

	uintptr_t i;

	if (inApp->import_data != NULL)
	{
		for (i = 0; i < inApp->import_count; i++)
		{
			if (inApp->import_data[i])
			{
				free(inApp->import_data[i]->name);
				free(inApp->import_data[i]);
			}
		}
		free(inApp->import_data);
	}

	if (inApp->export_data != NULL)
	{
		for (i = 0; i < inApp->export_count; i++)
		{
			if (inApp->export_data[i])
			{
				free(inApp->export_data[i]->name);
				free(inApp->export_data[i]);
			}
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

GuestResourceEntry* guestPackageFindResource(GuestPackage* inApp, const char* inName)
{
	if (!inApp || !inName)
	{
		return NULL;
	}

	for (uint32_t i = 0; i < inApp->resource_count; ++i)
	{
		if (resourceNamesEqual(inApp->resource_data[i].name, inName))
		{
			return &inApp->resource_data[i];
		}
	}

	const char* requestBase = resourceBaseName(inName);
	if (requestBase && requestBase[0] && requestBase != inName)
	{
		for (uint32_t i = 0; i < inApp->resource_count; ++i)
		{
			const char* resourceBase = resourceBaseName(inApp->resource_data[i].name);
			if (resourceNamesEqual(resourceBase, requestBase))
			{
				return &inApp->resource_data[i];
			}
		}
	}

	return NULL;
}

void guestPackageTraceResourceCandidates(GuestPackage* inApp, const char* inName)
{
	if (!resourceTraceEnabled() || !inApp || !inName)
	{
		return;
	}

	const char* requestBase = resourceBaseName(inName);
	for (uint32_t i = 0; i < inApp->resource_count; ++i)
	{
		const char* resourceBase = resourceBaseName(inApp->resource_data[i].name);
		if (resourceNamesEqual(inApp->resource_data[i].name, inName) ||
			(requestBase && resourceBase && resourceNamesEqual(resourceBase, requestBase)))
		{
			printf("guest-package: trace-resource candidate request=%s name=%s offset=0x%08x size=0x%08x xor=0x%02x\n",
				inName,
				inApp->resource_data[i].name,
				inApp->resource_data[i].offset,
				inApp->resource_data[i].size,
				inApp->resource_data[i].xorKey);
		}
	}
}

const uint8_t* guestPackageResourceData(GuestPackage* inApp, GuestResourceEntry* inEntry)
{
	std::lock_guard<std::mutex> lock(s_resourceDecodeMutex);
	if (!inApp || !inEntry || inEntry->offset > inApp->file_size || inEntry->size > inApp->file_size - inEntry->offset)
	{
		return NULL;
	}

	if (!inEntry->xorKey)
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
			inEntry->decoded_data[i] = src[i] ^ inEntry->xorKey;
		}
	}

	return inEntry->decoded_data;
}
