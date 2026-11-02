#pragma once

// Xash keeps entity slots after they leave the PVS. A non-null pointer is
// therefore not evidence of a position received in the current packet.
inline bool OC2_CurrentPlayerSnapshot(const cl_entity_t *entity)
{
    const cl_entity_t *local=gEngfuncs.GetLocalPlayer();
    if(!entity||!entity->player||!local||!IEngineStudio.GetPlayerState||
       entity->index<1||entity->index>gEngfuncs.GetMaxClients()||
       local->index<1||local->index>gEngfuncs.GetMaxClients())return false;
    const entity_state_t *current=IEngineStudio.GetPlayerState(entity->index-1);
    const entity_state_t *localState=IEngineStudio.GetPlayerState(local->index-1);
    return current&&localState&&localState->messagenum>0&&
        current->number==entity->index&&current->messagenum==localState->messagenum&&
        entity->curstate.messagenum==current->messagenum;
}
