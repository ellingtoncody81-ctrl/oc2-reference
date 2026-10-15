#pragma once

namespace buy_menu_state
{
void Init();
void ClearMap();
void Think();
bool IsOpen();
void Close();
int Message(const char *name, int size, void *buffer);
int ItemStatusMessage(const char *name, int size, void *buffer);
}
