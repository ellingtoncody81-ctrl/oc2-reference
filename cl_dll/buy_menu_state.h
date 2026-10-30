#pragma once

namespace buy_menu_state
{
void Init();
void ClearMap();
void Think();
bool IsOpen();
void Close();
int Message(const char *name, int size, void *buffer);
int EquipmentMessage(const char *name, int size, void *buffer);
int FireVariantMessage(const char *name, int size, void *buffer);
bool FireIsIncendiary(); // Authoritative carried item; never inferred from the player's team.
bool HasDefuser();
// ItemStatus bit 2 identifies the server's hostage rescue equipment mode.
bool HostageRescueKitMode();
int ItemStatusMessage(const char *name, int size, void *buffer);
}
