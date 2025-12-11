#include "mythic_plus_kill_requirement.h"

#include "Creature.h"
#include "CreatureData.h"
#include "Player.h"
#include "Map.h"
#include "Group.h"
#include "Chat.h"
#include "Config.h"
#include "mythic_plus.h"

#include <cmath>

// Глобальный объект и указатель.
static MythicPlusKillRequirement g_MythicPlusKillRequirement;
MythicPlusKillRequirement* sMythicPlusKillRequirement = &g_MythicPlusKillRequirement;

// Возвращает требуемый процент убийств для Mythic+ из конфига.
float GetMythicPlusRequiredKillPercent()
{
    return float(sConfigMgr->GetOption<uint32>("MythicPlus.RequiredKillPercent", 70u));
}

float GetMythicPlusBossKillPercent()
{
    // Сколько процентов даёт один убитый босс. По умолчанию 10.
    // Можно задавать как целое число.
    return float(sConfigMgr->GetOption<uint32>("MythicPlus.BossKillPercent", 10u));
}

MythicPlusKillRequirement::Counters* MythicPlusKillRequirement::GetOrCreateCounters(Map* map)
{
    if (!map)
        return nullptr;

    uint32 instanceId = map->GetInstanceId();
    auto it = _instanceCounters.find(instanceId);
    if (it == _instanceCounters.end())
    {
        Counters counters;
        counters.total = 0;
        counters.killed = 0;
        counters.bossesKilled = 0;
        counters.completed = false;
        counters.maxPercent = 0.0f;
        counters.lastRemaining = -1;
        it = _instanceCounters.emplace(instanceId, counters).first;
    }

    return &it->second;
}

MythicPlusKillRequirement::Counters const* MythicPlusKillRequirement::GetCounters(Map* map) const
{
    if (!map)
        return nullptr;

    uint32 instanceId = map->GetInstanceId();
    auto it = _instanceCounters.find(instanceId);
    if (it == _instanceCounters.end())
        return nullptr;

    return &it->second;
}

bool MythicPlusKillRequirement::IsEligibleMob(Creature* creature) const
{
    if (!creature)
        return false;

    Map* map = creature->GetMap();
    if (!map)
        return false;

    // Только инстансовые подземелья.
    if (!map->IsNonRaidDungeon() || !map->ToInstanceMap())
        return false;

    // Используем те же фильтры, что и система MythicPlus.
    if (!sMythicPlus->CanProcessCreature(creature))
        return false;

    // Не считаем временных призванных существ (адды от боссов и т.п.)
    if (creature->IsSummon())
        return false;

    // Считаем только ЭЛИТНЫХ мобов (Elite / Rare Elite)
    if (!creature->isElite())
        return false;

    // Не считаем боссов / финального босса — они идут по отдельной логике +10%
    if (creature->IsDungeonBoss() || sMythicPlus->IsFinalBoss(creature->GetEntry()))
        return false;

    return true;
}

void MythicPlusKillRequirement::RegisterEligibleMob(Creature* creature)
{
    if (!creature)
        return;

    Map* map = creature->GetMap();
    if (!map)
        return;

    if (!IsEligibleMob(creature))
        return;

    Counters* counters = GetOrCreateCounters(map);
    if (!counters)
        return;

    ++counters->total; // теперь это "total элитного трэша"
}

