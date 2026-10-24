#pragma once
#include <cstring>

// Definitions follow the installed inventory catalog; resource basenames follow
// the APK. In particular, its Huntsman panel filename contains a transposition.
namespace oc2_knife_icons
{
struct Entry
{
	int definition;
	const char *name, *panelToken, *deathToken;
};
static const Entry entries[] = {
	{0, "Default T", "knife_t", "knife_t"},
	{42, "Default CT", "knife", "knife"},
	{500, "Bayonet", "bayonet", "bayonet"},
	{503, "Classic Knife", "css", "css"},
	{505, "Flip Knife", "flip", "flip"},
	{506, "Gut Knife", "gut", "gut"},
	{507, "Karambit", "karambit", "karambit"},
	{508, "M9 Bayonet", "m9", "m9"},
	{509, "Huntsman Knife", "hunstman", "huntsman"},
	{512, "Falchion Knife", "falchion", "falchion"},
	{514, "Bowie Knife", "bowie", "bowie"},
	{515, "Butterfly Knife", "butterfly", "butterfly"},
	{516, "Shadow Daggers", "daggers", "daggers"},
	{517, "Paracord Knife", "paracord", "paracord"},
	{518, "Survival Knife", "survival", "survival"},
	{519, "Ursus Knife", "ursus", "ursus"},
	{520, "Navaja Knife", "navaja", "navaja"},
	{521, "Nomad Knife", "nomad", "nomad"},
	{522, "Stiletto Knife", "stiletto", "stiletto"},
	{523, "Talon Knife", "talon", "talon"},
	{525, "Skeleton Knife", "skeleton", "skeleton"},
};

inline const Entry *FindDefinition(int definition)
{
	for (const auto &entry : entries)
		if (entry.definition == definition) return &entry;
	return nullptr;
}

inline const Entry *FindDeathToken(const char *token)
{
	if (token)
		for (const auto &entry : entries)
			if (!std::strcmp(entry.deathToken, token)) return &entry;
	return nullptr;
}
}
