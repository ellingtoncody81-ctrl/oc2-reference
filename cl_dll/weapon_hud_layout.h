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
constexpr float cardWidth = 468.0f;
constexpr float cardHeight = 180.0f;
constexpr float cardBottomMargin = 45.0f;
constexpr float cardCountHeight = 28.8f;
constexpr float cardCountOffset = 66.0f;
}
