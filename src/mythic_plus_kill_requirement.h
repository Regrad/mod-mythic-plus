// mythic_plus_kill_requirement.h
#ifndef MYTHIC_PLUS_KILL_REQUIREMENT_H
#define MYTHIC_PLUS_KILL_REQUIREMENT_H

#include "Define.h"
#include <unordered_map>

class Creature;
class Unit;
class Map;

// Простая система трекинга: для каждой инсты храним общее кол-во трэш-мобов и количество убитых.
class MythicPlusKillRequirement
{
public:
    // Регистрируем моба как «подходящего» для килл-требования.
    void RegisterEligibleMob(Creature* creature);

    // Вызывается при смерти моба.
    void OnMobKilled(Unit* killer, Creature* creature);

    // Чистим данные конкретной инсты (по уничтожению инстанса).
    void ResetForInstance(Map* map);

    // Текущий процент убитых мобов в инсте (0–100).
    float GetKilledPercent(Map* map) const;

    // Проверка, что убитый процент >= требуемого.
    bool HasRequiredPercent(Map* map, float requiredPercent) const;

private:
    struct Counters
    {
        // total / killed — это ТОЛЬКО элитный трэш
        uint32 total = 0;   // всего элитных мобов
        uint32 killed = 0;   // убито элитных мобов

        // Сколько боссов уже убито (каждый даёт +10%)
        uint32 bossesKilled = 0;

        // Флаг: требуемый процент уже был достигнут хотя бы раз
        bool   completed = false;

        // Максимальный когда-либо достигнутый общий процент (трэш + боссы)
        float  maxPercent = 0.0f;

        // Последний показанный игрокам "оставшийся процент"
        int32  lastRemaining = -1; // -1 = ещё ничего не показывали
    };

    Counters* GetOrCreateCounters(Map* map);
    Counters const* GetCounters(Map* map) const;
    bool            IsEligibleMob(Creature* creature) const;

    // Ключ – instanceId.
    std::unordered_map<uint32, Counters> _instanceCounters;
};

// Глобальный указатель, чтобы было удобно дергать из других файлов.
extern MythicPlusKillRequirement* sMythicPlusKillRequirement;

// Возвращает требуемый процент убийств из конфига (по умолчанию 70%).
float GetMythicPlusRequiredKillPercent();
float GetMythicPlusBossKillPercent();

#endif // MYTHIC_PLUS_KILL_REQUIREMENT_H
