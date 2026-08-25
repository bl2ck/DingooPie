#include "shared/save/guest_save_transaction.h"
#include "shared/save/save_file_storage.h"
#include "shared/platform/storage_services.h"

#include <stdint.h>
#include <string.h>

static const char* kSaveTransactionBackupSuffix =
    ".dingoopie.transaction-v1.backup";
static const char* kSaveTransactionMarkerSuffix =
    ".dingoopie.transaction-v1.pending";

static bool modeTruncatesFile(const char* mode)
{
    return mode && strchr(mode, 'w') && !strchr(mode, 'a');
}

static std::string sidecarName(const char* name, const char* suffix)
{
    return std::string(name ? name : "") + suffix;
}

static bool writeMarker(const std::string& directory, const char* name, char state)
{
    std::string markerName = sidecarName(name, kSaveTransactionMarkerSuffix);
    FILE* marker = platformOpenStorageFile(directory, markerName, "wb");
    if (!marker)
    {
        return false;
    }
    bool ok = fwrite(&state, 1, 1, marker) == 1 && saveFileSync(marker);
    if (fclose(marker) != 0)
    {
        ok = false;
    }
    return ok;
}

static char readMarker(const std::string& directory, const char* name)
{
    std::string markerName = sidecarName(name, kSaveTransactionMarkerSuffix);
    FILE* marker = platformOpenStorageFile(directory, markerName, "rb");
    if (!marker)
    {
        return 0;
    }
    char state = 0;
    if (fread(&state, 1, 1, marker) != 1)
    {
        state = 0;
    }
    fclose(marker);
    return state;
}

static bool clearSidecars(const std::string& directory, const char* name)
{
    std::string backupName = sidecarName(name, kSaveTransactionBackupSuffix);
    std::string markerName = sidecarName(name, kSaveTransactionMarkerSuffix);
    if (!platformDeleteStorageFile(directory, backupName))
    {
        return false;
    }
    return platformDeleteStorageFile(directory, markerName);
}

static bool recoverTransaction(const std::string& directory, const char* name)
{
    std::string backupName = sidecarName(name, kSaveTransactionBackupSuffix);
    char state = readMarker(directory, name);
    if (!state)
    {
        return true;
    }
    if (state == 'C')
    {
        clearSidecars(directory, name);
        return true;
    }

    bool restored = state == '1' ?
        saveFileCopy(saveFilePathJoin(directory, backupName),
            saveFilePathJoin(directory, name)) :
        platformDeleteStorageFile(directory, name);
    if (!restored)
    {
        printf("fsys: save transaction recovery failed name=%s state=%c\n", name, state);
        return false;
    }
    if (writeMarker(directory, name, 'C'))
    {
        clearSidecars(directory, name);
    }
    printf("fsys: recovered interrupted save name=%s\n", name);
    return true;
}

static FILE* beginTransaction(const std::string& directory,
    const char* name, const char* mode)
{
    if (!recoverTransaction(directory, name))
    {
        return NULL;
    }

    std::string backupName = sidecarName(name, kSaveTransactionBackupSuffix);
    FILE* original = platformOpenStorageFile(directory, name, "rb");
    bool hadOriginal = original != NULL;
    if (original)
    {
        fclose(original);
        if (!saveFileCopy(saveFilePathJoin(directory, name),
            saveFilePathJoin(directory, backupName)))
        {
            return NULL;
        }
    }
    else
    {
        platformDeleteStorageFile(directory, backupName);
    }

    if (!writeMarker(directory, name, hadOriginal ? '1' : '0'))
    {
        platformDeleteStorageFile(directory, backupName);
        return NULL;
    }
    FILE* file = platformOpenStorageFile(directory, name, mode);
    if (!file)
    {
        recoverTransaction(directory, name);
    }
    return file;
}

static bool commitTransaction(const std::string& directory, const char* name)
{
    if (!writeMarker(directory, name, 'C'))
    {
        return false;
    }
    clearSidecars(directory, name);
    return true;
}

FILE* guestSaveOpenFile(const std::string& directory, const char* name,
    const char* mode, bool* transactional)
{
    bool useTransaction = modeTruncatesFile(mode);
    if (transactional)
    {
        *transactional = useTransaction;
    }
    return useTransaction ? beginTransaction(directory, name, mode) :
        (recoverTransaction(directory, name) ?
            platformOpenStorageFile(directory, name, mode) : NULL);
}

int guestSaveCloseFile(const std::string& directory, const char* name,
    FILE* file, bool transactional)
{
    bool transactionOk = !transactional || saveFileSync(file);
    int result = fclose(file);
    if (transactional && transactionOk && result == 0 &&
        !commitTransaction(directory, name))
    {
        return -1;
    }
    return result;
}
