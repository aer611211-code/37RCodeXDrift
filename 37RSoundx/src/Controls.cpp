#include "Controls.h"

#include "Utils.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace sx {
namespace {

constexpr float AnimationSpeed = 9.5f;

float Approach(float value, float target, float dt) {
    return Lerp(value, target, 1.0f - std::exp(-AnimationSpeed * dt));
}

std::wstring FixedValue(float value, int precision) {
    std::wstringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(precision);
    stream << value;
    return stream.str();
}

}

bool RectF::Contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

D2D1_RECT_F RectF::ToD2D() const {
    return D2D1::RectF(x, y, x + w, y + h);
}

D2D1_COLOR_F Color(float r, float g, float b, float a) {
    return D2D1::ColorF(r, g, b, a);
}

D2D1_COLOR_F Mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float amount) {
    amount = Clamp(amount, 0.0f, 1.0f);
    return Color(
        a.r + (b.r - a.r) * amount,
        a.g + (b.g - a.g) * amount,
        a.b + (b.b - a.b) * amount,
        a.a + (b.a - a.a) * amount);
}

Theme CoffeeTheme() {
    Theme theme;
    theme.background = Color(0.105f, 0.078f, 0.058f);
    theme.panel = Color(0.165f, 0.121f, 0.087f);
    theme.panelStroke = Color(0.355f, 0.235f, 0.135f);
    theme.text = Color(0.965f, 0.885f, 0.735f);
    theme.mutedText = Color(0.660f, 0.555f, 0.440f);
    theme.control = Color(0.245f, 0.170f, 0.110f);
    theme.controlHover = Color(0.330f, 0.230f, 0.145f);
    theme.gold = Color(0.930f, 0.640f, 0.235f);
    theme.goldSoft = Color(0.720f, 0.480f, 0.205f);
    theme.active = Color(0.235f, 0.690f, 0.610f);
    theme.danger = Color(0.800f, 0.250f, 0.180f);
    return theme;
}

void RenderContext::FillRounded(const RectF& rect, const D2D1_COLOR_F& color, float radius) {
    fill->SetColor(color);
    target->FillRoundedRectangle(D2D1::RoundedRect(rect.ToD2D(), radius, radius), fill);
}

void RenderContext::StrokeRounded(const RectF& rect, const D2D1_COLOR_F& color, float radius, float width) {
    stroke->SetColor(color);
    target->DrawRoundedRectangle(D2D1::RoundedRect(rect.ToD2D(), radius, radius), stroke, width);
}

void RenderContext::FillRect(const RectF& rect, const D2D1_COLOR_F& color) {
    fill->SetColor(color);
    target->FillRectangle(rect.ToD2D(), fill);
}

void RenderContext::Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& color, float width) {
    stroke->SetColor(color);
    target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), stroke, width);
}

void RenderContext::Text(const std::wstring& value, IDWriteTextFormat* format, const RectF& rect, const D2D1_COLOR_F& color) {
    if (!format || value.empty()) {
        return;
    }
    fill->SetColor(color);
    target->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), format, rect.ToD2D(), fill, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void SliderControl::Configure(std::wstring label, float minValue, float maxValue, float value, std::wstring suffix) {
    label_ = std::move(label);
    suffix_ = std::move(suffix);
    minValue_ = minValue;
    maxValue_ = maxValue;
    SetValue(value, false);
}

void SliderControl::SetRect(const RectF& rect) {
    rect_ = rect;
}

void SliderControl::SetValue(float value, bool notify) {
    value_ = Clamp(value, minValue_, maxValue_);
    if (notify && onChanged_) {
        onChanged_(value_);
    }
}

float SliderControl::Value() const {
    return value_;
}

void SliderControl::SetOnChanged(std::function<void(float)> callback) {
    onChanged_ = std::move(callback);
}

void SliderControl::Update(float dt, float mouseX, float mouseY) {
    hover_ = Approach(hover_, rect_.Contains(mouseX, mouseY) || dragging_ ? 1.0f : 0.0f, dt);
}

void SliderControl::Draw(RenderContext& context) const {
    const auto& theme = context.theme;
    context.Text(label_, context.labelFormat, { rect_.x, rect_.y, rect_.w * 0.5f, 24.0f }, theme.text);
    context.Text(ValueText(), context.smallFormat, { rect_.x + rect_.w - 96.0f, rect_.y + 1.0f, 96.0f, 24.0f }, theme.gold);

    const float trackX = rect_.x;
    const float trackY = rect_.y + 34.0f;
    const float trackW = rect_.w;
    const RectF track{ trackX, trackY, trackW, 8.0f };
    const float progress = (value_ - minValue_) / std::max(0.0001f, maxValue_ - minValue_);
    const RectF fill{ trackX, trackY, trackW * progress, 8.0f };
    const float knobX = trackX + trackW * progress;

    context.FillRounded(track, Mix(theme.control, theme.controlHover, hover_ * 0.45f), 4.0f);
    context.FillRounded(fill, Mix(theme.goldSoft, theme.gold, hover_), 4.0f);
    context.FillRounded({ knobX - 8.0f, trackY - 6.0f, 16.0f, 20.0f }, Mix(theme.gold, theme.text, hover_ * 0.22f), 7.0f);
}

