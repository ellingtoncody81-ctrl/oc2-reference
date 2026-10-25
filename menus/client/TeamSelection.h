#pragma once

// Called only after the server opens its accepted team's appearance menu.
// The two front-end actors map to Phoenix (T class 1) and SAS (CT class 3).
void UI_JoinGame_Confirmed(int team);

// Minimal frontend_dump snapshot with physical-pixel hit rectangles.
void UI_JoinGame_Dump();

