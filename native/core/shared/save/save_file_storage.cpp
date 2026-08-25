#include "shared/save/save_file_storage.h"
#include "shared/platform/storage_services.h"

#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#endif

std::string saveFilePathJoin(const std::string& directory,
    const std::string& relativePath)
{
    return directory + "\n" + relativePath;
}

bool saveFilePathSplit(const std::string& path, SaveFilePath* output)
{
    size_t separator = path.find('\n');
    if (!output || separator == std::string::npos || separator == 0 ||
        separator + 1 >= path.size())
    {
        return false;
    }
    output->directory = path.substr(0, separator);
    output->relativePath = path.substr(separator + 1);
    return true;
}

FILE* saveFileOpen(const std::string& path, const char* mode)
{
    SaveFilePath parts;
    return saveFilePathSplit(path, &parts) ? platformOpenStorageFile(
        parts.directory, parts.relativePath, mode) : NULL;
}

bool saveFileSync(FILE* file)
{
    if (!file || fflush(file) != 0)
    {
        return false;
    }
    int descriptor = fileno(file);
    return descriptor < 0 ||
#ifdef _WIN32
        _commit(descriptor) == 0 ||
#else
        fsync(descriptor) == 0 ||
#endif
        errno == EINVAL || errno == ENOTSUP;
}

bool saveFileReadAll(const std::string& path, std::vector<uint8_t>* output,
    size_t maxSize)
{
    if (!output)
    {
        return false;
    }
    FILE* file = saveFileOpen(path, "rb");
    if (!file)
    {
        return false;
    }
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long size = ok ? ftell(file) : -1;
    ok = size >= 0 && (size_t)size <= maxSize &&
        fseek(file, 0, SEEK_SET) == 0;
    if (ok)
    {
        output->resize((size_t)size);
        ok = output->empty() ||
            fread(output->data(), 1, output->size(), file) == output->size();
    }
    if (fclose(file) != 0)
    {
        ok = false;
    }
    return ok;
}

bool saveFileWriteAll(const std::string& path, const uint8_t* data, size_t size)
{
    FILE* file = saveFileOpen(path, "wb");
    if (!file)
    {
        return false;
    }
    bool ok = (!size || fwrite(data, 1, size, file) == size) &&
        saveFileSync(file);
    if (fclose(file) != 0)
    {
        ok = false;
    }
    return ok;
}

bool saveFileExists(const std::string& path)
{
    FILE* file = saveFileOpen(path, "rb");
    if (!file)
    {
        return false;
    }
    fclose(file);
    return true;
}

bool saveFileCopy(const std::string& sourcePath,
    const std::string& destinationPath)
{
    FILE* source = saveFileOpen(sourcePath, "rb");
    FILE* destination = source ? saveFileOpen(destinationPath, "wb") : NULL;
    bool ok = source && destination;
    uint8_t buffer[64 * 1024];
    while (ok)
    {
        size_t bytesRead = fread(buffer, 1, sizeof(buffer), source);
        if (bytesRead && fwrite(buffer, 1, bytesRead, destination) != bytesRead)
        {
            ok = false;
        }
        if (bytesRead < sizeof(buffer))
        {
            if (ferror(source))
            {
                ok = false;
            }
            break;
        }
    }
    if (ok)
    {
        ok = saveFileSync(destination);
    }
    if (source)
    {
        fclose(source);
    }
    if (destination && fclose(destination) != 0)
    {
        ok = false;
    }
    return ok;
}

static void deleteSidecars(const SaveFilePath& path)
{
    platformDeleteStorageFile(path.directory, path.relativePath + ".tmp");
    platformDeleteStorageFile(path.directory, path.relativePath + ".backup");
}

bool saveFileReplace(const std::string& path, const uint8_t* data, size_t size)
{
    SaveFilePath parts;
    if (!saveFilePathSplit(path, &parts))
    {
        return false;
    }
    const std::string temporaryPath = saveFilePathJoin(
        parts.directory, parts.relativePath + ".tmp");
    const std::string backupPath = saveFilePathJoin(
        parts.directory, parts.relativePath + ".backup");
    platformDeleteStorageFile(parts.directory, parts.relativePath + ".tmp");
    if (!saveFileWriteAll(temporaryPath, data, size))
    {
        platformDeleteStorageFile(parts.directory, parts.relativePath + ".tmp");
        return false;
    }
    const bool hadOriginal = saveFileExists(path);
    platformDeleteStorageFile(parts.directory, parts.relativePath + ".backup");
    if (hadOriginal && !saveFileCopy(path, backupPath))
    {
        deleteSidecars(parts);
        return false;
    }
    bool ok = saveFileCopy(temporaryPath, path);
    if (!ok && hadOriginal)
    {
        saveFileCopy(backupPath, path);
    }
    deleteSidecars(parts);
    return ok;
}

std::vector<std::string> saveFileRecoveryCandidates(const std::string& path)
{
    SaveFilePath parts;
    if (!saveFilePathSplit(path, &parts))
    {
        return std::vector<std::string>();
    }
    return {
        path,
        saveFilePathJoin(parts.directory, parts.relativePath + ".tmp"),
        saveFilePathJoin(parts.directory, parts.relativePath + ".backup") };
}

bool saveFilePromoteRecoveryCandidate(const std::string& candidatePath,
    const std::string& destinationPath)
{
    SaveFilePath parts;
    if (!saveFilePathSplit(destinationPath, &parts) ||
        !saveFileCopy(candidatePath, destinationPath))
    {
        return false;
    }
    deleteSidecars(parts);
    return true;
}