bool SliderControl::OnMouseDown(float x, float y) {
    const RectF hit{ rect_.x - 4.0f, rect_.y + 24.0f, rect_.w + 8.0f, 30.0f };
    if (!hit.Contains(x, y)) {
        return false;
    }
    dragging_ = true;
    UpdateFromPosition(x, true);
    return true;
}

void SliderControl::OnMouseMove(float x, float) {
    if (dragging_) {
        UpdateFromPosition(x, true);
    }
}

void SliderControl::OnMouseUp() {
    dragging_ = false;
}

void SliderControl::UpdateFromPosition(float x, bool notify) {
    const float progress = Clamp((x - rect_.x) / std::max(1.0f, rect_.w), 0.0f, 1.0f);
    SetValue(minValue_ + progress * (maxValue_ - minValue_), notify);
}

std::wstring SliderControl::ValueText() const {
    if (suffix_ == L"%") {
        return FixedValue(value_ * 100.0f, 0) + suffix_;
    }
    if (suffix_ == L"st") {
        const std::wstring sign = value_ > 0.01f ? L"+" : L"";
        return sign + FixedValue(value_, 1) + L" st";
    }
    if (suffix_ == L"dB") {
        const std::wstring sign = value_ > 0.01f ? L"+" : L"";
        return sign + FixedValue(value_, 1) + L" dB";
    }
    return FixedValue(value_, 2) + suffix_;
}

void ToggleControl::Configure(std::wstring label, bool enabled) {
    label_ = std::move(label);
    enabled_ = enabled;
    amount_ = enabled ? 1.0f : 0.0f;
}

void ToggleControl::SetRect(const RectF& rect) {
    rect_ = rect;
}

void ToggleControl::SetEnabled(bool enabled, bool notify) {
    enabled_ = enabled;
    if (notify && onChanged_) {
        onChanged_(enabled_);
    }
}

bool ToggleControl::Enabled() const {
    return enabled_;
}

void ToggleControl::SetOnChanged(std::function<void(bool)> callback) {
    onChanged_ = std::move(callback);
}

void ToggleControl::Update(float dt, float mouseX, float mouseY) {
    hover_ = Approach(hover_, rect_.Contains(mouseX, mouseY) ? 1.0f : 0.0f, dt);
    amount_ = Approach(amount_, enabled_ ? 1.0f : 0.0f, dt);
}

void ToggleControl::Draw(RenderContext& context) const {
    const auto& theme = context.theme;
    context.Text(label_, context.labelFormat, { rect_.x, rect_.y + 3.0f, rect_.w - 76.0f, 28.0f }, theme.text);

    const RectF track{ rect_.x + rect_.w - 66.0f, rect_.y, 66.0f, 32.0f };
    const D2D1_COLOR_F off = Mix(theme.control, theme.controlHover, hover_);
    const D2D1_COLOR_F on = Mix(theme.goldSoft, theme.active, amount_);
    context.FillRounded(track, Mix(off, on, amount_), 8.0f);
    context.StrokeRounded(track, Mix(theme.panelStroke, theme.gold, hover_ * 0.5f), 8.0f);

    const float knobX = track.x + 5.0f + amount_ * 34.0f;
    context.FillRounded({ knobX, track.y + 5.0f, 22.0f, 22.0f }, Mix(theme.text, theme.gold, amount_ * 0.35f), 7.0f);
}

bool ToggleControl::OnMouseDown(float x, float y) {
    if (!rect_.Contains(x, y)) {
        return false;
    }
    SetEnabled(!enabled_, true);
    return true;
}

void DropdownControl::Configure(std::wstring label) {
    label_ = std::move(label);
}

void DropdownControl::SetRect(const RectF& rect) {
    rect_ = rect;
}

void DropdownControl::SetItems(std::vector<std::wstring> items, int selectedIndex) {
    items_ = std::move(items);
    if (items_.empty()) {
        selectedIndex_ = -1;
        scrollOffset_ = 0;
        scrollPosition_ = 0.0f;
    } else {
        selectedIndex_ = Clamp(selectedIndex, 0, static_cast<int>(items_.size()) - 1);
    }
    scrollOffset_ = Clamp(scrollOffset_, 0, std::max(0, static_cast<int>(items_.size()) - 6));
    scrollPosition_ = Clamp(scrollPosition_, 0.0f, static_cast<float>(std::max(0, static_cast<int>(items_.size()) - 6)));
}

void DropdownControl::SetSelectedIndex(int index, bool notify) {
    if (items_.empty()) {
        selectedIndex_ = -1;
        return;
    }
    selectedIndex_ = Clamp(index, 0, static_cast<int>(items_.size()) - 1);
    if (notify && onChanged_) {
        onChanged_(selectedIndex_);
    }
}

