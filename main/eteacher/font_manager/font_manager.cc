#include "eteacher/font_manager/font_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <esp_log.h>

#include "assets.h"

// GxEPD2 的绘制基类来自 Adafruit_GFX。
#include <Adafruit_GFX.h>

namespace eteacher::font_manager {
namespace {

static constexpr char kTag[] = "font_manager";

static constexpr size_t kHeaderSize = 21;
static constexpr size_t kGlyphEntrySize = 18; // 见 bdf_to_bin.py: >IHHhhHI
static constexpr uint8_t kVersion = 1;

static inline uint16_t ReadBE16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static inline int16_t ReadSBE16(const uint8_t* p) {
    return static_cast<int16_t>(ReadBE16(p));
}

static inline uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           (static_cast<uint32_t>(p[3]));
}

static bool ReadHeader(const uint8_t* data, size_t size, FontHeader* out, const uint8_t** glyph_table, const uint8_t** glyph_data, size_t* glyph_data_size) {
    if (!data || !out || size < kHeaderSize) {
        return false;
    }

    // MAGIC
    if (!(data[0] == 'B' && data[1] == 'D' && data[2] == 'F' && data[3] == 'B')) {
        ESP_LOGE(kTag, "Bad MAGIC");
        return false;
    }
    // VERSION
    if (data[4] != kVersion) {
        ESP_LOGE(kTag, "Unsupported VERSION=%u", static_cast<unsigned>(data[4]));
        return false;
    }

    // Header layout: MAGIC(4) VERSION(1) ascent(2) descent(2) glyph_count(4) bbox(8)
    out->ascent = ReadBE16(&data[5]);
    out->descent = ReadBE16(&data[7]);
    out->glyph_count = ReadBE32(&data[9]);
    out->bbox_w = ReadSBE16(&data[13]);
    out->bbox_h = ReadSBE16(&data[15]);
    out->bbox_x = ReadSBE16(&data[17]);
    out->bbox_y = ReadSBE16(&data[19]);

    const size_t table_bytes = static_cast<size_t>(out->glyph_count) * kGlyphEntrySize;
    if (size < kHeaderSize + table_bytes) {
        ESP_LOGE(kTag, "Truncated glyph table (size=%u glyphs=%u)", static_cast<unsigned>(size), static_cast<unsigned>(out->glyph_count));
        return false;
    }

    if (glyph_table) {
        *glyph_table = data + kHeaderSize;
    }

    const uint8_t* gd = data + kHeaderSize + table_bytes;
    const size_t gd_size = size - (kHeaderSize + table_bytes);

    if (glyph_data) {
        *glyph_data = gd;
    }
    if (glyph_data_size) {
        *glyph_data_size = gd_size;
    }

    return true;
}

static bool ReadGlyphAt(const uint8_t* glyph_table, uint32_t index, Glyph* out) {
    if (!glyph_table || !out) {
        return false;
    }

    const uint8_t* p = glyph_table + static_cast<size_t>(index) * kGlyphEntrySize;

    out->codepoint = ReadBE32(&p[0]);
    out->width = ReadBE16(&p[4]);
    out->height = ReadBE16(&p[6]);
    out->x_offset = ReadSBE16(&p[8]);
    out->y_offset = ReadSBE16(&p[10]);
    out->advance = ReadBE16(&p[12]);
    out->offset = ReadBE32(&p[14]);

    return true;
}

// UTF-8 解码：读取下一个 codepoint。
// - 返回 true 表示成功（即使遇到非法序列也会返回 U+FFFD）
// - p 会前进
static bool Utf8Next(const char*& p, const char* end, uint32_t* out_cp) {
    if (!p || p >= end || !out_cp) {
        return false;
    }

    const uint8_t c0 = static_cast<uint8_t>(*p++);
    if (c0 < 0x80) {
        *out_cp = c0;
        return true;
    }

    auto bad = [&]() {
        *out_cp = 0xFFFD;
        return true;
    };

    // 2-byte
    if ((c0 & 0xE0) == 0xC0) {
        if (p >= end) return bad();
        const uint8_t c1 = static_cast<uint8_t>(*p);
        if ((c1 & 0xC0) != 0x80) return bad();
        ++p;
        const uint32_t cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        if (cp < 0x80) return bad();
        *out_cp = cp;
        return true;
    }

    // 3-byte
    if ((c0 & 0xF0) == 0xE0) {
        if (end - p < 2) return bad();
        const uint8_t c1 = static_cast<uint8_t>(p[0]);
        const uint8_t c2 = static_cast<uint8_t>(p[1]);
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) return bad();
        p += 2;
        const uint32_t cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        // overlong / surrogate
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return bad();
        *out_cp = cp;
        return true;
    }

