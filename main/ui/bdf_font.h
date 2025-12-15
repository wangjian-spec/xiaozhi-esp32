#pragma once

#include <cstddef>
#include <cstdint>

#pragma pack(push, 1)
struct BdfFontHeader {
    char magic[4];      // "BDFB"
    uint8_t version;
    uint16_t ascent;
    uint16_t descent;
    uint32_t glyph_count;
    int16_t bbox_w, bbox_h, bbox_x, bbox_y;
};

struct BdfGlyphEntry {
    uint32_t codepoint;     // Unicode
    uint16_t width;
    uint16_t height;
    int16_t x_offset;
    int16_t y_offset;
    uint16_t advance;
    uint32_t bitmap_offset; // byte offset from start of bitmap data section
};
#pragma pack(pop)

static_assert(sizeof(BdfFontHeader) == 21, "Unexpected BdfFontHeader size; packing mismatch");
static_assert(sizeof(BdfGlyphEntry) == 18, "Unexpected BdfGlyphEntry size; packing mismatch");

class BdfFont {
public:
    BdfFont() = default;

    // Load from a memory buffer. The buffer must remain valid for the lifetime of this object.
    bool Load(const void* data, size_t size);

    bool IsLoaded() const { return data_ != nullptr; }

    const BdfFontHeader& Header() const { return header_; }

    // Binary-search a glyph by codepoint.
    // Returns true and writes the entry to out_entry if found.
    bool FindGlyph(uint32_t codepoint, BdfGlyphEntry& out_entry) const;

    // Returns a pointer into the original buffer to the glyph bitmap.
    // Returns nullptr if entry's bitmap is out of bounds or font not loaded.
    const uint8_t* GlyphBitmap(const BdfGlyphEntry& entry) const;

    // Convenience: find + return bitmap pointer.
    bool GetGlyph(uint32_t codepoint, BdfGlyphEntry& out_entry, const uint8_t*& out_bitmap) const;

private:
    static uint16_t ReadLE16(const uint8_t* p);
    static int16_t ReadLE16S(const uint8_t* p);
    static uint32_t ReadLE32(const uint8_t* p);

    static uint16_t ReadBE16(const uint8_t* p);
    static int16_t ReadBE16S(const uint8_t* p);
    static uint32_t ReadBE32(const uint8_t* p);

    uint16_t Read16(const uint8_t* p) const { return big_endian_ ? ReadBE16(p) : ReadLE16(p); }
    int16_t Read16S(const uint8_t* p) const { return big_endian_ ? ReadBE16S(p) : ReadLE16S(p); }
    uint32_t Read32(const uint8_t* p) const { return big_endian_ ? ReadBE32(p) : ReadLE32(p); }

    bool ReadHeader(const uint8_t* p, size_t size);
    bool ReadGlyphEntry(size_t index, BdfGlyphEntry& out_entry) const;

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;

    BdfFontHeader header_{};

    const uint8_t* entries_ = nullptr;
    const uint8_t* bitmap_base_ = nullptr;
    size_t bitmap_size_ = 0;

    bool big_endian_ = false;
};
