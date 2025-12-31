#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// EnglishTeacher 墨水屏字库（BDF -> BIN）解析与渲染。
//
// BIN 格式（由 bdf_to_bin.py 生成）：
// - Header (21 bytes):
//   MAGIC(4) + VERSION(1) + ascent(2) + descent(2) + glyph_count(4)
//   + bbox_w(2) + bbox_h(2) + bbox_x(2) + bbox_y(2)
// - Glyph Table (18 bytes per glyph):
//   codepoint(4) + width(2) + height(2) + x_offset(2) + y_offset(2)
//   + advance(2) + offset(4)
// - Glyph Pixel Data:
//   行优先、按位存储，stride = (width + 7) / 8，bit7 对应 x=0。
//
// 注意：当前工具默认使用“大端”写入多字节字段；本模块按大端解析。

class Adafruit_GFX;

namespace eteacher::font_manager {

struct FontHeader {
    uint16_t ascent = 0;
    uint16_t descent = 0;
    uint32_t glyph_count = 0;

    int16_t bbox_w = 0;
    int16_t bbox_h = 0;
    int16_t bbox_x = 0;
    int16_t bbox_y = 0;
};

struct Glyph {
    uint32_t codepoint = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    int16_t x_offset = 0;
    int16_t y_offset = 0;
    uint16_t advance = 0;
    uint32_t offset = 0; // offset into glyph pixel data region
};

class Font {
public:
    Font() = default;

    // 从 flash 中的完整 bin blob 初始化（不会拷贝，内部只保存指针/大小）。
    bool Init(const uint8_t* data, size_t size);

    bool Ready() const { return ready_; }
    const FontHeader& Header() const { return header_; }

    // 二分查找 codepoint 对应字形，并返回像素数据指针/长度。
    // bitmap_size = stride * height。
    bool FindGlyph(uint32_t codepoint, Glyph* glyph, const uint8_t** bitmap, size_t* bitmap_size) const;

    // 渲染 UTF-8 字符串。
    // - x: 起始 X 坐标
    // - baseline_y: 基线 Y 坐标（文字“落在这条线”上，类似常见字体渲染）
    // 返回：渲染完成后的光标 x（可用于继续拼接绘制）。
    int16_t DrawUtf8(Adafruit_GFX& gfx, int16_t x, int16_t baseline_y, std::string_view utf8, uint16_t color) const;

    // 简单估算字符串宽度（按 advance 累加）。
    int16_t MeasureUtf8Width(std::string_view utf8) const;

private:
    const uint8_t* base_ = nullptr;
    size_t size_ = 0;

    const uint8_t* glyph_table_ = nullptr;
    const uint8_t* glyph_data_ = nullptr;
    size_t glyph_data_size_ = 0;

    FontHeader header_{};
    bool ready_ = false;
};

// 内置字体（通过 CMake EMBED_FILES 进入 flash）。
// 返回 nullptr 表示该字体未编译进固件。
const Font* GetBuiltinFont(std::string_view name);

} // namespace eteacher::font_manager
