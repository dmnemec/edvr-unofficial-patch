#pragma once

#include "mfd_types.h"
#include "mfd_view_model.h"
#include <vector>
#include <cstdint>

namespace edvr::mfd {

// Rasterizes MfdViewModel into an RGBA8 bitmap buffer matching
// Elite Dangerous's native cockpit HUD aesthetics.
class MfdRenderer {
public:
    MfdRenderer(int width = 512, int height = 384);

    int width() const { return m_width; }
    int height() const { return m_height; }
    const uint32_t* pixelData() const { return m_pixels.data(); }
    size_t pixelByteSize() const { return m_pixels.size() * sizeof(uint32_t); }

    void resize(int width, int height);

    // Render the complete view model onto the internal pixel buffer.
    void render(const MfdViewModel& model);

    // Drawing primitives:
    void clear(MfdColor color);
    void drawPixel(int x, int y, MfdColor color);
    void drawRect(int x, int y, int w, int h, MfdColor color);
    void drawRectFilled(int x, int y, int w, int h, MfdColor color);
    void drawLine(int x0, int y0, int x1, int y1, MfdColor color);
    void drawScanlines(MfdColor color, int step = 4);
    void drawChar(int x, int y, char c, MfdColor color, int scale = 1);
    void drawText(int x, int y, const std::string& text, MfdColor color, int scale = 1);
    void drawTextRight(int rightX, int y, const std::string& text, MfdColor color, int scale = 1);

private:
    int m_width;
    int m_height;
    std::vector<uint32_t> m_pixels; // RGBA8 packed

    void renderHeader(const MfdViewModel& model, MfdColor mainColor, MfdColor dimColor);
    void renderTabs(const MfdViewModel& model, MfdColor mainColor, MfdColor dimColor);
    void renderListTab(const MfdTab& tab, bool focused, MfdColor mainColor, MfdColor dimColor);
    void renderKeyValueTab(const MfdTab& tab, MfdColor mainColor, MfdColor dimColor);
    void renderTextTab(const MfdTab& tab, MfdColor mainColor, MfdColor dimColor);
    void renderFooter(const MfdViewModel& model, MfdColor dimColor);
};

} // namespace edvr::mfd
