/*
JoinClass.cpp - Character class selection menu
Copyright (C) 2018 a1batross
Copyright (C) 2026 $_Vladislav

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "BaseMenu.h"
#include "TeamSelection.h"

// The accepted server class menu acknowledges jointeam. Do not enqueue a
// speculative class command while the server can still reject a team request.
// The only home-scene actors are Phoenix (T class 1) and SAS (CT class 3).
void UI_JoinClassT_Show(int, int)
{
    UI_JoinGame_Confirmed(1);
    UI_CloseClientMenu();
    EngFuncs::ClientCmd(false, "joinclass 1\n");
}

void UI_JoinClassCT_Show(int, int)
{
    UI_JoinGame_Confirmed(2);
    UI_CloseClientMenu();
    EngFuncs::ClientCmd(false, "joinclass 3\n");
}
