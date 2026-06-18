#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <string>
#include <vector>

namespace sx {

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool Contains(float px, float py) const;
    D2D1_RECT_F ToD2D() const;
};

struct Theme {
    D2D1_COLOR_F background;
    D2D1_COLOR_F panel;
    D2D1_COLOR_F panelStroke;
    D2D1_COLOR_F text;
    D2D1_COLOR_F mutedText;
    D2D1_COLOR_F control;
    D2D1_COLOR_F controlHover;
    D2D1_COLOR_F gold;
    D2D1_COLOR_F goldSoft;
    D2D1_COLOR_F active;
    D2D1_COLOR_F danger;
};

struct RenderContext {
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* fill = nullptr;
    ID2D1SolidColorBrush* stroke = nullptr;
    IDWriteTextFormat* titleFormat = nullptr;
    IDWriteTextFormat* sectionFormat = nullptr;
    IDWriteTextFormat* labelFormat = nullptr;
    IDWriteTextFormat* smallFormat = nullptr;
    Theme theme{};

    void FillRounded(const RectF& rect, const D2D1_COLOR_F& color, float radius);
    void StrokeRounded(const RectF& rect, const D2D1_COLOR_F& color, float radius, float width = 1.0f);
    void FillRect(const RectF& rect, const D2D1_COLOR_F& color);
    void Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& color, float width = 1.0f);
    void Text(const std::wstring& value, IDWriteTextFormat* format, const RectF& rect, const D2D1_COLOR_F& color);
};

D2D1_COLOR_F Color(float r, float g, float b, float a = 1.0f);
D2D1_COLOR_F Mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float amount);
Theme CoffeeTheme();

class SliderControl {
public:
    void Configure(std::wstring label, float minValue, float maxValue, float value, std::wstring suffix);
    void SetRect(const RectF& rect);
    void SetValue(float value, bool notify);
    float Value() const;
    void SetOnChanged(std::function<void(float)> callback);

    void Update(float dt, float mouseX, float mouseY);
    void Draw(RenderContext& context) const;
    bool OnMouseDown(float x, float y);
    void OnMouseMove(float x, float y);
    void OnMouseUp();

private:
    void UpdateFromPosition(float x, bool notify);
    std::wstring ValueText() const;

    std::wstring label_;
    std::wstring suffix_;
    RectF rect_{};
    float minValue_ = 0.0f;
    float maxValue_ = 1.0f;
    float value_ = 0.0f;
    float hover_ = 0.0f;
    bool dragging_ = false;
    std::function<void(float)> onChanged_;
};

class ToggleControl {
public:
    void Configure(std::wstring label, bool enabled);
    void SetRect(const RectF& rect);
    void SetEnabled(bool enabled, bool notify);
    bool Enabled() const;
    void SetOnChanged(std::function<void(bool)> callback);

    void Update(float dt, float mouseX, float mouseY);
    void Draw(RenderContext& context) const;
    bool OnMouseDown(float x, float y);

private:
    std::wstring label_;
    RectF rect_{};
    bool enabled_ = false;
    float hover_ = 0.0f;
    float amount_ = 0.0f;
    std::function<void(bool)> onChanged_;
};

class DropdownControl {
public:
    void Configure(std::wstring label);
    void SetRect(const RectF& rect);
    void SetItems(std::vector<std::wstring> items, int selectedIndex);
    void SetSelectedIndex(int index, bool notify);
    int SelectedIndex() const;
    bool IsOpen() const;
    void Close();
    void SetOnChanged(std::function<void(int)> callback);

    void Update(float dt, float mouseX, float mouseY);
    void DrawBase(RenderContext& context) const;
    void DrawMenu(RenderContext& context) const;
    bool OnMouseDown(float x, float y);
    bool OnMouseWheel(short delta);

private:
    RectF MenuRect() const;
    int HitItem(float x, float y) const;

    std::wstring label_;
    std::vector<std::wstring> items_;
    RectF rect_{};
    int selectedIndex_ = -1;
    int scrollOffset_ = 0;
    float scrollPosition_ = 0.0f;
    bool open_ = false;
    float hover_ = 0.0f;
    float openAmount_ = 0.0f;
    std::function<void(int)> onChanged_;
};

}