    // 4-byte
    if ((c0 & 0xF8) == 0xF0) {
        if (end - p < 3) return bad();
        const uint8_t c1 = static_cast<uint8_t>(p[0]);
        const uint8_t c2 = static_cast<uint8_t>(p[1]);
        const uint8_t c3 = static_cast<uint8_t>(p[2]);
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) return bad();
        p += 3;
        const uint32_t cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) return bad();
        *out_cp = cp;
        return true;
    }

    return bad();
}

static inline bool GlyphBitIsSet(const uint8_t* bitmap, uint16_t width, uint16_t x, uint16_t y) {
    const uint16_t stride = static_cast<uint16_t>((width + 7) / 8);
    const size_t byte_index = static_cast<size_t>(y) * stride + (x >> 3);
    const uint8_t b = bitmap[byte_index];
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7));
    return (b & mask) != 0;
}

// EnglishTeacher 的字库文件随 assets.bin 写入 assets 分区（不占用 app 镜像空间）。
// build_default_assets.py 会把 main/eteacher/font_manager 下的文件复制到 assets.bin 根目录。
struct BuiltinFontAsset {
    std::string_view name;      // 对外字体名（不含扩展名）
    std::string_view filename;  // assets.bin 内文件名（含 .bin）
};

static const BuiltinFontAsset kBuiltinFonts[] = {
    {"wenquanyi_9pt", "wenquanyi_9pt.bin"},
    {"wenquanyi_11pt", "wenquanyi_11pt.bin"},
};

} // namespace

bool Font::Init(const uint8_t* data, size_t size) {
    ready_ = false;
    base_ = data;
    size_ = size;
    glyph_table_ = nullptr;
    glyph_data_ = nullptr;
    glyph_data_size_ = 0;
    header_ = {};

    const uint8_t* gt = nullptr;
    const uint8_t* gd = nullptr;
    size_t gd_size = 0;

    if (!ReadHeader(data, size, &header_, &gt, &gd, &gd_size)) {
        return false;
    }

    glyph_table_ = gt;
    glyph_data_ = gd;
    glyph_data_size_ = gd_size;
    ready_ = true;

    ESP_LOGI(kTag, "Font ready: ascent=%u descent=%u glyphs=%u bbox=(%d,%d,%d,%d)",
             static_cast<unsigned>(header_.ascent),
             static_cast<unsigned>(header_.descent),
             static_cast<unsigned>(header_.glyph_count),
             static_cast<int>(header_.bbox_w),
             static_cast<int>(header_.bbox_h),
             static_cast<int>(header_.bbox_x),
             static_cast<int>(header_.bbox_y));

    return true;
}

bool Font::FindGlyph(uint32_t codepoint, Glyph* glyph, const uint8_t** bitmap, size_t* bitmap_size) const {
    if (!ready_ || !glyph_table_ || !glyph_data_ || header_.glyph_count == 0) {
        return false;
    }

    // 约定：glyph 表按 codepoint 升序（bdf_to_bin.py 通常会对 codepoint 排序）。
    uint32_t lo = 0;
    uint32_t hi = header_.glyph_count;
    Glyph tmp;

    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        ReadGlyphAt(glyph_table_, mid, &tmp);

        if (tmp.codepoint < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo >= header_.glyph_count) {
        return false;
    }

    ReadGlyphAt(glyph_table_, lo, &tmp);
    if (tmp.codepoint != codepoint) {
        return false;
    }

    const uint16_t stride = static_cast<uint16_t>((tmp.width + 7) / 8);
    const size_t bytes = static_cast<size_t>(stride) * tmp.height;

    if (tmp.offset >= glyph_data_size_) {
        return false;
    }
    if (bytes > (glyph_data_size_ - tmp.offset)) {
        return false;
    }

    if (glyph) {
        *glyph = tmp;
    }
    if (bitmap) {
        *bitmap = glyph_data_ + tmp.offset;
    }
    if (bitmap_size) {
        *bitmap_size = bytes;
    }

    return true;
}

