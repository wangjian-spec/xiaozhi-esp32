#include "custom_sd_fat.h"

#include <algorithm>

bool CustomSdFat::Begin(const SdSpiConfig& cfg) {
	opened_ = SdFat::begin(cfg);
	return opened_;
}

bool CustomSdFat::ReadFile(const char* path, std::vector<uint8_t>* out, size_t max_bytes) {
	if (!opened_ || !path || !out) {
		return false;
	}

	SdFile file;
	if (!file.open(path, O_RDONLY)) {
		return false;
	}

	const uint64_t size64 = file.fileSize();
	if (size64 > static_cast<uint64_t>(SIZE_MAX)) {
		file.close();
		return false;
	}

	const size_t size = static_cast<size_t>(size64);
	if (max_bytes != 0 && size > max_bytes) {
		file.close();
		return false;
	}

	out->clear();
	out->resize(size);

	size_t offset = 0;
	while (offset < size) {
		const size_t want = std::min<size_t>(size - offset, 4096);
		const int n = file.read(out->data() + offset, want);
		if (n <= 0) {
			file.close();
			return false;
		}
		offset += static_cast<size_t>(n);
	}

	file.close();
	return true;
}

bool CustomSdFat::ReadImage(const char* path, std::vector<uint8_t>* out, size_t max_bytes) {
	return ReadFile(path, out, max_bytes);
}

bool CustomSdFat::OpenImage(const char* path, SdFile* out) {
	if (!opened_ || !path || !out) {
		return false;
	}
	out->close();
	return out->open(path, O_RDONLY);
}

bool CustomSdFat::ReadMp3(const char* path, std::vector<uint8_t>* out, size_t max_bytes) {
	return ReadFile(path, out, max_bytes);
}

bool CustomSdFat::OpenMp3(const char* path, SdFile* out) {
	if (!opened_ || !path || !out) {
		return false;
	}
	out->close();
	return out->open(path, O_RDONLY);
}

