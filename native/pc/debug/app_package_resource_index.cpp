#include "app_package_resource_index.h"

#include <algorithm>
#include <ctype.h>
#include <string.h>

static const uint32_t kAppPackageIndexMaxTables = 8;
static const uint32_t kAppPackageIndexMaxCount = 65535;
static const uint32_t kAppPackageIndexSampleCount = 256;
static const uint32_t kAppPackageIndexScanAlignment = 0x1000;
static const uint32_t kAppPackageIndexMaxNameSize = 256;

struct AppPackageResourceTable
{
    uint32_t base;
    uint32_t count;
    uint32_t countSize;
    uint32_t nameSize;
    uint32_t recordSize;
    uint32_t tableEnd;
    int score;
};

struct AppPackageResourceRecord
{
    std::string name;
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

static uint32_t alignPackageOffset(uint32_t value)
{
    return (value + kAppPackageIndexScanAlignment - 1u) &
        ~(kAppPackageIndexScanAlignment - 1u);
}

static bool appPackageResourceNameCharacterValid(uint8_t value)
{
    return value >= 0x20 && value != 0x7f;
}

static int appPackageResourceNameLength(const uint8_t* data, uint32_t maxLength)
{
    for (uint32_t index = 0; index < maxLength; ++index)
    {
        if (data[index] == 0)
        {
            for (uint32_t tail = index + 1; tail < maxLength; ++tail)
            {
                if (data[tail] != 0)
                {
                    return -1;
                }
            }
            return (int)index;
        }
        if (!appPackageResourceNameCharacterValid(data[index]))
        {
            return -1;
        }
    }
    return -1;
}

static bool appPackageResourceNameHasPath(const char* name)
{
    return name && (strchr(name, '\\') || strchr(name, '/') || strchr(name, ':'));
}

static bool appPackageResourceNameHasKnownExtension(const char* name)
{
    const char* dot = name ? strrchr(name, '.') : NULL;
    if (!dot)
    {
        return false;
    }
    char extension[16] = {};
    size_t length = strlen(dot);
    if (length >= sizeof(extension))
    {
        return false;
    }
    for (size_t index = 0; index < length; ++index)
    {
        extension[index] = (char)tolower((unsigned char)dot[index]);
    }
    static const char* knownExtensions[] = {
        ".ani", ".bin", ".bmp", ".dat", ".exe", ".fnt", ".fon", ".fsm",
        ".gif", ".ini", ".jpeg", ".jpg", ".log", ".map", ".mid", ".midi",
        ".mp3", ".pak", ".pcm", ".png", ".res", ".s3dbsp", ".s3ddat",
        ".s3dpal", ".s3dsty", ".sai", ".sau", ".sbn", ".sbp", ".script",
        ".sdf", ".sdt", ".sef", ".soj", ".spl", ".spr", ".sst", ".stx",
        ".txt", ".war", ".wav"
    };
    for (size_t index = 0;
        index < sizeof(knownExtensions) / sizeof(knownExtensions[0]); ++index)
    {
        if (strcmp(extension, knownExtensions[index]) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool probeAppPackageResourceTable(const GuestPackage* package,
    uint32_t base, uint32_t countSize, uint32_t nameSize,
    AppPackageResourceTable* output)
{
    if (!package || !package->file_data || !output ||
        base > package->file_size || countSize > package->file_size - base ||
        nameSize > package->file_size - base - countSize - 4u)
    {
        return false;
    }
    uint32_t recordSize = nameSize + 4u;
    uint32_t count = countSize == 2 ?
        readPackageU16(package->file_data, base) :
        readPackageU32(package->file_data, base);
    uint64_t tableSize64 = (uint64_t)countSize + (uint64_t)count * recordSize;
    if (count < 4 || count > kAppPackageIndexMaxCount ||
        tableSize64 > UINT32_MAX || (uint64_t)base + tableSize64 > package->file_size)
    {
        return false;
    }

    uint32_t tableSize = (uint32_t)tableSize64;
    uint32_t sampleCount = count < kAppPackageIndexSampleCount ?
        count : kAppPackageIndexSampleCount;
    uint32_t validNames = 0;
    uint32_t knownNames = 0;
    uint32_t pathNames = 0;
    uint32_t validOffsets = 0;
    uint32_t orderedOffsets = 0;
    uint32_t lastOffset = tableSize;
    char name[kAppPackageIndexMaxNameSize + 1] = {};
    for (uint32_t index = 0; index < sampleCount; ++index)
    {
        uint32_t record = base + countSize + index * recordSize;
        int nameLength = appPackageResourceNameLength(
            package->file_data + record, nameSize);
        uint32_t relativeOffset = readPackageU32(
            package->file_data, record + nameSize);
        if (relativeOffset >= tableSize &&
            (uint64_t)base + relativeOffset < package->file_size)
        {
            validOffsets++;
            if (relativeOffset >= lastOffset)
            {
                orderedOffsets++;
            }
            lastOffset = relativeOffset;
        }
        if (nameLength <= 0 || (uint32_t)nameLength > kAppPackageIndexMaxNameSize)
        {
            continue;
        }
        memset(name, 0, sizeof(name));
        memcpy(name, package->file_data + record, (size_t)nameLength);
        validNames++;
        knownNames += appPackageResourceNameHasKnownExtension(name) ? 1u : 0u;
        pathNames += appPackageResourceNameHasPath(name) ? 1u : 0u;
    }

    uint32_t minimumValid = sampleCount * 8u / 10u;
    if (minimumValid == 0)
    {
        minimumValid = 1;
    }
    uint32_t minimumSignals = sampleCount < 8 ? sampleCount : 8;
    if (validNames < minimumValid || validOffsets < minimumValid ||
        orderedOffsets < minimumValid ||
        (knownNames < minimumSignals && pathNames < minimumSignals))
    {
        return false;
    }

    output->base = base;
    output->count = count;
    output->countSize = countSize;
    output->nameSize = nameSize;
    output->recordSize = recordSize;
    output->tableEnd = base + tableSize;
    output->score = (int)(validNames * 2u + validOffsets + orderedOffsets +
        knownNames * 4u + pathNames);
    return true;
}

static bool probeAppPackageResourceTableAt(const GuestPackage* package,
    uint32_t base, AppPackageResourceTable* output)
{
    static const uint32_t countSizes[] = { 2, 4 };
    static const uint32_t nameSizes[] = { 64, 32, 128, 256 };
    bool found = false;
    AppPackageResourceTable best = {};
    for (size_t countIndex = 0;
        countIndex < sizeof(countSizes) / sizeof(countSizes[0]); ++countIndex)
    {
        for (size_t nameIndex = 0;
            nameIndex < sizeof(nameSizes) / sizeof(nameSizes[0]); ++nameIndex)
        {
            AppPackageResourceTable candidate = {};
            if (probeAppPackageResourceTable(package, base,
                countSizes[countIndex], nameSizes[nameIndex], &candidate) &&
                (!found || candidate.score > best.score))
            {
                best = candidate;
                found = true;
            }
        }
    }
    if (found && output)
    {
        *output = best;
    }
    return found;
}

static void addAppPackageResourceTable(
    std::vector<AppPackageResourceTable>* tables,
    const AppPackageResourceTable& candidate)
{
    if (!tables)
    {
        return;
    }
    for (size_t index = 0; index < tables->size(); ++index)
    {
        if (candidate.base > (*tables)[index].base &&
            candidate.base < (*tables)[index].tableEnd)
        {
            return;
        }
        if ((*tables)[index].base == candidate.base)
        {
            if (candidate.score > (*tables)[index].score)
            {
                (*tables)[index] = candidate;
            }
            return;
        }
    }
    if (tables->size() < kAppPackageIndexMaxTables)
    {
        tables->push_back(candidate);
    }
}

static uint32_t appPackageRawEnd(const GuestPackage* package)
{
    static const uint32_t rawHeaderOffset = 96;
    if (!package || !package->file_data || package->file_size < rawHeaderOffset + 32u ||
        memcmp(package->file_data + rawHeaderOffset, "RAWD", 4) != 0)
    {
        return package ? package->file_size : 0;
    }
    uint32_t rawOffset = readPackageU32(package->file_data, rawHeaderOffset + 8u);
    uint32_t rawSize = readPackageU32(package->file_data, rawHeaderOffset + 12u);
    if (rawOffset > package->file_size || rawSize > package->file_size - rawOffset)
    {
        return package->file_size;
    }
    return rawOffset + rawSize;
}

static void appendAppPackageResourceTable(const GuestPackage* package,
    const AppPackageResourceTable& table, uint32_t packageEnd,
    std::vector<RuntimePackageResourceLookup>* output)
{
    if (!package || !output || packageEnd <= table.tableEnd)
    {
        return;
    }
    std::vector<AppPackageResourceRecord> records;
    records.reserve(table.count);
    for (uint32_t index = 0; index < table.count; ++index)
    {
        uint32_t record = table.base + table.countSize + index * table.recordSize;
        int nameLength = appPackageResourceNameLength(
            package->file_data + record, table.nameSize);
        uint32_t relativeOffset = readPackageU32(
            package->file_data, record + table.nameSize);
        if (nameLength <= 0 ||
            (uint32_t)nameLength > kAppPackageIndexMaxNameSize ||
            relativeOffset < table.tableEnd - table.base ||
            (uint64_t)table.base + relativeOffset >= packageEnd)
        {
            continue;
        }
        AppPackageResourceRecord item;
        item.name.assign((const char*)package->file_data + record, (size_t)nameLength);
        item.offset = table.base + relativeOffset;
        records.push_back(item);
    }
    std::sort(records.begin(), records.end(),
        [](const AppPackageResourceRecord& left, const AppPackageResourceRecord& right) {
            return left.offset < right.offset;
        });
    for (size_t index = 0; index < records.size(); ++index)
    {
        uint32_t nextOffset = packageEnd;
        for (size_t next = index + 1; next < records.size(); ++next)
        {
            if (records[next].offset > records[index].offset)
            {
                nextOffset = records[next].offset;
                break;
            }
        }
        if (nextOffset <= records[index].offset)
        {
            continue;
        }
        RuntimePackageResourceLookup item;
        item.name = records[index].name;
        item.offset = records[index].offset;
        item.size = nextOffset - records[index].offset;
        output->push_back(item);
    }
}

void buildAppPackageResourceLookup(const GuestPackage* package,
    std::vector<RuntimePackageResourceLookup>* output)
{
    if (!output)
    {
        return;
    }
    output->clear();
    if (!package || !package->file_data)
    {
        return;
    }
    uint32_t rawEnd = appPackageRawEnd(package);
    if (rawEnd >= package->file_size)
    {
        return;
    }
    std::vector<AppPackageResourceTable> tables;
    AppPackageResourceTable candidate = {};
    if (probeAppPackageResourceTableAt(package, rawEnd, &candidate))
    {
        addAppPackageResourceTable(&tables, candidate);
    }
    uint32_t scan = alignPackageOffset(rawEnd);
    for (uint32_t base = scan;
        base + 6u < package->file_size && tables.size() < kAppPackageIndexMaxTables;
        base += kAppPackageIndexScanAlignment)
    {
        if (base != rawEnd && probeAppPackageResourceTableAt(package, base, &candidate))
        {
            addAppPackageResourceTable(&tables, candidate);
        }
        if (base > UINT32_MAX - kAppPackageIndexScanAlignment)
        {
            break;
        }
    }
    std::sort(tables.begin(), tables.end(),
        [](const AppPackageResourceTable& left, const AppPackageResourceTable& right) {
            return left.base < right.base;
        });
    for (size_t index = 0; index < tables.size(); ++index)
    {
        uint32_t packageEnd = index + 1 < tables.size() ?
            tables[index + 1].base : package->file_size;
        appendAppPackageResourceTable(
            package, tables[index], packageEnd, output);
    }
    std::sort(output->begin(), output->end(),
        [](const RuntimePackageResourceLookup& left,
            const RuntimePackageResourceLookup& right) {
            return left.offset < right.offset;
        });
}