int16_t Font::DrawUtf8(Adafruit_GFX& gfx, int16_t x, int16_t baseline_y, std::string_view utf8, uint16_t color) const {
    if (!ready_) {
        return x;
    }

    const char* p = utf8.data();
    const char* end = utf8.data() + utf8.size();

    while (p < end) {
        uint32_t cp = 0;
        if (!Utf8Next(p, end, &cp)) {
            break;
        }

        Glyph g;
        const uint8_t* bitmap = nullptr;
        size_t bitmap_size = 0;

        // 找不到字形时用 '?' 兜底。
        if (!FindGlyph(cp, &g, &bitmap, &bitmap_size)) {
            (void)bitmap_size;
            if (!FindGlyph(static_cast<uint32_t>('?'), &g, &bitmap, &bitmap_size)) {
                // 连 '?' 都没有就跳过
                continue;
            }
        }

        // BDF 的 (x_offset, y_offset) 定义：相对“字符原点(基线左端)”到 bitmap 左下角。
        // 屏幕坐标 y 向下，因此 top_y 需要从 baseline 往上推。
        const int16_t left_x = static_cast<int16_t>(x + g.x_offset);
        const int16_t top_y = static_cast<int16_t>(baseline_y - (g.y_offset + static_cast<int16_t>(g.height)));

        for (uint16_t yy = 0; yy < g.height; ++yy) {
            for (uint16_t xx = 0; xx < g.width; ++xx) {
                if (GlyphBitIsSet(bitmap, g.width, xx, yy)) {
                    gfx.drawPixel(static_cast<int16_t>(left_x + static_cast<int16_t>(xx)),
                                  static_cast<int16_t>(top_y + static_cast<int16_t>(yy)),
                                  color);
                }
            }
        }

        x = static_cast<int16_t>(x + static_cast<int16_t>(g.advance));
    }

    return x;
}

int16_t Font::MeasureUtf8Width(std::string_view utf8) const {
    if (!ready_) {
        return 0;
    }

    int32_t width = 0;
    const char* p = utf8.data();
    const char* end = utf8.data() + utf8.size();

    while (p < end) {
        uint32_t cp = 0;
        if (!Utf8Next(p, end, &cp)) {
            break;
        }

        Glyph g;
        const uint8_t* bitmap = nullptr;
        size_t bitmap_size = 0;
        if (!FindGlyph(cp, &g, &bitmap, &bitmap_size)) {
            if (!FindGlyph(static_cast<uint32_t>('?'), &g, &bitmap, &bitmap_size)) {
                continue;
            }
        }
        width += g.advance;
    }

    return static_cast<int16_t>(std::clamp<int32_t>(width, 0, 0x7FFF));
}

const Font* GetBuiltinFont(std::string_view name) {
    // 懒初始化：避免全局构造顺序问题。
    struct Slot {
        Font font;
        bool tried = false;
    };

    static Slot slots[sizeof(kBuiltinFonts) / sizeof(kBuiltinFonts[0])];

    for (size_t i = 0; i < (sizeof(kBuiltinFonts) / sizeof(kBuiltinFonts[0])); ++i) {
        if (kBuiltinFonts[i].name != name) {
            continue;
        }

        if (!slots[i].tried) {
            slots[i].tried = true;

            void* ptr = nullptr;
            size_t size = 0;

            // assets 分区未烧录/校验失败时，GetAssetData 会返回 false。
            if (!Assets::GetInstance().GetAssetData(std::string(kBuiltinFonts[i].filename), ptr, size) || !ptr || size == 0) {
                ESP_LOGE(kTag, "Font asset not found in assets partition: %.*s",
                         static_cast<int>(kBuiltinFonts[i].filename.size()),
                         kBuiltinFonts[i].filename.data());
            } else if (!slots[i].font.Init(static_cast<const uint8_t*>(ptr), size)) {
                ESP_LOGE(kTag, "Failed to init builtin font from assets: %.*s", static_cast<int>(name.size()), name.data());
            }
        }

        return slots[i].font.Ready() ? &slots[i].font : nullptr;
    }

    return nullptr;
}

} // namespace eteacher::font_manager
