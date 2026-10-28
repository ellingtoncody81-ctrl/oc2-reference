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
int ItemStatusMessage(const char *name, int size, void *buffer);
}
