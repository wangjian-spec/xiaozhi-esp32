#ifndef STORAGE_SD_RESOURCE_H_
#define STORAGE_SD_RESOURCE_H_

#include <string>
#include <vector>
#include <mutex>

// Simple SD card resource reader (fonts, bitmaps, audio).
// Uses SDSPI mount at /sdcard. Paths are POSIX-style.
class SdResource {
public:
    static SdResource& GetInstance();

    // Initialize (idempotent). Returns true on success.
    bool Init();

    // Generic file read from SD (relative to mount root). Returns true on success.
    bool ReadFile(const std::string& relative_path, std::vector<uint8_t>& out);

    // Simple existence check; does not read file content.
    bool Exists(const std::string& relative_path);

    // Combined existence + read helper with logging; returns true when file is non-empty and fully read.
    bool LoadBinary(const std::string& relative_path, std::vector<uint8_t>& out, const char* label = nullptr);

    // Convenience wrappers for typical asset categories.
    bool ReadFont(const std::string& filename, std::vector<uint8_t>& out);
    bool ReadBitmap(const std::string& filename, std::vector<uint8_t>& out);
    bool ReadAudio(const std::string& filename, std::vector<uint8_t>& out);

    const std::string& mount_point() const { return mount_point_; }

private:
    SdResource() = default;
    SdResource(const SdResource&) = delete;
    SdResource& operator=(const SdResource&) = delete;

    bool mounted_ = false;
    std::string mount_point_ = "/sdcard";
    std::mutex mutex_;
};

#endif  // STORAGE_SD_RESOURCE_H_
