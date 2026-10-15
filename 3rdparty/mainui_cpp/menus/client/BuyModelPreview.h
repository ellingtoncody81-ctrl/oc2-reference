#pragma once

// CS1.6 team: 1=T, 2=CT. The viewport uses physical screen pixels.
// Returns false when this preview cannot be drawn, so the menu can fall back.
bool OC2_DrawBuyPreview(int team, int weaponId, int x, int y, int width, int height);
