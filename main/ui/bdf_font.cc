#include "ui/bdf_font.h"

#include <cstring>

namespace {
constexpr size_t kHeaderSize = sizeof(BdfFontHeader);
constexpr size_t kEntrySize = sizeof(BdfGlyphEntry);

// Offsets within packed-on-disk structs
constexpr size_t kHdrMagicOff = 0;
constexpr size_t kHdrVersionOff = 4;
constexpr size_t kHdrAscentOff = 5;
constexpr size_t kHdrDescentOff = 7;
constexpr size_t kHdrGlyphCountOff = 9;
constexpr size_t kHdrBboxWOff = 13;
constexpr size_t kHdrBboxHOff = 15;
constexpr size_t kHdrBboxXOff = 17;
constexpr size_t kHdrBboxYOff = 19;

constexpr size_t kEntCodepointOff = 0;
constexpr size_t kEntWidthOff = 4;
constexpr size_t kEntHeightOff = 6;
constexpr size_t kEntXOffOff = 8;
constexpr size_t kEntYOffOff = 10;
constexpr size_t kEntAdvanceOff = 12;
constexpr size_t kEntBitmapOffOff = 14;

static size_t BitmapBytesFor(uint16_t w, uint16_t h) {
    const size_t row_bytes = (static_cast<size_t>(w) + 7u) / 8u;
    return row_bytes * static_cast<size_t>(h);
}
}

uint16_t BdfFont::ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

int16_t BdfFont::ReadLE16S(const uint8_t* p) {
    return static_cast<int16_t>(ReadLE16(p));
}

uint32_t BdfFont::ReadLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] |
                                (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16) |
                                (static_cast<uint32_t>(p[3]) << 24));
}

uint16_t BdfFont::ReadBE16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

int16_t BdfFont::ReadBE16S(const uint8_t* p) {
    return static_cast<int16_t>(ReadBE16(p));
}

uint32_t BdfFont::ReadBE32(const uint8_t* p) {
    return static_cast<uint32_t>((static_cast<uint32_t>(p[0]) << 24) |
                                (static_cast<uint32_t>(p[1]) << 16) |
                                (static_cast<uint32_t>(p[2]) << 8) |
                                 static_cast<uint32_t>(p[3]));
}

bool BdfFont::ReadHeader(const uint8_t* p, size_t size) {
    if (size < kHeaderSize) {
        return false;
    }

    std::memset(&header_, 0, sizeof(header_));
    std::memcpy(header_.magic, p + kHdrMagicOff, 4);
    header_.version = p[kHdrVersionOff];
    header_.ascent = Read16(p + kHdrAscentOff);
    header_.descent = Read16(p + kHdrDescentOff);
    header_.glyph_count = Read32(p + kHdrGlyphCountOff);
    header_.bbox_w = Read16S(p + kHdrBboxWOff);
    header_.bbox_h = Read16S(p + kHdrBboxHOff);
    header_.bbox_x = Read16S(p + kHdrBboxXOff);
    header_.bbox_y = Read16S(p + kHdrBboxYOff);

    return true;
}

