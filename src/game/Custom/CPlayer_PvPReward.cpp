#include "CPlayer.h"
#include "Custom.h"
#include "Custom/Custom_Constants.h"
#include "World/World.h"

#include <algorithm>
#include <numeric>
#include <ostream>

void CPlayer::HandlePvPKill()
{
    if (!sWorld.getConfig(CONFIG_UINT32_PVPREWARD_TYPE))
        return;

    // Get total damage dealt to victim
    float TotalDMG = std::accumulate(
        GetDamagers().begin(),
        GetDamagers().end(),
        0.f, // Start value
        [](const float prev, const DMGHEALMap::value_type& current)
        {
            return prev + current.second;
        }
    );

    for (DMGHEALMap::value_type& damager : GetDamagers())
    {
        CPlayer* attacker = sCustom.GetCPlayer(damager.first);
        if (!attacker)
            continue; // Attacker no longer ingame

        float attackReward = damager.second / TotalDMG;
        attacker->AddReward(GetNameLink(true), attackReward);

        // Get total healing done to attacker
        float TotalHeal = std::accumulate(
            attacker->GetHealers().begin(),
            attacker->GetHealers().end(),
            0.f, // Start value
            [](const float prev, const DMGHEALMap::value_type& current)
            {
                return prev + current.second;
            }
        );

        for (DMGHEALMap::value_type& healinfo : attacker->GetHealers())
        {
            CPlayer* healer = sCustom.GetCPlayer(healinfo.first);
            if (!healer)
                continue; // Healer no longer ingame

            float healReward = attackReward * (healinfo.second / TotalDMG) * (healinfo.second / attacker->GetMaxHealth());
            healer->AddReward(GetNameLink(true), healReward);
        }

        // Decrease everyones healing on an attacker once one target is killed
        // to allow new healers to gain some rewards too.
        std::for_each(
            attacker->GetHealers().begin(),
            attacker->GetHealers().end(),
            [](DMGHEALMap::value_type& current)
            {
                current.second *= 0.75;
            }
        );
    }

    GetDamagers().clear();
    GetHealers().clear();
}

void CPlayer::AddDamage(ObjectGuid guid, uint32 amount)
{
    if (sWorld.getConfig(CONFIG_UINT32_PVPREWARD_TYPE))
        m_Damagers[guid] += amount;
}

void CPlayer::AddHealing(ObjectGuid guid, uint32 amount)
{
    if (sWorld.getConfig(CONFIG_UINT32_PVPREWARD_TYPE))
        m_Healers[guid] += amount;
}

void CPlayer::AddReward(std::string name, float amount)
{
    uint32 rewardtype = sWorld.getConfig(CONFIG_UINT32_PVPREWARD_TYPE);
    if (!rewardtype)
        return;

    uint32 rewardperkill = sWorld.getConfig(CONFIG_UINT32_PVPREWARD_AMOUNT);

    m_rewards.push_back(name + "|r");
    m_PendingReward += amount;

    if (m_PendingReward >= 1)
    {
        switch (rewardtype)
        {
            case static_cast<uint32>(PvPRewardType::GOLD):
            {
                uint32 reward = m_PendingReward * 10000.f * rewardperkill;

                SetMoney(GetMoney() + reward);
                m_PendingReward = 0;
                BoxChat << "You were rewarded with " << GetGoldString(reward)
                        << " for the kills of " << GetRewardNames() << std::endl;
            }
            break;
            case static_cast<uint32>(PvPRewardType::ARENAPOINTS):
            {
                uint32 reward = m_PendingReward * rewardperkill;

                SetArenaPoints(GetArenaPoints() + reward);
                m_PendingReward -= 1.f;
                BoxChat << "You were rewarded with " << reward << "arena points"
                        << " for the kills of " << GetRewardNames() << std::endl;
            }
            break;
            case static_cast<uint32>(PvPRewardType::HONOR):
            {
                uint32 reward = m_PendingReward * rewardperkill;

                SetHonorPoints(GetHonorPoints() + reward);
                m_PendingReward -= 1.f;
                BoxChat << "You were rewarded with " << reward << " honor points"
                        << " for the kills of " << GetRewardNames() << std::endl;
            }
            break;
            case static_cast<uint32>(PvPRewardType::NONE):
                break;
            // Default means rewardtype is an item ID.
            default:
            {
                auto item = sObjectMgr.GetItemPrototype(rewardtype);
                if (!item)
                    return;

                uint32 reward = m_PendingReward * rewardperkill;

                if (StoreNewItemInBestSlots(rewardtype, reward))
                {
                    m_PendingReward -= 1.f;
                    BoxChat << "You were rewarded with " << reward << " " << item->Name1
                            << " for the kills of " << GetRewardNames() << std::endl;
                }
            }
        }

        m_rewards.clear();
    }
}

std::string CPlayer::GetRewardNames(bool duplicates)
{
    auto Names = m_rewards;

    // Remove duplicates if wanted by caller
    if (!duplicates)
        Names.erase(std::unique(Names.begin(), Names.end()), Names.end());

    std::ostringstream ss;
    for (auto name = Names.begin(); name != Names.end(); ++name)
    {
        auto nextname = name;
        // Get next name in this loop already.
        std::advance(nextname, 1);

        // If next name is last, add "and", else add ",".
        if (nextname == Names.end())
            ss << " and ";
        else if (name != Names.begin())
            ss << ", ";

        ss << *name;
    }

    return ss.str();
}

std::string CPlayer::GetGoldString(uint32 copper)
{
    std::ostringstream ss;

    int32 gold = copper / 10000;
    copper -= gold * 10000;
    int32 silver = copper / 100;
    copper -= silver * 100;

    // If there's more than one gold display gold
    if (gold)
        ss << gold << " Gold";

    // If the value is roundable to gold and silver only, display
    // "and" rather than "," for proper scentence building
    if (gold && silver && !copper)
        ss << " and ";
    else if (gold && silver)
        ss << ", ";

    // Display silver amount
    if (silver)
        ss << silver << " Silver";
    // Display copper amount
    if (copper)
        ss << " and " << copper << " Copper";

    return ss.str();
}
