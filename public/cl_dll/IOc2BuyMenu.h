#pragma once
#include "interface.h"

// Optional client/menu interface. Keep the existing GameClientExports ABI intact.
namespace oc2_buy
{
struct State
{
	int team = 0;
	int money = 0;
	bool alive = false;
	bool inBuyZone = false;
	bool canBuy = false;
	// -1 means unlimited, -2 means the remote server did not provide a clock.
	int seconds = -2;
	const char *reason = "";
};

struct Item
{
	int id = 0;
	int column = 0; // equipment, pistols, mid-tier, rifles, grenades
	const char *name = "";
	const char *image = ""; // APK card resource; renderer replaces baked labels/prices
	int price = 0;
	int count = 0;
	int limit = 0;
	bool visible = true; // hide the other team's catalogue without changing stable IDs
	bool owned = false;
	bool enabled = false;
	const char *reason = "";
};
}

class IOc2BuyMenu : public IBaseInterface
{
public:
	virtual bool GetState(oc2_buy::State &state) = 0;
	virtual int ItemCount() = 0;
	virtual bool GetItem(int index, oc2_buy::Item &item) = 0;
	virtual bool Purchase(int index) = 0;
	virtual void SetOpen(bool open) = 0;
};

#define OC2_BUYMENU_INTERFACE_VERSION "OC2BuyMenu001"
