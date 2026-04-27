#include "widget_internal.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_ui/common_ui_utils.h"

#include <algorithm>

namespace app_ui {

void BasicWidget::ApplyStyle(uint16_t style_id) {
    style_id_ = style_id;
    MarkDirty();
}

uint16_t BasicWidget::StyleId() const {
    return style_id_;
}

Size BasicWidget::OnMeasure(const Size& constraint) {
    return constraint;
}

Size ContainerWidget::OnMeasure(const Size& constraint) {
    return constraint;
}

void ContainerWidget::OnDraw(Painter&) {
}

void TextWidget::SetText(const std::string& text) {
    text_ = text;
    OnTextChanged();
    MarkMeasureDirty();
    MarkDirty();
}

void TextWidget::SetTextId(uint32_t text_id) {
    if (text_id_ == text_id) {
        return;
    }
    text_id_ = text_id;
    MarkMeasureDirty();
    MarkDirty();
}

uint32_t TextWidget::TextId() const {
    return text_id_;
}

const std::string& TextWidget::Text() const {
    return text_;
}

Size LabelWidget::OnMeasure(const Size& constraint) {
    return constraint;
}

void LabelWidget::SetProfile(const LabelProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const LabelProfile& LabelWidget::Profile() const {
    return profile_;
}

void LabelWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const bool focused = Focused() && profile_.focus_invert;
    auto *ep = dynamic_cast<EpdPainter *>(&p);
    const int16_t max_radius = static_cast<int16_t>(std::max<int>(0, (std::min<int>(rect.w, rect.h) - 1) / 2));
    const int16_t radius = std::max<int16_t>(0, std::min<int16_t>(profile_.corner_radius, max_radius));

    if (profile_.draw_border && rect.w > 0 && rect.h > 0) {
        if (profile_.draw_rounded_border && ep) {
            if (focused) {
                ep->Gfx().fillRoundRect(rect.x + ep->Offset().x,
                                        rect.y + ep->Offset().y,
                                        rect.w,
                                        rect.h,
                                        radius,
                                        GxEPD_BLACK);
                p.SetTextColor(Color::White);
            } else {
                p.SetDrawColor(Color::White);
                p.FillRect(rect);
                p.SetTextColor(Color::Black);
            }
            ep->Gfx().drawRoundRect(rect.x + ep->Offset().x,
                                    rect.y + ep->Offset().y,
                                    rect.w,
                                    rect.h,
                                    radius,
                                    GxEPD_BLACK);
        } else {
            p.SetDrawColor(focused ? Color::Black : Color::White);
            p.FillRect(rect);
            p.SetDrawColor(Color::Black);
            p.DrawRect(rect);
            p.SetTextColor(focused ? Color::White : Color::Black);
        }
    } else if (focused) {
        p.SetDrawColor(Color::Black);
        p.FillRect(rect);
        p.SetTextColor(Color::White);
    } else {
        p.SetTextColor(Color::Black);
    }

    int16_t content_left = 0;
    if (profile_.draw_left_prefix && !profile_.left_prefix.empty()) {
        const Size prefix_size = p.MeasureText(profile_.left_prefix.c_str(), nullptr);
        int16_t prefix_x = std::max<int16_t>(0, profile_.text_offset_x);
        int16_t prefix_y = profile_.text_offset_y;
        if (profile_.center_text_v) {
            prefix_y = static_cast<int16_t>((rect.h - prefix_size.h) / 2);
            if (prefix_y < 0) {
                prefix_y = 0;
            }
        }
        p.DrawText({prefix_x, prefix_y}, profile_.left_prefix.c_str());
        content_left = static_cast<int16_t>(prefix_x + prefix_size.w + std::max<int16_t>(0, profile_.left_prefix_gap));
    }

    int16_t text_x = std::max<int16_t>(content_left, profile_.text_offset_x);
    int16_t text_y = profile_.text_offset_y;
    if ((profile_.center_text_h || profile_.center_text_v) && rect.w > 0 && rect.h > 0 && !text_.empty()) {
        const Size text_size = p.MeasureText(text_.c_str(), nullptr);
        if (profile_.center_text_h) {
            const int center_origin = profile_.center_text_full_rect ? 0 : content_left;
            const int content_width = profile_.center_text_full_rect
                ? rect.w
                : std::max<int>(0, rect.w - content_left);
            text_x = static_cast<int16_t>(center_origin + (content_width - text_size.w) / 2);
            if (text_x < center_origin) {
                text_x = static_cast<int16_t>(center_origin);
            }
        }
        if (profile_.center_text_v) {
            text_y = static_cast<int16_t>((rect.h - text_size.h) / 2);
            if (text_y < 0) {
                text_y = 0;
            }
        }
    }
    p.DrawText({text_x, text_y}, text_.c_str());
}

void ButtonWidget::SetProfile(const ButtonProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const ButtonProfile& ButtonWidget::Profile() const {
    return profile_;
}

void ButtonWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const bool focused = Focused() && profile_.focus_invert;
    p.SetDrawColor(focused ? Color::Black : Color::White);
    p.FillRect(rect);
    p.SetDrawColor(Color::Black);
    p.DrawRect(rect);
    p.SetTextColor(focused ? Color::White : Color::Black);
    const Size text_size = p.MeasureText(text_.c_str(), nullptr);
    int16_t x = static_cast<int16_t>((rect.w - text_size.w) / 2);
    int16_t y = static_cast<int16_t>((rect.h - text_size.h) / 2);
    if (x < profile_.min_text_x) {
        x = profile_.min_text_x;
    }
    if (y < profile_.min_text_y) {
        y = profile_.min_text_y;
    }
    p.DrawText({x, y}, text_.c_str());
}

void ImageWidget::SetProfile(const ImageProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const ImageProfile& ImageWidget::Profile() const {
    return profile_;
}

void ImageWidget::OnTextChanged() {
    cached_bitmap_available_ = false;
    cached_bitmap_.Clear();
    if (text_.empty()) {
        return;
    }
    cached_bitmap_available_ = eteacher::app_ui::LoadBinImageOwned(text_, &cached_bitmap_);
}

void ImageWidget::SetQrCode(int size, const std::vector<uint8_t>& modules) {
    if (size <= 0 || static_cast<size_t>(size * size) != modules.size()) {
        ClearQrCode();
        return;
    }
    qr_size_ = size;
    qr_modules_ = modules;
    MarkDirty();
}

void ImageWidget::ClearQrCode() {
    qr_size_ = 0;
    qr_modules_.clear();
    MarkDirty();
}

bool ImageWidget::HasQrCode() const {
    return qr_size_ > 0 && static_cast<size_t>(qr_size_ * qr_size_) == qr_modules_.size();
}

void ImageWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const bool has_bitmap = cached_bitmap_available_ && cached_bitmap_.data() != nullptr &&
                            cached_bitmap_.width > 0 && cached_bitmap_.height > 0;
    const bool draw_border = profile_.draw_border && (!has_bitmap || profile_.draw_border_on_content);
    const int16_t border_thickness = draw_border ? std::max<int16_t>(1, profile_.border_thickness) : 0;
    const int16_t inset = std::max<int16_t>(border_thickness, std::max<int16_t>(0, profile_.content_inset));
    Rect inner = {inset,
                  inset,
                  static_cast<int16_t>(std::max<int>(0, rect.w - inset * 2)),
                  static_cast<int16_t>(std::max<int>(0, rect.h - inset * 2))};

    if (draw_border) {
        p.SetDrawColor(Color::Black);
        for (int16_t border = 0; border < border_thickness; ++border) {
            const Rect border_rect = {border,
                                      border,
                                      static_cast<int16_t>(std::max<int>(0, rect.w - border * 2)),
                                      static_cast<int16_t>(std::max<int>(0, rect.h - border * 2))};
            if (border_rect.w <= 0 || border_rect.h <= 0) {
                break;
            }
            p.DrawRect(border_rect);
        }
    }

    if (inner.w > 0 && inner.h > 0) {
        p.SetDrawColor(Color::White);
        p.FillRect(inner);
    }

    if (has_bitmap) {
        if (auto* ep = dynamic_cast<EpdPainter*>(&p)) {
            auto& gfx = ep->Gfx();
            const Point offset = ep->Offset();
            const int16_t draw_x = static_cast<int16_t>(offset.x + inner.x +
                                                        std::max<int16_t>(0, (inner.w - static_cast<int16_t>(cached_bitmap_.width)) / 2));
            const int16_t draw_y = static_cast<int16_t>(offset.y + inner.y +
                                                        std::max<int16_t>(0, (inner.h - static_cast<int16_t>(cached_bitmap_.height)) / 2));
            gfx.drawBitmap(draw_x,
                           draw_y,
                           cached_bitmap_.data(),
                           static_cast<int16_t>(cached_bitmap_.width),
                           static_cast<int16_t>(cached_bitmap_.height),
                           GxEPD_BLACK);
        }
        return;
    }

    if (HasQrCode() && inner.w > 0 && inner.h > 0) {
        const int quiet_zone_modules = std::max(0, profile_.quiet_zone_modules);
        const int total_modules = qr_size_ + quiet_zone_modules * 2;
        if (total_modules > 0) {
            const int scale_x = inner.w / total_modules;
            const int scale_y = inner.h / total_modules;
            const int module_px = std::min(scale_x, scale_y);
            if (module_px >= 1) {
                const int qr_px = total_modules * module_px;
                const int offset_x = inner.x + (inner.w - qr_px) / 2;
                const int offset_y = inner.y + (inner.h - qr_px) / 2;
                p.SetDrawColor(Color::Black);
                for (int y = 0; y < qr_size_; ++y) {
                    for (int x = 0; x < qr_size_; ++x) {
                        const size_t idx = static_cast<size_t>(y * qr_size_ + x);
                        if (idx >= qr_modules_.size() || qr_modules_[idx] == 0) {
                            continue;
                        }
                        const int px = offset_x + (x + quiet_zone_modules) * module_px;
                        const int py = offset_y + (y + quiet_zone_modules) * module_px;
                        p.FillRect({static_cast<int16_t>(px), static_cast<int16_t>(py),
                                    static_cast<int16_t>(module_px), static_cast<int16_t>(module_px)});
                    }
                }
                return;
            }
        }
    }

    if (profile_.draw_fallback_text && !text_.empty()) {
        p.SetTextColor(Color::Black);
        p.DrawText({profile_.text_offset_x, profile_.text_offset_y}, text_.c_str());
    }
}

TextAreaWidget::TextAreaWidget() {
    SetFontName("wenquanyi_11pt");
}

void TextAreaWidget::SetProfile(const TextAreaProfile& profile) {
    profile_ = profile;
    MarkMeasureDirty();
    MarkDirty();
}

const TextAreaProfile& TextAreaWidget::Profile() const {
    return profile_;
}

void TextAreaWidget::OnDraw(Painter& p) {
    Rect rect = LocalRect();
    const bool focused = Focused();
    p.SetDrawColor(focused ? Color::Black : Color::White);
    p.FillRect(rect);
    p.SetDrawColor(Color::Black);
    p.SetTextColor(focused ? Color::White : Color::Black);

    if (profile_.decoration_mode == TextAreaProfile::DecorationMode::Box) {
        p.DrawRect(rect);
    } else {
        const int16_t margin_x = std::max<int16_t>(0, profile_.underline_margin_x);
        const int16_t start_x = margin_x;
        const int16_t end_x = rect.w > margin_x ? static_cast<int16_t>(rect.w - margin_x) : 0;
        const int16_t step = std::max<int16_t>(1, static_cast<int16_t>(profile_.underline_segment + profile_.underline_gap));
        const int16_t segment = std::max<int16_t>(1, profile_.underline_segment);

        const int max_lines = std::max(1, profile_.max_lines);
        if (max_lines <= 1) {
            const int16_t y = rect.h > 0 ? static_cast<int16_t>(rect.h - 1) : 0;
            for (int16_t x = start_x; x < end_x; x = static_cast<int16_t>(x + step)) {
                int seg_w = segment;
                if (x + seg_w > end_x) {
                    seg_w = end_x - x;
                }
                if (seg_w > 0) {
                    p.DrawHLine({x, y}, seg_w);
                }
            }
            if (!text_.empty()) {
                p.DrawText({profile_.text_offset_x, profile_.text_offset_y}, text_.c_str());
            }
            return;
        }

        const Size line_ref = p.MeasureText("A", nullptr);
        const int16_t line_height = static_cast<int16_t>(std::max<int16_t>(1, line_ref.h));
        const int16_t line_step = static_cast<int16_t>(line_height + std::max<int16_t>(0, profile_.line_gap_px) + 1 + std::max<int16_t>(0, profile_.line_gap_px));
        const int wrap_width = std::max<int>(1, end_x - start_x);

        std::vector<std::string> lines;
        if (!text_.empty()) {
            lines = widget_internal::WrapTextByWidth(p, text_, wrap_width, max_lines);
            if (lines.empty()) {
                lines.push_back(text_);
            }
        } else {
            lines.push_back("");
        }

        for (size_t i = 0; i < lines.size() && i < static_cast<size_t>(max_lines); ++i) {
            const int16_t line_y = static_cast<int16_t>(profile_.text_offset_y + static_cast<int16_t>(i) * line_step);
            if (!lines[i].empty()) {
                p.DrawText({profile_.text_offset_x, line_y}, lines[i].c_str());
            }

            const int16_t underline_y = static_cast<int16_t>(line_y + line_height + std::max<int16_t>(0, profile_.line_gap_px));
            if (underline_y < 0 || underline_y >= rect.h) {
                continue;
            }
            for (int16_t x = start_x; x < end_x; x = static_cast<int16_t>(x + step)) {
                int seg_w = segment;
                if (x + seg_w > end_x) {
                    seg_w = end_x - x;
                }
                if (seg_w > 0) {
                    p.DrawHLine({x, underline_y}, seg_w);
                }
            }
        }
    }
}

} // namespace app_ui
