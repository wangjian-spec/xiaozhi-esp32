#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SdFat.h>

class CustomSdFat : public SdFat {
public:
    CustomSdFat() = default;

    bool Begin(const SdSpiConfig& cfg);

    // Read entire file into memory.
    // If max_bytes != 0, rejects files larger than max_bytes.
    bool ReadFile(const char* path, std::vector<uint8_t>* out, size_t max_bytes = 0);

    // Image API: returns raw file bytes (decode is caller's job).
    bool ReadImage(const char* path, std::vector<uint8_t>* out, size_t max_bytes = 0);
    bool OpenImage(const char* path, SdFile* out);

    // MP3 API: returns raw file bytes or an opened file for streaming.
    bool ReadMp3(const char* path, std::vector<uint8_t>* out, size_t max_bytes = 0);
    bool OpenMp3(const char* path, SdFile* out);

private:
    bool opened_ = false;
};