bool BdfFont::Load(const void* data, size_t size) {
    data_ = nullptr;
    size_ = 0;
    entries_ = nullptr;
    bitmap_base_ = nullptr;
    bitmap_size_ = 0;
    std::memset(&header_, 0, sizeof(header_));

    if (data == nullptr || size < kHeaderSize) {
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);

    // Decide endianness up front based on whether the glyph table fits.
    if (std::memcmp(bytes + kHdrMagicOff, "BDFB", 4) != 0) {
        return false;
    }
    const uint32_t glyph_count_le = ReadLE32(bytes + kHdrGlyphCountOff);
    const uint32_t glyph_count_be = ReadBE32(bytes + kHdrGlyphCountOff);
    const uint64_t table_bytes_le = static_cast<uint64_t>(kHeaderSize) +
                                   static_cast<uint64_t>(glyph_count_le) * static_cast<uint64_t>(kEntrySize);
    const uint64_t table_bytes_be = static_cast<uint64_t>(kHeaderSize) +
                                   static_cast<uint64_t>(glyph_count_be) * static_cast<uint64_t>(kEntrySize);
    const bool le_ok = (glyph_count_le > 0) && (table_bytes_le <= size);
    const bool be_ok = (glyph_count_be > 0) && (table_bytes_be <= size);
    if (!le_ok && !be_ok) {
        return false;
    }
    big_endian_ = (!le_ok && be_ok);

    if (!ReadHeader(bytes, size)) {
        return false;
    }

    if (std::memcmp(header_.magic, "BDFB", 4) != 0) {
        return false;
    }

    const uint64_t table_bytes = static_cast<uint64_t>(kHeaderSize) +
                                 static_cast<uint64_t>(header_.glyph_count) * static_cast<uint64_t>(kEntrySize);
    if (table_bytes > size) {
        return false;
    }

    entries_ = bytes + kHeaderSize;
    bitmap_base_ = bytes + static_cast<size_t>(table_bytes);
    bitmap_size_ = size - static_cast<size_t>(table_bytes);

    // Validate that entries are sorted and bitmap offsets are in-range.
    uint32_t prev_cp = 0;
    bool have_prev = false;
    for (uint32_t i = 0; i < header_.glyph_count; ++i) {
        const uint8_t* ep = entries_ + static_cast<size_t>(i) * kEntrySize;
        const uint32_t cp = Read32(ep + kEntCodepointOff);
        const uint16_t w = Read16(ep + kEntWidthOff);
        const uint16_t h = Read16(ep + kEntHeightOff);
        const uint32_t off = Read32(ep + kEntBitmapOffOff);

        if (have_prev && cp <= prev_cp) {
            return false;
        }
        have_prev = true;
        prev_cp = cp;

        const size_t bmp_bytes = BitmapBytesFor(w, h);
        if (static_cast<uint64_t>(off) + static_cast<uint64_t>(bmp_bytes) > bitmap_size_) {
            return false;
        }
    }

    data_ = bytes;
    size_ = size;
    return true;
}

bool BdfFont::ReadGlyphEntry(size_t index, BdfGlyphEntry& out_entry) const {
    if (!IsLoaded() || index >= header_.glyph_count) {
        return false;
    }
    const uint8_t* ep = entries_ + index * kEntrySize;
    out_entry.codepoint = Read32(ep + kEntCodepointOff);
    out_entry.width = Read16(ep + kEntWidthOff);
    out_entry.height = Read16(ep + kEntHeightOff);
    out_entry.x_offset = Read16S(ep + kEntXOffOff);
    out_entry.y_offset = Read16S(ep + kEntYOffOff);
    out_entry.advance = Read16(ep + kEntAdvanceOff);
    out_entry.bitmap_offset = Read32(ep + kEntBitmapOffOff);
    return true;
}

bool BdfFont::FindGlyph(uint32_t codepoint, BdfGlyphEntry& out_entry) const {
    if (!IsLoaded() || header_.glyph_count == 0) {
        return false;
    }

    size_t lo = 0;
    size_t hi = static_cast<size_t>(header_.glyph_count);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const uint8_t* ep = entries_ + mid * kEntrySize;
        const uint32_t mid_cp = Read32(ep + kEntCodepointOff);

        if (mid_cp == codepoint) {
            return ReadGlyphEntry(mid, out_entry);
        }
        if (mid_cp < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return false;
}

const uint8_t* BdfFont::GlyphBitmap(const BdfGlyphEntry& entry) const {
    if (!IsLoaded()) {
        return nullptr;
    }
    const size_t bmp_bytes = BitmapBytesFor(entry.width, entry.height);
    const uint64_t end = static_cast<uint64_t>(entry.bitmap_offset) + static_cast<uint64_t>(bmp_bytes);
    if (end > bitmap_size_) {
        return nullptr;
    }
    return bitmap_base_ + entry.bitmap_offset;
}

bool BdfFont::GetGlyph(uint32_t codepoint, BdfGlyphEntry& out_entry, const uint8_t*& out_bitmap) const {
    out_bitmap = nullptr;
    if (!FindGlyph(codepoint, out_entry)) {
        return false;
    }
    out_bitmap = GlyphBitmap(out_entry);
    return out_bitmap != nullptr;
}