void MythicPlusKillRequirement::OnMobKilled(Player* killer, Creature* creature)
{
    if (!killer || !creature)
        return;

    Map* map = creature->GetMap();
    if (!map)
        return;

    // Только если карта в режиме Mythic+
    if (!sMythicPlus->IsMapInMythicPlus(map))
        return;

    Counters* counters = GetOrCreateCounters(map);
    if (!counters)
        return;

    float requiredPercent = GetMythicPlusRequiredKillPercent();
    bool wasCompleted = counters->completed;

    // Определяем: это босс или элитный трэш?
    bool isBoss = creature->IsDungeonBoss() || sMythicPlus->IsFinalBoss(creature->GetEntry());

    if (isBoss)
    {
        // Каждый босс даёт +10% к прогрессу
        ++counters->bossesKilled;
    }
    else
    {
        // Для трэша — только элитные мобы считаются
        if (!IsEligibleMob(creature))
            return;

        if (counters->total == 0)
            return;

        if (counters->killed < counters->total)
            ++counters->killed;
    }

    // --- пересчёт процентов ---

    // Процент по ЭЛИТНОМУ трэшу (0–100)
    float trashPercent = 0.0f;
    if (counters->total > 0)
    {
        trashPercent = 100.0f * float(counters->killed) / float(counters->total);
        if (trashPercent < 0.0f)
            trashPercent = 0.0f;
        if (trashPercent > 100.0f)
            trashPercent = 100.0f;
    }

    // Процент от боссов: каждый босс даёт BossKillPercent из конфига
    float bossStep = GetMythicPlusBossKillPercent();
    float bossPercent = float(counters->bossesKilled) * bossStep;
    if (bossPercent > 100.0f)
        bossPercent = 100.0f;

    // Сырой суммарный прогресс (трэш + боссы)
    float rawTotalPercent = trashPercent + bossPercent;
    if (rawTotalPercent > 100.0f)
        rawTotalPercent = 100.0f;

    // Монотонность: используем только максимум за всё время
    if (rawTotalPercent > counters->maxPercent)
        counters->maxPercent = rawTotalPercent;

    float killedPercent = counters->maxPercent;

    // Фиксируем момент, когда требование выполнено
    if (!counters->completed && killedPercent >= requiredPercent)
        counters->completed = true;

    // --- формируем сообщение ---

    std::ostringstream oss;

    if (!counters->completed)
    {
        float remaining = requiredPercent - killedPercent;
        if (remaining < 0.0f)
            remaining = 0.0f;

        uint32 remainingRounded = uint32(std::round(remaining));

        // Не даём "оставшемуся проценту" расти: максимум монотонно уменьшается
        if (counters->lastRemaining >= 0 &&
            remainingRounded > uint32(counters->lastRemaining))
        {
            remainingRounded = uint32(counters->lastRemaining);
        }

        counters->lastRemaining = int32(remainingRounded);

        if (remainingRounded > 0)
        {
            oss << "Осталось убить примерно " << remainingRounded
                << "% элитных монстров (и боссов) для получения Mythic Plus награды.";
        }
        else
        {
            oss << "Требуемый процент убийств элитных монстров для Mythic Plus награды уже выполнен.";
        }
    }
    else if (!wasCompleted)
    {
        // Только один раз, в момент первого достижения требуемого процента
        oss << "Требуемый процент убийств элитных монстров для Mythic Plus награды уже выполнен.";
        counters->lastRemaining = 0;
    }
    else
    {
        // Уже всё выполнено и сообщение было – не спамим дальше.
        return;
    }

    MythicPlus::BroadcastToMap(map, oss.str());
}

void MythicPlusKillRequirement::ResetForInstance(Map* map)
{
    if (!map)
        return;

    _instanceCounters.erase(map->GetInstanceId());
}

float MythicPlusKillRequirement::GetKilledPercent(Map* map) const
{
    Counters const* counters = GetCounters(map);
    if (!counters)
        return 0.0f;

    float pct = counters->maxPercent;
    if (pct < 0.0f)
        pct = 0.0f;
    if (pct > 100.0f)
        pct = 100.0f;

    return pct;
}

bool MythicPlusKillRequirement::HasRequiredPercent(Map* map, float requiredPercent) const
{
    Counters const* counters = GetCounters(map);
    if (!counters)
        return false;

    // Если требование уже было выполнено однажды, больше его не сбрасываем
    // даже если позже появятся новые мобы и общий % формально упадёт.
    if (counters->completed)
        return true;

    return GetKilledPercent(map) >= requiredPercent;
}
