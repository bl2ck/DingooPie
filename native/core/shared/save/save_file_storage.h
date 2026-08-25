#ifndef DINGOO_PIE_SHARED_SAVE_SAVE_FILE_STORAGE_H
#define DINGOO_PIE_SHARED_SAVE_SAVE_FILE_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

struct SaveFilePath
{
    std::string directory;
    std::string relativePath;
};

std::string saveFilePathJoin(const std::string& directory,
    const std::string& relativePath);
bool saveFilePathSplit(const std::string& path, SaveFilePath* output);
FILE* saveFileOpen(const std::string& path, const char* mode);
bool saveFileSync(FILE* file);
bool saveFileReadAll(const std::string& path, std::vector<uint8_t>* output,
    size_t maxSize = SIZE_MAX);
bool saveFileWriteAll(const std::string& path, const uint8_t* data, size_t size);
bool saveFileExists(const std::string& path);
bool saveFileCopy(const std::string& sourcePath,
    const std::string& destinationPath);
bool saveFileReplace(const std::string& path, const uint8_t* data, size_t size);
std::vector<std::string> saveFileRecoveryCandidates(const std::string& path);
bool saveFilePromoteRecoveryCandidate(const std::string& candidatePath,
    const std::string& destinationPath);

#endif
