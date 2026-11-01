#pragma once

// Logical pixels at 1080p. The display scale remains shared with the rest of
// the HUD; these dimensions enlarge silhouettes without scaling slot keys.
namespace weapon_hud_layout
{
constexpr float gunWidth = 180.0f;
constexpr float grenadeWidth = 74.4f;
constexpr float iconHeight = 78.0f;
constexpr float gunGap = 20.0f;
constexpr float grenadeGap = 26.4f;
constexpr float gunRowHeight = 132.0f;
constexpr float grenadeRowHeight = 168.0f;
constexpr float maxRowWidth = 528.0f;
constexpr float quantityHeight = 32.0f;
constexpr float quantityOffset = 86.4f;
constexpr float nameHeight = 26.0f;
constexpr float gunNameOffset = 96.0f;
constexpr float grenadeNameOffset = 129.6f;
constexpr float slotHeight = 16.0f;
constexpr float slotOffset = 31.0f;
constexpr float cardSourceScale = 0.66f;
constexpr float cardWidth = 768.0f * cardSourceScale;
// OC2 card canvases are 768x268; preserve their actual aspect ratio.
constexpr float cardHeight = cardWidth * (268.0f / 768.0f);
// Anchor the visible settled cards, keeping the original deal animation intact.
constexpr float cardAlphaBottom[] = {196.0f, 199.0f, 202.0f, 209.0f, 215.0f};
// Match the reference: card width is about 56% of the visible emblem diameter,
// with a small gap between the numeral baseline and the emblem's upper rim.
constexpr float cardBottomMargin = 80.0f;
// Match the lower skull's overlap to group three as the cards fan out.
constexpr float cardVerticalOffset[] = {-7.25f, -4.0f, 0.0f, 10.5f, 16.5f};
inline float CardTop(int kills, float screenHeight, float scale)
{
    const int index = kills < 1 ? 0 : kills > 5 ? 4 : kills - 1;
    return screenHeight - (cardAlphaBottom[index] * cardSourceScale + cardBottomMargin
        - cardVerticalOffset[index]) * scale;
}
constexpr float cardCountHeight = 28.8f;
constexpr float cardCountOffset = 66.0f;
}