int DropdownControl::SelectedIndex() const {
    return selectedIndex_;
}

bool DropdownControl::IsOpen() const {
    return open_;
}

void DropdownControl::Close() {
    open_ = false;
}

void DropdownControl::SetOnChanged(std::function<void(int)> callback) {
    onChanged_ = std::move(callback);
}

void DropdownControl::Update(float dt, float mouseX, float mouseY) {
    hover_ = Approach(hover_, rect_.Contains(mouseX, mouseY) || (open_ && MenuRect().Contains(mouseX, mouseY)) ? 1.0f : 0.0f, dt);
    openAmount_ = Approach(openAmount_, open_ ? 1.0f : 0.0f, dt);
    scrollPosition_ = Approach(scrollPosition_, static_cast<float>(scrollOffset_), dt);
}

void DropdownControl::DrawBase(RenderContext& context) const {
    const auto& theme = context.theme;
    context.Text(label_, context.smallFormat, { rect_.x, rect_.y - 24.0f, rect_.w, 20.0f }, theme.mutedText);
    context.FillRounded(rect_, Mix(theme.control, theme.controlHover, hover_), 8.0f);
    context.StrokeRounded(rect_, Mix(theme.panelStroke, theme.gold, hover_ * 0.45f), 8.0f);

    const std::wstring value = selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(items_.size()) ? items_[selectedIndex_] : L"No device";
    context.Text(value, context.labelFormat, { rect_.x + 14.0f, rect_.y + 10.0f, rect_.w - 52.0f, rect_.h - 12.0f }, theme.text);

    const float cx = rect_.x + rect_.w - 24.0f;
    const float cy = rect_.y + rect_.h * 0.5f;
    const float direction = openAmount_ * 2.0f - 1.0f;
    context.Line(cx - 6.0f, cy - 2.0f * direction, cx, cy + 4.0f * direction, theme.gold, 2.0f);
    context.Line(cx + 6.0f, cy - 2.0f * direction, cx, cy + 4.0f * direction, theme.gold, 2.0f);
}

void DropdownControl::DrawMenu(RenderContext& context) const {
    if (openAmount_ <= 0.01f || items_.empty()) {
        return;
    }

    const auto& theme = context.theme;
    RectF menu = MenuRect();
    menu.h *= openAmount_;
    context.FillRounded(menu, Color(0.120f, 0.087f, 0.062f, 0.98f), 8.0f);
    context.StrokeRounded(menu, theme.goldSoft, 8.0f);

    const int first = Clamp(static_cast<int>(std::floor(scrollPosition_)), 0, std::max(0, static_cast<int>(items_.size()) - 1));
    const int last = std::min<int>(static_cast<int>(items_.size()), first + 8);
    for (int itemIndex = first; itemIndex < last; ++itemIndex) {
        const float y = menu.y + 6.0f + (static_cast<float>(itemIndex) - scrollPosition_) * 38.0f;
        const RectF item{ menu.x + 6.0f, y, menu.w - 12.0f, 32.0f };
        if (item.y + item.h < menu.y + 4.0f || item.y > menu.y + menu.h - 4.0f) {
            continue;
        }
        if (itemIndex == selectedIndex_) {
            context.FillRounded(item, Color(theme.gold.r, theme.gold.g, theme.gold.b, 0.16f), 6.0f);
        }
        context.Text(items_[itemIndex], context.smallFormat, { item.x + 9.0f, item.y + 7.0f, item.w - 18.0f, item.h }, itemIndex == selectedIndex_ ? theme.gold : theme.text);
    }
}

bool DropdownControl::OnMouseDown(float x, float y) {
    if (open_) {
        const int hit = HitItem(x, y);
        if (hit >= 0) {
            SetSelectedIndex(hit, true);
            open_ = false;
            return true;
        }
        if (!rect_.Contains(x, y)) {
            open_ = false;
            return false;
        }
    }

    if (rect_.Contains(x, y)) {
        open_ = !open_;
        return true;
    }
    return false;
}

bool DropdownControl::OnMouseWheel(short delta) {
    if (!open_ || items_.size() <= 6) {
        return false;
    }
    const int maxOffset = std::max(0, static_cast<int>(items_.size()) - 6);
    scrollOffset_ = Clamp(scrollOffset_ + (delta < 0 ? 1 : -1), 0, maxOffset);
    return true;
}

RectF DropdownControl::MenuRect() const {
    const int visible = std::min<int>(6, static_cast<int>(items_.size()));
    return { rect_.x, rect_.y + rect_.h + 8.0f, rect_.w, 12.0f + visible * 38.0f };
}

int DropdownControl::HitItem(float x, float y) const {
    if (!MenuRect().Contains(x, y)) {
        return -1;
    }

    const float local = (y - (rect_.y + rect_.h + 14.0f)) / 38.0f;
    const int index = static_cast<int>(std::floor(local + scrollPosition_));
    if (local < 0.0f || index < 0 || index >= static_cast<int>(items_.size()) || local >= 6.0f) {
        return -1;
    }
    return index;
}

}
