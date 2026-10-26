#ifndef OC2_PLAYER_MODEL_POLICY_H
#define OC2_PLAYER_MODEL_POLICY_H

namespace oc2_player_model
{
// TeamInfo/ClCorpse use 1 for T and 2 for CT. VIP remains a CT game role.
inline const char *TeamModelPath(int team)
{
	// APK slot filenames terror/sas contain Leet/GSG9 meshes. The lobby uses
	// Phoenix's plaid variant and the gas-mask SAS; select their combat rigs.
	if (team == 1) return "models/player/phoenix_varb/phoenix_varb.mdl";
	if (team == 2) return "models/player/sas_vara/sas_vara.mdl";
	return nullptr;
}
}

#endif
