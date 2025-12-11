/*
 * Credits: silviu20092
 */

#include "ScriptMgr.h"
#include "mythic_plus.h"
#include "mythic_plus_kill_requirement.h" // NEW

class mythic_plus_all_creaturescript : public AllCreatureScript
{
public:
    mythic_plus_all_creaturescript() : AllCreatureScript("mythic_plus_all_creaturescript") {}

    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        Map* map = creature->GetMap();
        if (sMythicPlus->CanMapBeMythicPlus(map))
        {
            MythicPlus::CreatureData* creatureData = sMythicPlus->GetCreatureData(creature);
            if (creatureData->processed)
            {
                // if the creature was processed and is a clone, mark it visually
                if (creatureData->copy)
                {
                    if (!creature->HasAura(69105))
                        creature->CastSpell(creature, 69105, true);
                }
                return;
            }

            if (!sMythicPlus->CanProcessCreature(creature))
                return;

            MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map);
            if (mapData->mythicLevel)
            {
                sMythicPlus->ScaleCreature(creature);
                sMythicPlus->StoreOriginalCreatureData(creature);

                creatureData->processed = true;
                sMythicPlus->ProcessStaticAffixes(mapData->mythicLevel, creature);

                // NEW: регистрируем трэш-моба в системе килл-требования
                if (sMythicPlusKillRequirement)
                    sMythicPlusKillRequirement->RegisterEligibleMob(creature);
            }
        }
    }
};

void AddSC_mythic_plus_all_creaturescript()
{
    new mythic_plus_all_creaturescript();
}
