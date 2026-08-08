/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include <algorithm>
#include <set>
#include <vector>

#include "DBCEnums.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "GameEventMgr.h"
#include "Group.h"
#include "LFGMgr.h"
#include "LFGStatePolicy.h"
#include "Object.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"

INSTANTIATE_SINGLETON_1(LFGMgr);

LFGMgr::LFGMgr()
{
    m_proposalId = 0;
    // Starts at a non-zero base: retail never sends ticketId 0.
    m_nextTicketId = 1000;
}

LFGMgr::~LFGMgr()
{
    m_dailyAny.clear();
    m_dailyTBCHeroic.clear();
    m_dailyLKNormal.clear();
    m_dailyLKHeroic.clear();

    m_playerData.clear();
    m_queueSet.clear();

    m_playerStatusMap.clear();
    m_groupStatusMap.clear();
    m_groupSet.clear();
    m_proposalMap.clear();

    m_roleCheckMap.clear();

    m_bootStatusMap.clear();

    m_tankWaitTime.clear();
    m_healerWaitTime.clear();
    m_dpsWaitTime.clear();
    m_avgWaitTime.clear();
}

void LFGMgr::Update()
{
    //todo: remove old queues

    // remove old role checks
    RemoveOldRoleChecks();

    // and proposals nobody answered
    RemoveOldProposals();

    // and boot votes that ran out of time
    RemoveOldBoots();

    // go through a waitTimeMap::iterator for each wait map and update times based on player count
    for (waitTimeMap::iterator tankItr = m_tankWaitTime.begin(); tankItr != m_tankWaitTime.end(); ++tankItr)
    {
        LFGWait waitInfo = tankItr->second;
        if (waitInfo.doAverage)
        {
            int32 lastTime = waitInfo.previousTime;
            int32 thisTime = waitInfo.time;

            // average of the two join times
            waitInfo.time = (thisTime + lastTime) / 2;

            // now set what was just the current wait time to the previous time for a later calculation
            waitInfo.previousTime = thisTime;
            waitInfo.doAverage = false;

            tankItr->second = waitInfo;
        }
    }
    for (waitTimeMap::iterator healItr = m_healerWaitTime.begin(); healItr != m_healerWaitTime.end(); ++healItr)
    {
        LFGWait waitInfo = healItr->second;
        if (waitInfo.doAverage)
        {
            int32 lastTime = waitInfo.previousTime;
            int32 thisTime = waitInfo.time;

            // average of the two join times
            waitInfo.time = (thisTime + lastTime) / 2;

            // now set what was just the current wait time to the previous time for a later calculation
            waitInfo.previousTime = thisTime;
            waitInfo.doAverage = false;

            healItr->second = waitInfo;
        }
    }
    for (waitTimeMap::iterator dpsItr = m_dpsWaitTime.begin(); dpsItr != m_dpsWaitTime.end(); ++dpsItr)
    {
        LFGWait waitInfo = dpsItr->second;
        if (waitInfo.doAverage)
        {
            int32 lastTime = waitInfo.previousTime;
            int32 thisTime = waitInfo.time;

            // average of the two join times
            waitInfo.time = (thisTime + lastTime) / 2;

            // now set what was just the current wait time to the previous time for a later calculation
            waitInfo.previousTime = thisTime;
            waitInfo.doAverage = false;

            dpsItr->second = waitInfo;
        }
    }
    for (waitTimeMap::iterator avgItr = m_avgWaitTime.begin(); avgItr != m_avgWaitTime.end(); ++avgItr)
    {
        LFGWait waitInfo = avgItr->second;
        if (waitInfo.doAverage)
        {
            int32 lastTime = waitInfo.previousTime;
            int32 thisTime = waitInfo.time;

            // average of the two join times
            waitInfo.time = (thisTime + lastTime) / 2;

            // now set what was just the current wait time to the previous time for a later calculation
            waitInfo.previousTime = thisTime;
            waitInfo.doAverage = false;

            avgItr->second = waitInfo;
        }
    }

    // Queue System
    FindQueueMatches();
    SendQueueStatus();
}


ItemRewards LFGMgr::GetDungeonItemRewards(uint32 dungeonId, DungeonTypes type)
{
    ItemRewards rewards;
    LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(dungeonId);
    if (dungeon)
    {
        uint32 minLevel = dungeon->MinLevel;
        uint32 maxLevel = dungeon->MaxLevel;
        uint32 avgLevel = (minLevel+maxLevel)/2; // otherwise there are issues

        DungeonFinderItemsMap const& itemBuffer = sObjectMgr.GetDungeonFinderItemsMap();
        for (DungeonFinderItemsMap::const_iterator it = itemBuffer.begin(); it != itemBuffer.end(); ++it)
        {
            DungeonFinderItems itemCache = it->second;
            if (itemCache.dungeonType == type)
            {
                // should only be one of this inequality in the map
                if ((avgLevel >= itemCache.minLevel) && (avgLevel <= itemCache.maxLevel))
                {
                    rewards.itemId = itemCache.itemReward;
                    rewards.itemAmount = itemCache.itemAmount;
                    return rewards;
                }
            }
        }
    }
    return rewards;
}

DungeonTypes LFGMgr::GetDungeonType(uint32 dungeonId)
{
    LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(dungeonId);
    if (dungeon)
    {
        // DungeonTypes classifies FIVE-MAN dungeons for daily-reward purposes -- see
        // RegisterPlayerDaily and the reward selection in LFGMgrProposal -- and has no raid member
        // at all. A raid row must therefore not be classified here.
        //
        // That needs saying because translating the difficulty made this newly wrong. Raid rows
        // carry raw DifficultyID 3 (10-normal) and 4 (25-normal), which translate to internal 0 and
        // 1, exactly the values the tests below look for. So every TBC and WotLK raid would come out
        // of here typed DUNGEON_TBC / DUNGEON_WOTLK or their heroic variants -- a 25-man raid counted
        // as a normal 5-man dungeon for daily rewards. Before the translation, raw 3 and 4 matched
        // neither constant and fell out as DUNGEON_UNKNOWN by accident.
        if (dungeon->TypeID == LFG_TYPE_RAID)
        {
            return DUNGEON_UNKNOWN;
        }

        // LfgDungeons.dbc carries a RAW client DifficultyID; the DUNGEON_DIFFICULTY_* constants
        // are internal modes. Comparing them directly made raw 1 (5-man normal) equal
        // DUNGEON_DIFFICULTY_HEROIC, which is 1, while raw 2 (5-man heroic) matched neither
        // constant and fell through to DUNGEON_UNKNOWN -- so every TBC and WotLK dungeon was
        // typed as heroic or as unknown, never as normal.
        int32 const mode = ToInternalDifficulty(dungeon->DifficultyID);

        switch (dungeon->ExpansionLevel)
        {
            case 0:
                return DUNGEON_CLASSIC;
            case 1:
            {
                if (mode == int32(DUNGEON_DIFFICULTY_NORMAL))
                {
                    return DUNGEON_TBC;
                }
                else if (mode == int32(DUNGEON_DIFFICULTY_HEROIC))
                {
                    return DUNGEON_TBC_HEROIC;
                }
                return DUNGEON_UNKNOWN;                     // was a fall-through into case 2
            }
            case 2:
            {
                if (mode == int32(DUNGEON_DIFFICULTY_NORMAL))
                {
                    return DUNGEON_WOTLK;
                }
                else if (mode == int32(DUNGEON_DIFFICULTY_HEROIC))
                {
                    return DUNGEON_WOTLK_HEROIC;
                }
                return DUNGEON_UNKNOWN;                     // was a fall-through into default
            }
            default:
                return DUNGEON_UNKNOWN;
        }
    }
    return DUNGEON_UNKNOWN;
}

void LFGMgr::RegisterPlayerDaily(uint32 guidLow, DungeonTypes dungeon)
{
    switch (dungeon)
    {
        case DUNGEON_CLASSIC:
        case DUNGEON_TBC:
            m_dailyAny.insert(guidLow);
            break;
        case DUNGEON_TBC_HEROIC:
            m_dailyTBCHeroic.insert(guidLow);
            break;
        case DUNGEON_WOTLK:
            m_dailyLKNormal.insert(guidLow);
            break;
        case DUNGEON_WOTLK_HEROIC:
            m_dailyLKHeroic.insert(guidLow);
            break;
        default:
            break;
    }
}

bool LFGMgr::HasPlayerDoneDaily(uint32 guidLow, DungeonTypes dungeon)
{
    switch (dungeon)
    {
        case DUNGEON_CLASSIC:
        case DUNGEON_TBC:
            return (m_dailyAny.find(guidLow) != m_dailyAny.end()) ? true : false;
        case DUNGEON_TBC_HEROIC:
            return (m_dailyTBCHeroic.find(guidLow) != m_dailyTBCHeroic.end()) ? true : false;
        case DUNGEON_WOTLK:
            return (m_dailyLKNormal.find(guidLow) != m_dailyLKNormal.end()) ? true : false;
        case DUNGEON_WOTLK_HEROIC:
            return (m_dailyLKHeroic.find(guidLow) != m_dailyLKHeroic.end()) ? true : false;
        default:
            return false;
    }
    return false;
}

void LFGMgr::ResetDailyRecords()
{
    m_dailyAny.clear();
    m_dailyTBCHeroic.clear();
    m_dailyLKNormal.clear();
    m_dailyLKHeroic.clear();
}

bool LFGMgr::IsSeasonActive(uint32 dungeonId)
{
    switch (dungeonId)
    {
        case 285:
            return IsHolidayActive(HOLIDAY_HALLOWS_END);
        case 286:
            return IsHolidayActive(HOLIDAY_FIRE_FESTIVAL);
        case 287:
            return IsHolidayActive(HOLIDAY_BREWFEST);
        case 288:
            return IsHolidayActive(HOLIDAY_LOVE_IS_IN_THE_AIR);
        default:
            return false;
    }
    return false;
}

dungeonEntries LFGMgr::FindRandomDungeonsForPlayer(uint32 level, uint8 expansion)
{
    dungeonEntries randomDungeons;

    // go through the dungeon dbc and select the applicable dungeons
    for (uint32 id = 0; id < sLfgDungeonsStore.GetNumRows(); ++id)
    {
        LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(id);
        if (dungeon)
        {
            if ( (dungeon->TypeID == LFG_TYPE_RANDOM_DUNGEON)
                || (IsSeasonal(dungeon->Flags) && IsSeasonActive(dungeon->ID)) )
                if ((uint8)dungeon->ExpansionLevel <= expansion && dungeon->MinLevel <= level
                    && dungeon->MaxLevel >= level)
                    randomDungeons[dungeon->ID] = dungeon->Entry();
        }
    }
    return randomDungeons;
}

/**
 * @brief The `dungeonfinder_requirements` row for an LfgDungeons entry, or NULL.
 *
 * This is the third DBC-to-world-table join that crosses the difficulty key spaces, and it
 * has to translate for the same reason the other two do.
 *
 * LfgDungeons.dbc carries a RAW client DifficultyID; `dungeonfinder_requirements`.`difficulty`
 * is an INTERNAL 0-based mode. Confirmed against the shipped world data rather than assumed:
 * every five-man map with two rows carries 0 and 1 -- Opening of the Dark Portal 269, The Forge
 * of Souls 632, Trial of the Champion 650, Pit of Saron 658 and Halls of Reflection 668 -- and
 * Icecrown Citadel 631 carries 2 and 3. In the raw space those would be 1/2 and 5/6.
 *
 * 269 is the clearest exhibit of the five and was missing from an earlier version of this list:
 * its difficulty-1 row is the Black Morass heroic attunement, so a table keyed on raw ids would
 * have had to carry it at 2.
 *
 * Passing the raw id shifted every lookup by one tier, in both directions:
 *
 *   an LFG NORMAL row (raw 1) fetched the (map, 1) row, which is the HEROIC requirement, so a
 *   player was held to the heroic item level and attunement quest to queue for normal content.
 *   Item level and quests specifically: no five-man row in the table carries an achievement --
 *   the only two that do are Icecrown Citadel's (631, 2) and (631, 3), a raid map that never
 *   reaches this lookup because LFG_FORBIDDEN_RAID short-circuits above. The live five-man
 *   gates are min_item_level 180 on 12 rows, 200 on 6 and 219 on 2;
 *   an LFG HEROIC row (raw 2) asked for (map, 2), which for a five-man is CHALLENGE and has no
 *   row at all, so the real heroic requirement was skipped entirely.
 *
 * Too strict where it should be lenient and absent where it should bite.
 *
 * It is PRE-EXISTING, not something this branch caused. An earlier version of this comment
 * claimed the row-ordinal indexing had made the lookup miss on noise and that correcting the
 * index turned it systematic. That is false, and measurably so: this function enumerates
 * 0..GetNumRows()-1 and reads MapID, DifficultyID and TypeID off ONE row pointer, so with 343
 * rows and no duplicate ids both index modes visit every row exactly once and the same 33 rows
 * reach the requirements table with the same wrong tier key either way. The claim was carried
 * over from a genuinely index-sensitive site -- CreateDungeonGroup, which looks a row up by a
 * client-supplied id -- where it does hold.
 *
 * It is fixed here because this is the branch that separates the two key spaces, not because
 * the branch created it.
 *
 * A row whose tier has no internal mode yields NULL, which reads as "no requirement". That is
 * the pre-existing behaviour for a missing row and is safe here: JoinLFG refuses such a slot
 * outright at admission, so nothing untranslatable reaches a queue on the strength of it.
 */
static DungeonFinderRequirements const* GetDungeonFinderRequirementsFor(LfgDungeonsEntry const* dungeon)
{
    int32 const mode = ToInternalDifficulty(dungeon->DifficultyID);
    if (mode < 0)
    {
        return NULL;
    }

    return sObjectMgr.GetDungeonFinderRequirements(uint32(dungeon->MapID), uint32(mode));
}

dungeonForbidden LFGMgr::FindRandomDungeonsNotForPlayer(Player* plr)
{
    uint32 level = plr->getLevel();
    uint8 expansion = plr->GetSession()->Expansion();

    dungeonForbidden randomDungeons;

    for (uint32 id = 0; id < sLfgDungeonsStore.GetNumRows(); ++id)
    {
        LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(id);
        if (dungeon)
        {
            uint32 forbiddenReason = 0;

            if ((uint8)dungeon->ExpansionLevel > expansion)
            {
                forbiddenReason = (uint32)LFG_FORBIDDEN_EXPANSION;
            }
            else if (dungeon->TypeID == LFG_TYPE_RAID)
            {
                forbiddenReason = (uint32)LFG_FORBIDDEN_RAID;
            }
            else if (dungeon->MinLevel > level)
            {
                forbiddenReason = (uint32)LFG_FORBIDDEN_LOW_LEVEL;
            }
            else if (dungeon->MaxLevel < level)
            {
                forbiddenReason = (uint32)LFG_FORBIDDEN_HIGH_LEVEL;
            }
            else if (IsSeasonal(dungeon->Flags) && !IsSeasonActive(dungeon->ID)) // check pointers/function args
            {
                forbiddenReason = (uint32)LFG_FORBIDDEN_NOT_IN_SEASON;
            }
            else if (DungeonFinderRequirements const* req = GetDungeonFinderRequirementsFor(dungeon))
            {
                if (req->minItemLevel && (plr->GetEquipGearScore(false,false) < req->minItemLevel))
                {
                    forbiddenReason = (uint32)LFG_FORBIDDEN_LOW_GEAR_SCORE;
                }
                else if (req->achievement && !plr->GetAchievementMgr().HasAchievement(req->achievement))
                {
                    forbiddenReason = (uint32)LFG_FORBIDDEN_MISSING_ACHIEVEMENT;
                }
                else if (plr->GetTeam() == ALLIANCE && req->allianceQuestId && !plr->GetQuestRewardStatus(req->allianceQuestId))
                {
                    forbiddenReason = (uint32)LFG_FORBIDDEN_QUEST_INCOMPLETE;
                }
                else if (plr->GetTeam() == HORDE && req->hordeQuestId && !plr->GetQuestRewardStatus(req->hordeQuestId))
                {
                    forbiddenReason = (uint32)LFG_FORBIDDEN_QUEST_INCOMPLETE;
                }
                else
                    if (req->item)
                    {
                        if (!plr->HasItemCount(req->item, 1) && (!req->item2 || !plr->HasItemCount(req->item2, 1)))
                        {
                            forbiddenReason = LFG_FORBIDDEN_MISSING_ITEM;
                        }
                    }
                    else if (req->item2 && !plr->HasItemCount(req->item2, 1))
                    {
                        forbiddenReason = LFG_FORBIDDEN_MISSING_ITEM;
                    }
            }

            if (forbiddenReason)
            {
                randomDungeons[dungeon->Entry()] = forbiddenReason;
            }
        }
    }
    return randomDungeons;
}

namespace
{
    // The client's LFD frame offers four INDEPENDENT checkboxes -- FrameXML/LFDFrame.lua
    // calls SetLFGRoles(leader, tank, healer, dps) -- so the mask that arrives on the
    // wire routinely carries several roles at once. A player who ticked tank AND dps is
    // willing to fill either, not neither.
    //
    // Every consumer here used to switch on the exact value of (mask & ~LEADER), matching
    // only 0x02/0x04/0x08. A hybrid therefore counted as zero of everything: solo hybrids
    // merged into a full-size entry that still reported every role missing and could
    // neither complete nor merge again, and a premade containing one hybrid failed its
    // role check outright and was ejected.
    //
    // Assigning each player exactly one of the roles they offered needs backtracking, not
    // a greedy pass: given a tank-only player and a tank-or-healer player, handing the
    // tank slot to the hybrid first strands the specialist even though a valid assignment
    // exists.
    //
    // The search is MEMOISED, and that is not an optimisation. Plain backtracking is
    // exponential in the number of players, and this is not a five-man-only path: raid
    // finder rows ask for 2/6/17 and flexible raid for 0/0/25, so a 25-player entry of
    // hybrids would explore on the order of 3^25 states and hang the world thread --
    // LFGMgr::Update runs on it. Keying failures on (index, remaining quota) collapses
    // that to at most (players+1) x (tank+1) x (healer+1) x (damage+1) states, a few
    // thousand even for the largest shipped composition.
    struct RoleQuota
    {
        uint8 tank;
        uint8 healer;
        uint8 damage;

        uint32 Total() const { return uint32(tank) + healer + damage; }
    };

    /// Pack (index, remaining quota) into one key for the failure memo.
    uint64 RoleStateKey(size_t index, RoleQuota const& remaining)
    {
        return (uint64(index) << 24)
             | (uint64(remaining.tank) << 16)
             | (uint64(remaining.healer) << 8)
             | uint64(remaining.damage);
    }

    bool AssignRolesRecursive(std::vector<uint8> const& masks, size_t index, RoleQuota remaining,
                              RoleQuota& leftover, std::set<uint64>& deadEnds)
    {
        if (index == masks.size())
        {
            leftover = remaining;   // what is still open once everyone present is placed
            return true;
        }

        // Already proved unsatisfiable from this exact state.
        uint64 const key = RoleStateKey(index, remaining);
        if (deadEnds.find(key) != deadEnds.end())
        {
            return false;
        }

        static uint8 const candidates[3] = { PLAYER_ROLE_TANK, PLAYER_ROLE_HEALER, PLAYER_ROLE_DAMAGE };

        for (uint8 i = 0; i < 3; ++i)
        {
            uint8 const role = candidates[i];
            if (!(masks[index] & role))
            {
                continue;
            }

            uint8* slot = (role == PLAYER_ROLE_TANK) ? &remaining.tank
                        : (role == PLAYER_ROLE_HEALER) ? &remaining.healer
                        : &remaining.damage;

            if (!*slot)
            {
                continue;
            }

            --(*slot);
            if (AssignRolesRecursive(masks, index + 1, remaining, leftover, deadEnds))
            {
                return true;
            }
            ++(*slot);
        }

        deadEnds.insert(key);
        return false;
    }

    /// Can every player fill exactly one of the roles they offered, within the dungeon's caps?
    /// On success `leftover` receives the roles still open, which is what the queue
    /// advertises as "needed" and what the completion test reads.
    bool RolesFitQuota(roleMap const& roles, RoleQuota const& quota, RoleQuota& leftover)
    {
        if (roles.size() > quota.Total())
        {
            return false;
        }

        std::vector<uint8> masks;
        masks.reserve(roles.size());

        for (roleMap::const_iterator it = roles.begin(); it != roles.end(); ++it)
        {
            uint8 const offered = uint8(it->second & ~PLAYER_ROLE_LEADER);
            if (!offered)
            {
                return false;   // no role ticked at all -- cannot be placed
            }

            masks.push_back(offered);
        }

        // Least-flexible player first, so the search prunes early.
        std::sort(masks.begin(), masks.end(), [](uint8 a, uint8 b)
        {
            uint8 popA = uint8((a & 2 ? 1 : 0) + (a & 4 ? 1 : 0) + (a & 8 ? 1 : 0));
            uint8 popB = uint8((b & 2 ? 1 : 0) + (b & 4 ? 1 : 0) + (b & 8 ? 1 : 0));
            return popA < popB;
        });

        leftover = quota;

        std::set<uint64> deadEnds;
        return AssignRolesRecursive(masks, 0, quota, leftover, deadEnds);
    }

    /// The role composition a dungeon actually wants, straight off its DBC row.
    ///
    /// Not every queueable row is a 1/1/3 five-man: the shipped LfgDungeons.dbc carries
    /// 0/0/3 scenarios, 0/0/1 solo content, 2/6/17 raid finder and 0/0/25 flexible raid.
    /// Reading the row instead of assuming NORMAL_* is what lets those queue at all.
    bool GetDungeonQuota(std::set<uint32> const& dungeonList, RoleQuota& quota)
    {
        if (dungeonList.empty())
        {
            return false;
        }

        LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(*dungeonList.begin());
        if (!dungeon)
        {
            return false;
        }

        quota.tank   = uint8(dungeon->Count_tank);
        quota.healer = uint8(dungeon->Count_healer);
        quota.damage = uint8(dungeon->Count_damage);

        return quota.Total() != 0;
    }
}

bool LFGMgr::RolesAreValidForDungeons(roleMap const& roles, std::set<uint32> const& dungeonList)
{
    RoleQuota quota;
    if (!GetDungeonQuota(dungeonList, quota))
    {
        return false;
    }

    RoleQuota leftover;
    return RolesFitQuota(roles, quota, leftover);
}

void LFGMgr::UpdateNeededRoles(ObjectGuid guid, LFGPlayers* information)
{
    // The role composition comes from the DUNGEON, not from a difficulty test.
    //
    // This previously read `if (dungeon->DifficultyID == DUNGEON_DIFFICULTY_NORMAL)`,
    // comparing a RAW client DifficultyID against the internal 0-based enum -- two
    // different key spaces at 5.4.8. The shipped LfgDungeons.dbc carries no queueable
    // row with DifficultyID 0 at all (TypeID 1 has {1,2,7,11,12,14}, TypeID 6 has
    // {1,2,11,12}), so the branch NEVER fired and the needed-role counts stayed at zero
    // for every entry. With zeros, RoleMapsAreCompatible computed (3-0)+(3-0) = 6 > 3
    // and refused every pair, so the matchmaker formed nothing at all.
    RoleQuota quota;
    if (!GetDungeonQuota(information->dungeonList, quota))
    {
        m_playerData[guid] = *information;
        return;
    }

    RoleQuota leftover;
    if (!RolesFitQuota(information->currentRoles, quota, leftover))
    {
        // No assignment places everyone present -- the entry is over-subscribed on some
        // role. Report the dungeon's full requirement so it advertises as unsatisfiable
        // rather than wrapping a uint8 and claiming 254 damage slots are free.
        leftover = quota;
    }

    // The resolver already clamped these: they count down from the quota as players are
    // placed and can never go below zero, so the old `1 - tankCount` uint8 wrap that
    // turned a two-tank party into "254 more tanks welcome" cannot recur.
    information->neededTanks   = leftover.tank;
    information->neededHealers = leftover.healer;
    information->neededDps     = leftover.damage;

    m_playerData[guid] = *information;
}

void LFGMgr::AddToQueue(ObjectGuid guid)
{
    LFGPlayers* information = GetPlayerOrPartyData(guid);
    if (!information)
    {
        return;
    }

    // This will be necessary for finding matches in the queue
    UpdateNeededRoles(guid, information);

    // put info into wait time maps for starters
    for (roleMap::iterator it = information->currentRoles.begin(); it != information->currentRoles.end(); ++it)
    {
        AddToWaitMap(it->second, information->dungeonList);
    }

    // just in case someone's already been in the queue.
    queueSet::iterator qItr = m_queueSet.find(guid);
    if (qItr == m_queueSet.end())
    {
        m_queueSet.insert(guid);
    }

    // Tell the client its queue status straight away. Retail's first SMSG_LFG_QUEUE_STATUS
    // lands 1-5s after the join, long before any tick would fire -- see SendQueueStatusFor.
    // This must stay AFTER UpdateNeededRoles above, which fills the tank/healer/dps counts
    // the packet carries; sending first would report a queue that needs nobody.
    SendQueueStatusFor(guid, time(0));
}

void LFGMgr::RemoveFromQueue(ObjectGuid guid)
{
    m_queueSet.erase(guid);

    //todo - might need to implement a removefromwaitmap function
}

void LFGMgr::AddToWaitMap(uint8 role, std::set<uint32> dungeons)
{
    // use withoutLeader for switch operator
    uint8 withoutLeader = role;
    withoutLeader &= ~PLAYER_ROLE_LEADER;

    switch (withoutLeader)
    {
        case PLAYER_ROLE_TANK:
        {
            for (std::set<uint32>::iterator itr = dungeons.begin(); itr != dungeons.end(); ++itr)
            {
                waitTimeMap::iterator it = m_tankWaitTime.find(*itr);
                if (it != m_tankWaitTime.end())
                {
                    // Increment current player count by one
                    ++it->second.playerCount;
                }
                else
                {
                    LFGWait waitInfo(QUEUE_DEFAULT_TIME, -1, 1, false);
                    m_tankWaitTime[*itr] = waitInfo;
                }
            }
        } break;
        case PLAYER_ROLE_HEALER:
        {
            for (std::set<uint32>::iterator itr = dungeons.begin(); itr != dungeons.end(); ++itr)
            {
                waitTimeMap::iterator it = m_healerWaitTime.find(*itr);
                if (it != m_healerWaitTime.end())
                {
                    // Increment current player count by one
                    ++it->second.playerCount;
                }
                else
                {
                    LFGWait waitInfo(QUEUE_DEFAULT_TIME, -1, 1, false);
                    m_healerWaitTime[*itr] = waitInfo;
                }
            }
        } break;
        case PLAYER_ROLE_DAMAGE:
        {
            for (std::set<uint32>::iterator itr = dungeons.begin(); itr != dungeons.end(); ++itr)
            {
                waitTimeMap::iterator it = m_dpsWaitTime.find(*itr);
                if (it != m_dpsWaitTime.end())
                {
                    // Increment current player count by one
                    ++it->second.playerCount;
                }
                else
                {
                    LFGWait waitInfo(QUEUE_DEFAULT_TIME, -1, 1, false);
                    m_dpsWaitTime[*itr] = waitInfo;
                }
            }
        } break;
        default:
            break;
    }

    // insert the average time regardless of role
    for (std::set<uint32>::iterator itr = dungeons.begin(); itr != dungeons.end(); ++itr)
    {
        waitTimeMap::iterator it = m_avgWaitTime.find(*itr);
        if (it != m_avgWaitTime.end())
        {
            ++it->second.playerCount;
        }
        else
        {
            LFGWait waitInfo(QUEUE_DEFAULT_TIME, -1, 1, false);
            m_avgWaitTime[*itr] = waitInfo;
        }
    }
}

bool LFGMgr::HasLiveProposalFor(ObjectGuid plrGuid) const
{
    for (proposalMap::const_iterator it = m_proposalMap.begin(); it != m_proposalMap.end(); ++it)
    {
        if (it->second.answers.find(plrGuid) != it->second.answers.end())
        {
            return true;
        }
    }

    return false;
}

ObjectGuid LFGMgr::FindQueueEntryContaining(ObjectGuid plrGuid) const
{
    // Their own key first: that is the common case and it is O(1).
    playerData::const_iterator own = m_playerData.find(plrGuid);
    if (own != m_playerData.end())
    {
        return plrGuid;
    }

    // Otherwise they were merged into somebody else's entry, or queued as part of a
    // party keyed by the group guid.
    for (playerData::const_iterator it = m_playerData.begin(); it != m_playerData.end(); ++it)
    {
        if (it->second.currentRoles.find(plrGuid) != it->second.currentRoles.end())
        {
            return it->first;
        }
    }

    return ObjectGuid();
}

void LFGMgr::RemovePlayerFromQueue(ObjectGuid plrGuid)
{
    ObjectGuid const entryGuid = FindQueueEntryContaining(plrGuid);

    m_playerStatusMap.erase(plrGuid);

    if (!entryGuid)
    {
        m_queueSet.erase(plrGuid);
        m_playerData.erase(plrGuid);
        return;
    }

    LFGPlayers* entry = GetPlayerOrPartyData(entryGuid);
    if (!entry)
    {
        return;
    }

    entry->currentRoles.erase(plrGuid);

    // Last one out takes the entry with them.
    if (entry->currentRoles.empty())
    {
        m_queueSet.erase(entryGuid);
        m_playerData.erase(entryGuid);
        return;
    }

    // The survivors need one fewer of whatever this player was covering.
    UpdateNeededRoles(entryGuid, entry);
}

bool LFGMgr::EntryHasGameMaster(LFGPlayers const* entry) const
{
    if (!entry)
    {
        return false;
    }

    for (roleMap::const_iterator it = entry->currentRoles.begin(); it != entry->currentRoles.end(); ++it)
    {
        Player* pPlayer = sObjectAccessor.FindPlayer(it->first);

        // Account security, not `.gm on`. The operator should not have to make
        // themselves untargetable just to test the dungeon finder.
        if (pPlayer && pPlayer->GetSession() &&
            pPlayer->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
        {
            return true;
        }
    }

    return false;
}

bool LFGMgr::TryFormGroup(ObjectGuid guid)
{
    LFGPlayers* entry = GetPlayerOrPartyData(guid);
    if (!entry || entry->currentState != LFG_STATE_QUEUED)
    {
        return false;
    }

    // `.debug dungeon` lets an entry containing a game master go without a full
    // composition, so the operator can drive the whole proposal -> group -> teleport
    // chain without finding four other people. Everyone else still needs a real group.
    bool const debugComplete = m_debugMode != LFG_DEBUG_OFF && EntryHasGameMaster(entry);

    if (!debugComplete && (entry->neededTanks || entry->neededHealers || entry->neededDps))
    {
        return false;
    }

    // Everyone in the entry must be online. SendDungeonProposal skips offline players
    // when filling `groups` and `answers` while `currentRoles` still counts them toward
    // the completed composition, so the online members could all accept, `allOkay` would
    // see no pending answer for the absent one, and a SHORT group would be built and
    // teleported in. Drop them from the entry instead and let it re-fill.
    std::vector<ObjectGuid> offline;
    for (roleMap::const_iterator it = entry->currentRoles.begin(); it != entry->currentRoles.end(); ++it)
    {
        if (!sObjectAccessor.FindPlayer(it->first))
        {
            offline.push_back(it->first);
        }
    }

    if (!offline.empty())
    {
        for (std::vector<ObjectGuid>::const_iterator it = offline.begin(); it != offline.end(); ++it)
        {
            entry->currentRoles.erase(*it);
            m_playerStatusMap.erase(*it);
        }

        if (entry->currentRoles.empty())
        {
            m_queueSet.erase(guid);
            m_playerData.erase(guid);
            return false;
        }

        UpdateNeededRoles(guid, entry);
        return false;   // no longer complete; stays queued and keeps looking
    }

    // Construct the whole proposal while the entry is still queued. Every refusal in
    // SendDungeonProposal occurs before player/proposal state or packets change, so a
    // category with no concrete destination remains an intact queue entry here.
    if (!SendDungeonProposal(guid, entry))
    {
        return false;
    }

    // Out of the MATCH set, but the entry itself stays. Leaving it in m_queueSet would
    // have it matched again next tick and fire a fresh proposal -- and a new
    // SMSG_LFG_PROPOSAL_UPDATE -- every tick forever. Keeping m_playerData is what lets
    // a declined or timed-out proposal put the survivors back in the queue rather than
    // ejecting them from the dungeon finder.
    m_queueSet.erase(guid);
    entry->currentState = LFG_STATE_PROPOSAL;
    return true;
}

void LFGMgr::CancelProposal(uint32 proposalId, std::set<ObjectGuid> const& culprits)
{
    proposalMap::iterator it = m_proposalMap.find(proposalId);
    if (it == m_proposalMap.end())
    {
        return;
    }

    LFGProposal proposal = it->second;      // copy: the map entry is erased below
    m_proposalMap.erase(it);

    // Tell every client the proposal is over so the window closes.
    proposal.state = LFG_PROPOSAL_FAILED;
    for (proposalAnswerMap::const_iterator ans = proposal.answers.begin();
         ans != proposal.answers.end(); ++ans)
    {
        if (Player* pMember = sObjectAccessor.FindPlayer(ans->first))
        {
            pMember->GetSession()->SendLfgProposalUpdate(proposal);
        }
    }

    LFGPlayers* entry = GetPlayerOrPartyData(proposal.queueGuid);

    // The players responsible leave the dungeon finder outright -- the client says so:
    // "You have been removed from the queue because you did not accept the invitation."
    //
    // The entry keyed by proposal.queueGuid is deliberately NOT erased in this loop.
    // A culprit is very often the entry key itself -- the solo player whose entry did
    // the absorbing, or the single queuer in a `.debug dungeon` proposal -- and erasing
    // m_playerData[queueGuid] here destroyed the node `entry` points into, which the
    // survivor check below then read. An ordinary decline was a use-after-free on the
    // world thread.
    for (std::set<ObjectGuid>::const_iterator bad = culprits.begin(); bad != culprits.end(); ++bad)
    {
        if (entry)
        {
            entry->currentRoles.erase(*bad);
        }

        // isGroup derived from how this member was ANNOUNCED, not hardcoded false.
        //
        // The client files each status body under the whole 20-byte RideTicket, and
        // SendLfgUpdate picks requesterGuid from this flag. A party-owned queue announced
        // its opening bodies group-keyed (reason 24 at LFGMgrQueue.cpp, reason 14 at
        // SendDungeonProposal), so closing with false carried a DIFFERENT ticket: the
        // client created a second record and left the first at joined = 1, queued = 0,
        // which UIParent.lua:3932 reports as "suspended" -- the eye stuck until relog.
        // This is the mainline path for an ordinary decline.
        //
        // proposal.groups records exactly how each member was announced, so it is the
        // authority here.
        playerGroupMap::const_iterator badGroup = proposal.groups.find(*bad);
        bool const badWasGroupKeyed = badGroup != proposal.groups.end() && badGroup->second;

        SetPlayerState(*bad, LFG_STATE_NONE);
        SetPlayerUpdateType(*bad, LFG_UPDATE_LEAVE);
        SendLfgUpdate(*bad, GetPlayerStatus(*bad), badWasGroupKeyed);

        m_playerStatusMap.erase(*bad);

        if (*bad == proposal.queueGuid)
        {
            continue;   // handled below, after `entry` is finished with
        }

        m_queueSet.erase(*bad);
        m_playerData.erase(*bad);
    }

    if (!entry || entry->currentRoles.empty())
    {
        m_queueSet.erase(proposal.queueGuid);
        m_playerData.erase(proposal.queueGuid);
        return;
    }

    // Everyone else goes back in: "You have been returned to the front of the queue."
    entry->currentState = LFG_STATE_QUEUED;
    UpdateNeededRoles(proposal.queueGuid, entry);

    for (roleMap::const_iterator role = entry->currentRoles.begin();
         role != entry->currentRoles.end(); ++role)
    {
        playerGroupMap::const_iterator roleGroup = proposal.groups.find(role->first);
        bool const roleWasGroupKeyed = roleGroup != proposal.groups.end() && roleGroup->second;

        SetPlayerState(role->first, LFG_STATE_QUEUED);
        SetPlayerUpdateType(role->first, LFG_UPDATE_ADDED_TO_QUEUE);
        SendLfgUpdate(role->first, GetPlayerStatus(role->first), roleWasGroupKeyed);
    }

    // Anyone in the proposal who was neither blamed nor requeued still has an open
    // reason-14 record. Without a closing body it sits at joined = 1, queued = 0 and the
    // eye stays lit. Reachable whenever `entry` no longer lists them -- a merged queuer
    // whose entry was folded away, or a member removed by the group branch of LeaveLFG.
    for (proposalAnswerMap::const_iterator ans = proposal.answers.begin();
         ans != proposal.answers.end(); ++ans)
    {
        if (culprits.find(ans->first) != culprits.end() ||
            entry->currentRoles.find(ans->first) != entry->currentRoles.end())
        {
            continue;                   // already closed out or requeued above
        }

        playerGroupMap::const_iterator ansGroup = proposal.groups.find(ans->first);
        bool const ansWasGroupKeyed = ansGroup != proposal.groups.end() && ansGroup->second;

        SetPlayerState(ans->first, LFG_STATE_NONE);
        SetPlayerUpdateType(ans->first, LFG_UPDATE_LEAVE);
        SendLfgUpdate(ans->first, GetPlayerStatus(ans->first), ansWasGroupKeyed);
    }

    m_queueSet.insert(proposal.queueGuid);
}

void LFGMgr::CancelProposalsFor(ObjectGuid plrGuid)
{
    // Same teardown the expiry reaper performs, but driven by an explicit leave instead
    // of the clock. The player is the culprit -- they are the one walking away -- so the
    // others are requeued without them, exactly as a decline would do.
    std::vector<uint32> owned;
    for (proposalMap::const_iterator it = m_proposalMap.begin(); it != m_proposalMap.end(); ++it)
    {
        if (it->second.answers.find(plrGuid) != it->second.answers.end())
        {
            owned.push_back(it->first);
        }
    }

    // Collected first: CancelProposal erases from the map being walked.
    for (std::vector<uint32>::const_iterator it = owned.begin(); it != owned.end(); ++it)
    {
        std::set<ObjectGuid> culprit;
        culprit.insert(plrGuid);
        CancelProposal(*it, culprit);
    }
}

/// End a boot vote and restore every state it touched before dropping its record.
void LFGMgr::FinishBootVote(ObjectGuid groupGuid, Group* pGroup, LFGBoot boot,
                            bool removeVictim, bool notify)
{
    LFGStatePolicy::BootTerminalPlan const plan =
        LFGStatePolicy::ResolveBootTerminal(removeVictim, pGroup != NULL);

    boot.inProgress = false;

    // First clear everyone recorded by the vote. A participant may have left by a
    // path that did not update the live Group roster; such a player is not a dungeon
    // survivor and must not keep LFG_STATE_BOOT.
    for (proposalAnswerMap::const_iterator ans = boot.answers.begin();
         ans != boot.answers.end(); ++ans)
    {
        SetPlayerState(ans->first, LFG_STATE_NONE);
    }
    SetPlayerState(boot.playerVotedOn, LFG_STATE_NONE);

    if (plan.restoreGroup)
    {
        if (LFGGroupStatus* status = GetGroupStatus(groupGuid))
        {
            if (status->state == LFG_STATE_BOOT)
            {
                status->state = LFG_STATE_IN_DUNGEON;
                m_groupStatusMap[groupGuid] = *status;
            }
        }

        // MemberSlots includes offline members; GroupReference does not. Restore state
        // from the persisted roster, then use the live references only for notification.
        for (Group::MemberSlotList::const_iterator member = pGroup->GetMemberSlots().begin();
             member != pGroup->GetMemberSlots().end(); ++member)
        {
            bool const isVictim = member->guid == boot.playerVotedOn;
            LFGStatePolicy::BootPlayerState const disposition =
                isVictim ? plan.victim : plan.survivor;
            LFGState const state = disposition == LFGStatePolicy::BootPlayerState::None
                    ? LFG_STATE_NONE : LFG_STATE_IN_DUNGEON;
            SetPlayerState(member->guid, state);
        }

        if (notify)
        {
            for (GroupReference* ref = pGroup->GetFirstMember(); ref != NULL; ref = ref->next())
            {
                if (Player* member = ref->getSource())
                {
                    member->GetSession()->SendLfgBootUpdate(boot);
                }
            }
        }
    }

    // Every terminal path owns this erasure. Keeping it here prevents a completed,
    // expired, abandoned or disbanded vote from blocking the next one.
    m_bootStatusMap.erase(groupGuid);
}

/// Expire boot votes nobody finished answering.
///
/// LFG_TIME_BOOT is 30 seconds, measured across all 14 observed retail boot
/// sessions, and the client counts down against the timeLeft we send. Nothing
/// cleared the vote when that ran out, so a boot that never reached the threshold
/// pinned the whole group in LFG_STATE_BOOT permanently: CastVote refuses any
/// other state, AttemptToKickPlayer now refuses a boot already in progress, and
/// the state only ever moved again on a completed vote. One ignored popup
/// disabled vote kick for the rest of the run.
///
/// An expired vote FAILS. A kick needs REQUIRED_VOTES_FOR_BOOT explicit agrees,
/// so silence must never remove anybody.
void LFGMgr::RemoveOldBoots()
{
    time_t const now = time(NULL);

    std::vector<ObjectGuid> expired;
    for (bootStatusMap::const_iterator it = m_bootStatusMap.begin(); it != m_bootStatusMap.end(); ++it)
    {
        if (it->second.inProgress && it->second.startTime &&
            (now - it->second.startTime) >= LFG_TIME_BOOT)
        {
            expired.push_back(it->first);
        }
    }

    // Collected first, because the loop below erases from the map being walked.
    for (std::vector<ObjectGuid>::const_iterator it = expired.begin(); it != expired.end(); ++it)
    {
        ObjectGuid const groupGuid = *it;

        bootStatusMap::iterator bootIt = m_bootStatusMap.find(groupGuid);
        if (bootIt == m_bootStatusMap.end())
        {
            continue;
        }

        LFGBoot const boot = bootIt->second;
        Group* pGroup = sObjectMgr.GetGroupById(groupGuid.GetCounter());
        FinishBootVote(groupGuid, pGroup, boot, false, true);

        DEBUG_LOG("LFG RemoveOldBoots: vote against %s in group %s expired after %u s; nobody removed",
                  boot.playerVotedOn.GetString().c_str(), groupGuid.GetString().c_str(),
                  uint32(LFG_TIME_BOOT));
    }
}

void LFGMgr::RemoveOldProposals()
{
    time_t const now = time(NULL);

    std::vector<uint32> expired;
    for (proposalMap::const_iterator it = m_proposalMap.begin(); it != m_proposalMap.end(); ++it)
    {
        if (it->second.createdTime && (now - it->second.createdTime) >= LFG_TIME_PROPOSAL)
        {
            expired.push_back(it->first);
        }
    }

    // Collected first: CancelProposal erases from the map being walked.
    //
    // Without this reaper a recipient who ignored the popup, disconnected, or whose
    // client-side timer lapsed left everyone else pinned at LFG_STATE_PROPOSAL for ever
    // -- and JoinLFG refuses that state, so they could not re-queue until relog.
    for (std::vector<uint32>::const_iterator it = expired.begin(); it != expired.end(); ++it)
    {
        proposalMap::const_iterator prop = m_proposalMap.find(*it);
        if (prop == m_proposalMap.end())
        {
            continue;
        }

        // Whoever did not answer is the culprit, exactly as a decliner would be.
        //
        // Cancelling with an empty culprit set requeued the entry UNCHANGED, including
        // the member who never responded. The role counts were still complete, so the
        // very next tick re-formed the same proposal and timed out again -- trapping the
        // players who did accept in a permanent timeout loop.
        std::set<ObjectGuid> silent;
        for (proposalAnswerMap::const_iterator ans = prop->second.answers.begin();
             ans != prop->second.answers.end(); ++ans)
        {
            if (ans->second != LFG_ANSWER_AGREE || !sObjectAccessor.FindPlayer(ans->first))
            {
                silent.insert(ans->first);
            }
        }

        DEBUG_LOG("LFG RemoveOldProposals: proposal %u expired after %u s with %u "
                  "unanswered member(s); cancelling",
                  *it, uint32(LFG_TIME_PROPOSAL), uint32(silent.size()));
        CancelProposal(*it, silent);
    }
}

void LFGMgr::FindQueueMatches()
{
    // Snapshot: MergeGroups and TryFormGroup both erase from m_queueSet, and erasing the
    // element an active iterator points at is UB.
    queueSet const snapshot = m_queueSet;

    for (queueSet::const_iterator itr = snapshot.begin(); itr != snapshot.end(); ++itr)
    {
        // An entry can be absorbed or dequeued by an earlier iteration of this same pass.
        if (m_queueSet.find(*itr) == m_queueSet.end())
        {
            continue;
        }

        FindSpecificQueueMatches(*itr);

        // A party that arrives already complete -- the common premade-of-five case --
        // is never merged with anything, so the completion test inside MergeGroups
        // never sees it. Without this check such a group waits in the queue forever.
        TryFormGroup(*itr);
    }
}

void LFGMgr::FindSpecificQueueMatches(ObjectGuid guid)
{
    uint64 rawGuid = guid.GetRawValue();
    LFGPlayers* queueInfo = GetPlayerOrPartyData(guid);
    if (queueInfo)
    {
        // compare to everyone else in queue for compatibility
        // after a match is found call UpdateNeededRoles
        // Use the roleMap to store player guid/role information; merge into queueInfo struct & delete other struct/map entry
        queueSet const snapshot = m_queueSet;

        for (queueSet::const_iterator itr = snapshot.begin(); itr != snapshot.end(); ++itr)
        {
            if (*itr == guid)
            {
                continue;
            }

            // Absorbed by an earlier merge in this same pass, or dequeued by a proposal.
            if (m_queueSet.find(*itr) == m_queueSet.end())
            {
                continue;
            }

            // Re-read: MergeGroups mutates the entry we are accumulating into.
            queueInfo = GetPlayerOrPartyData(guid);
            if (!queueInfo)
            {
                return;
            }

            LFGPlayers* matchInfo = GetPlayerOrPartyData(*itr);
            if (matchInfo)
            {
                // 1. iterate through queueInfo's dungeon set and search the matchInfo for a matching entry.
                // 2. if an(y) entry is found, great and proceed!
                // 2a. if an entry is found and the amounts of players-to-roles are compatible, make
                //     a new map of only the inter-compatible dungeons and use that if the other checks pass
                // 3. Regardless of outcome, after the end of calculations send a LFGQueueStatus packet
                bool fullyCompatible = false;
                std::set<uint32> compatibleDungeons;

                // Compare what each entry can actually RUN, not what it asked for.
                //
                // A random queue stores only the CATEGORY row in dungeonList -- the
                // expansion lives in candidateDungeons -- so intersecting dungeonList
                // directly compared a category id against real dungeon ids and matched
                // nothing. Random-vs-random happened to work because both carried the same
                // category, and specific-vs-specific worked; RANDOM-VS-SPECIFIC could never
                // match at all, which is most of what a random queuer exists to do, and it
                // is also why a random queuer could never backfill a run pinned to its
                // dungeon.
                std::set<uint32> const& queueRunnable =
                    queueInfo->candidateDungeons.empty() ? queueInfo->dungeonList
                                                         : queueInfo->candidateDungeons;
                std::set<uint32> const& matchRunnable =
                    matchInfo->candidateDungeons.empty() ? matchInfo->dungeonList
                                                         : matchInfo->candidateDungeons;

                for (std::set<uint32>::const_iterator dItr = matchRunnable.begin(); dItr != matchRunnable.end(); ++dItr)
                {
                    if (queueRunnable.find(*dItr) != queueRunnable.end())
                    {
                        compatibleDungeons.insert(*dItr);
                    }
                }

                if (!compatibleDungeons.empty())
                {
                    // check for player / role count and also team compatibility
                    // if function returns true, then merge groups into one
                    if (RoleMapsAreCompatible(queueInfo, matchInfo, compatibleDungeons) && MatchesAreOfSameTeam(queueInfo, matchInfo))
                    {
                        MergeGroups(guid, *itr, compatibleDungeons);
                    }
                }
            }
        }
    }
}

bool LFGMgr::RoleMapsAreCompatible(LFGPlayers* groupOne, LFGPlayers* groupTwo,
                                   std::set<uint32> const& compatibleDungeons)
{
    // When this is called we already know the dungeons overlap, so just focus on roles.
    //
    // The question is simply: if these two entries were one, could every player in the
    // union fill a distinct slot the dungeon actually has? Asking the resolver directly
    // replaces the old per-role arithmetic, which recovered "present" as
    // (NORMAL_X - neededX) and so inherited every uint8 wrap in neededX -- a two-tank
    // party gave (1-255) + (1-0) = -254, which passed the cap test and merged a party
    // that could never complete.
    //
    // It also drops the hardcoded 1/1/3/5, which is wrong for the 108 of 247 queueable
    // TypeID 1 rows that are scenarios (0/0/3), solo content (0/0/1), raid finder
    // (2/6/17) or flexible raid (0/0/25).
    RoleQuota quota;
    if (!GetDungeonQuota(compatibleDungeons, quota))
    {
        return false;
    }

    if ((groupOne->currentRoles.size() + groupTwo->currentRoles.size()) > quota.Total())
    {
        return false;
    }

    // `.debug dungeon group`: an entry containing a game master takes whoever else is
    // waiting, whatever they picked. The size cap above still applies, and the duplicate
    // check below still applies -- only the role composition is waived, and only when a
    // GM is involved.
    bool const debugMerge = m_debugMode == LFG_DEBUG_GROUP &&
                            (EntryHasGameMaster(groupOne) || EntryHasGameMaster(groupTwo));

    roleMap combined = groupOne->currentRoles;
    for (roleMap::const_iterator it = groupTwo->currentRoles.begin(); it != groupTwo->currentRoles.end(); ++it)
    {
        combined[it->first] = it->second;
    }

    // A player present in both entries collapses to one key, so the union can be smaller
    // than the sum -- that is the duplicate-membership case and it must not merge.
    if (combined.size() != groupOne->currentRoles.size() + groupTwo->currentRoles.size())
    {
        return false;
    }

    // At most ONE live finder run per proposal.
    //
    // Merging two in-progress runs has no sane resolution: whichever group were continued,
    // the other's players would be torn out of a dungeon they are standing in and their
    // instance abandoned. Every fork treats this as hard-incompatible -- SkyFire
    // LFGQueue.cpp:355-360, CPP :478-483, PandariaCore :382-386, all named
    // LFG_INCOMPATIBLES_MULTIPLE_LFG_GROUPS.
    //
    // Checked BEFORE the debug early-out on purpose: a `.debug dungeon` merge must not be
    // able to build a proposal the normal path refuses.
    uint32 liveRuns = 0;
    ResolveContinuingGroup(combined, liveRuns);
    if (liveRuns > 1)
    {
        return false;
    }

    if (debugMerge)
    {
        return true;
    }

    RoleQuota leftover;
    return RolesFitQuota(combined, quota, leftover);
}

bool LFGMgr::MatchesAreOfSameTeam(LFGPlayers* groupOne, LFGPlayers* groupTwo)
{
    // we should safely be able to compare any two players from each struct to
    // determine compatibility
    roleMap::iterator it1 = groupOne->currentRoles.begin();
    roleMap::iterator it2 = groupTwo->currentRoles.begin();

    // now we find the players from the maps
    Player* pPlayer1 = sObjectAccessor.FindPlayer(it1->first);
    Player* pPlayer2 = sObjectAccessor.FindPlayer(it2->first);

    if (!pPlayer1 || !pPlayer2)
    {
        return false;
    }

    // todo: disable this if a config option is set
    if (pPlayer1->GetTeamId() == pPlayer2->GetTeamId())
    {
        return true;
    }

    return false;
}

void LFGMgr::MergeGroups(ObjectGuid guidOne, ObjectGuid guidTwo, std::set<uint32> compatibleDungeons)
{
    // merge into the entry for rawGuidOne, then see if they are
    // able to enter the dungeon at this point or not
    LFGPlayers* mainGroup   = GetPlayerOrPartyData(guidOne);
    LFGPlayers* bufferGroup = GetPlayerOrPartyData(guidTwo);

    if (!mainGroup || !bufferGroup)
    {
        return;
    }

    // Preserve random request/reward identity independently from the concrete
    // overlap. Which entry absorbs which must not decide whether the category is
    // forgotten. Two distinct random categories cannot share one reward identity,
    // so refuse that merge rather than silently choosing one.
    LFGStatePolicy::QueueSelectionPlan const selection =
        LFGStatePolicy::MergeQueueSelection(mainGroup->randomDungeonID,
                                            bufferGroup->randomDungeonID,
                                            compatibleDungeons);
    if (!selection.valid)
    {
        return;
    }

    mainGroup->randomDungeonID = selection.randomDungeonId;
    mainGroup->dungeonList = selection.requestedDungeons;
    mainGroup->candidateDungeons = selection.candidateDungeons;

    // move players / roles into a single roleMap
    for (roleMap::iterator it = bufferGroup->currentRoles.begin(); it != bufferGroup->currentRoles.end(); ++it)
    {
        mainGroup->currentRoles[it->first] = it->second;
    }

    // update the role count / needed role info
    UpdateNeededRoles(guidOne, mainGroup);

    // being safe
    //mainGroup = GetPlayerOrPartyData(rawGuidOne);

    // Both containers, or guidTwo lingers in m_queueSet pointing at data that no
    // longer exists. That stale entry is not merely a leak: the merged-away
    // player still reads LFG_STATE_QUEUED, SendQueueStatus keys off m_playerData
    // so their client never hears again, and a re-queue skips JoinLFG's
    // duplicate cleanup (it is guarded on existing data) -- leaving that player
    // live in a fresh solo entry AND still listed in the merged entry's roles,
    // which can produce two proposals for the same person.
    m_queueSet.erase(guidTwo);
    m_playerData.erase(guidTwo);

    // Completion is decided after the absorbed entry is gone, so the proposal is built
    // from one consistent view and TryFormGroup can dequeue the survivor safely.
    TryFormGroup(guidOne);
}

void LFGMgr::SendQueueStatus()
{
    // First we should get the current time
    time_t timeNow = time(0);

    // Check who is listed as being in the queue
    for (queueSet::iterator itr = m_queueSet.begin(); itr != m_queueSet.end(); ++itr)
    {
        SendQueueStatusFor(*itr, timeNow);
    }
}

// Split out of SendQueueStatus so a single queue can be told its status the moment it is
// created, rather than only on the next matchmaker tick.
//
// Retail sends the first SMSG_LFG_QUEUE_STATUS within seconds of the join: across 55 queued
// sessions at build 18414 the delay from CMSG_LFG_JOIN to the first status is 1-5s (median 4),
// and it then repeats roughly every 35s. capture-000044 seq 1577 confirms both halves of that
// -- its queuedTime field reads 3, matching the 3s the index measured since the join.
//
// Tick-only delivery could not reproduce that. SendQueueStatus runs at the END of Update(),
// AFTER FindQueueMatches, so a queue that matched on its first tick was dequeued before the
// status was ever built and the player got NONE at all -- no role counts, no average wait, an
// empty eye tooltip. Observed live on a solo debug queue that matched 29s after joining.
void LFGMgr::SendQueueStatusFor(ObjectGuid queueGuid, time_t timeNow)
{
    // make sure it's not a false entry
    LFGPlayers* queueInfo = GetPlayerOrPartyData(queueGuid);
    if (!queueInfo || queueInfo->currentState != LFG_STATE_QUEUED)
    {
        return;
    }

    // Guarded because this now also runs at join time: dungeonList.begin() on an empty
    // set is undefined, and an empty list is reachable if every candidate was filtered.
    if (queueInfo->dungeonList.empty())
    {
        return;
    }

    for (roleMap::iterator rItr = queueInfo->currentRoles.begin(); rItr != queueInfo->currentRoles.end(); ++rItr)
    {
        if (Player* pPlayer = sObjectAccessor.FindPlayer(rItr->first))
        {
            uint32 dungeonId = *queueInfo->dungeonList.begin();

            // Each recipient must be told about THEIR OWN queue, not the
            // merged entry's key.
            //
            // The key is whichever entry did the absorbing, so after two solo
            // players merge it is one of their guids. The other player joined
            // under their own guid -- that is what SMSG_LFG_UPDATE_STATUS sent
            // them as requesterGuid -- and a queue status arriving under a
            // stranger's identity does not match the queue their client is
            // tracking, so it is ignored: no role counts, no average wait, a
            // placeholder time in queue, and most of the minimap eye's tooltip
            // missing. The absorbing player saw none of this, because for them
            // the merged key IS their own guid.
            //
            // Mirrors SendLfgUpdate: a party member's queue is keyed by the
            // group guid, everyone else by their own.
            ObjectGuid memberQueueGuid = rItr->first;
            if (Group* pGroup = pPlayer->GetGroup())
            {
                if (pGroup->GetObjectGuid() == queueGuid)
                {
                    memberQueueGuid = queueGuid;
                }
            }

            LFGQueueStatus status;
            status.queueGuid = memberQueueGuid.GetRawValue();
            status.dungeonID        = dungeonId;
            status.neededTanks      = queueInfo->neededTanks;
            status.neededHeals      = queueInfo->neededHealers;
            status.neededDps        = queueInfo->neededDps;
            status.timeSpentInQueue = uint32(timeNow - queueInfo->joinedTime);
            status.joinTime = uint32(queueInfo->joinedTime);
            status.ticketId = queueInfo->ticketId;

            int32 playerWaitTime;

            // strip leader flag from role
            uint8 withoutLeader = rItr->second;
            withoutLeader &= ~PLAYER_ROLE_LEADER;

            switch (withoutLeader)
            {
                case PLAYER_ROLE_TANK:
                    playerWaitTime = m_tankWaitTime[dungeonId].time;
                    break;
                case PLAYER_ROLE_HEALER:
                    playerWaitTime = m_healerWaitTime[dungeonId].time;
                    break;
                case PLAYER_ROLE_DAMAGE:
                    playerWaitTime = m_dpsWaitTime[dungeonId].time;
                    break;
                default:
                    playerWaitTime = m_avgWaitTime[dungeonId].time;
                    break;
            }

            status.playerAvgWaitTime = playerWaitTime;
            status.dpsAvgWaitTime    = m_dpsWaitTime[dungeonId].time;
            status.healerAvgWaitTime = m_healerWaitTime[dungeonId].time;
            status.tankAvgWaitTime   = m_tankWaitTime[dungeonId].time;
            status.avgWaitTime       = m_avgWaitTime[dungeonId].time;

            // Send packet to client
            pPlayer->GetSession()->SendLfgQueueStatus(status);
        }
    }
}

uint32 LFGMgr::GetGroupDungeonEntry(ObjectGuid groupGuid)
{
    // The Group object itself does not know which dungeon it is for -- isLFGGroup() is
    // only a bit in m_groupType -- so the resolved id has to come from the group status
    // LFGMgr records when the dungeon group is created.
    LFGGroupStatus const* status = GetGroupStatus(groupGuid);
    return status ? GetDungeonEntry(status->dungeonID) : 0;
}

void LFGMgr::OnDungeonEncounterCredited(Map* map, bool lastEncounter)
{
    if (!map)
    {
        return;
    }

    // Mark by GROUP, not by player: the run's progress belongs to the run. Collected
    // first so a group with five members inside is only handled once.
    std::set<ObjectGuid> lfgGroups;
    Map::PlayerList const& players = map->GetPlayers();
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* pPlayer = it->getSource();
        if (!pPlayer)
        {
            continue;
        }

        Group* pGroup = pPlayer->GetGroup();
        if (pGroup && pGroup->isLFGGroup())
        {
            lfgGroups.insert(pGroup->GetObjectGuid());
        }
    }

    for (std::set<ObjectGuid>::const_iterator it = lfgGroups.begin(); it != lfgGroups.end(); ++it)
    {
        LFGGroupStatus* status = GetGroupStatus(*it);
        if (!status)
        {
            continue;
        }

        if (!status->madeProgress)
        {
            status->madeProgress = true;
            DEBUG_LOG("LFG: group %s has made progress; leaving no longer earns Deserter",
                      it->GetString().c_str());
        }
    }

    if (!lastEncounter)
    {
        return;
    }

    // The final encounter completes the run. This is the call site the
    // "Place LFG reward here" comment in DungeonPersistentState::UpdateEncounterState
    // was left for: HandleBossKilled has existed all along with NO caller anywhere in
    // the tree, so no LFG run has ever paid its reward or reached
    // LFG_STATE_FINISHED_DUNGEON.
    for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        Player* pPlayer = it->getSource();
        if (!pPlayer)
        {
            continue;
        }

        Group* pGroup = pPlayer->GetGroup();
        if (pGroup && pGroup->isLFGGroup() && GetGroupStatus(pGroup->GetObjectGuid()))
        {
            HandleBossKilled(pPlayer);
            break;      // HandleBossKilled already walks the whole group
        }
    }
}

void LFGMgr::ApplyDungeonCooldown(Player* pPlayer)
{
    // Retail starts a 15-minute requeue cooldown when the player ENTERS, independently
    // of Deserter. The corpus bears that out: spell 71328 appears in 6920 build-18414
    // payloads against 752 for Deserter (71041) -- roughly nine times as many, which is
    // what you expect when everyone who zones in gets one and only early leavers get the
    // other.
    //
    // Never cast on a player who is between maps. A far teleport removes them from the old
    // map at once and m_currMap stays NULL until MSG_MOVE_WORLDPORT_ACK; Spell::CheckCast
    // reads the caster's zone through Map::m_TerrainData and takes the world down on the
    // null. Callers must apply this BEFORE starting the teleport, not after.
    if (!pPlayer || !pPlayer->IsInWorld() || pPlayer->HasAura(LFG_COOLDOWN_SPELL))
    {
        return;
    }

    pPlayer->CastSpell(pPlayer, LFG_COOLDOWN_SPELL, true);
}

void LFGMgr::RecordEntryPoint(Player* pPlayer)
{
    // The return location is captured ONCE, when the player joins the queue -- not when
    // they are teleported in.
    //
    // It used to be taken inside TeleportToDungeon, which runs on EVERY entry. Walk out
    // through the instance portal and you stand at the dungeon's outdoor entrance; use the
    // eye to teleport back in and TeleportToDungeon overwrote the saved point with that
    // doorstep. From then on "Teleport out of dungeon" returned the player to the dungeon
    // door forever, and the place they actually queued from was gone. Reported live:
    // "each time i get ported outside the dungeon entrance, never have i been put back to
    // the queue location".
    //
    // The corpus agrees the point is fixed for the life of a run: in capture-000044 the
    // teleport-out at seq 150590 and a later exit at seq 151999 are byte-identical
    // (map 974, -4046.44 6351.08) even though the player teleported back IN at seq 151132
    // between them. Across DIFFERENT queue episodes it moves, which is what you expect from
    // a value captured at queue time.
    //
    // Skipped on a dungeon or raid map on purpose: SetBattleGroundEntryPoint's dungeon
    // branch resolves the CLOSEST GRAVEYARD rather than the player's position, which is not
    // a place anyone queued from. A player queueing from inside a dungeon (a backfill
    // re-queue) therefore keeps the entry point they already had, which is the correct one.
    if (!pPlayer || !pPlayer->IsInWorld())
    {
        return;
    }

    Map* map = pPlayer->GetMap();
    if (!map || map->IsDungeon() || map->IsRaid() || pPlayer->InBattleGround())
    {
        return;
    }

    pPlayer->SetBattleGroundEntryPoint();
}

void LFGMgr::RestoreDungeonGroup(Group* pGroup, uint32 mapId, uint32 difficulty, uint32 encountersMask)
{
    // Rebuild a live run's LFG status after a world restart, from state that already
    // persists. No new table, no schema change.
    //
    // Nothing in LFGMgr is saved, so after a restart a party still standing in its dungeon
    // came back with the group intact (groups.groupType keeps GROUPTYPE_LFD, and the bind
    // reloads from group_instance) and an LFGMgr that had never heard of it. One line then
    // took the whole feature out: Group.cpp's `update.isLfg = isLFGGroup() &&
    // GetGroupDungeonEntry(...) != 0` resolved to 0, so login's SendUpdate emitted
    // SMSG_GROUP_LIST with no LFG block at all.
    //
    // That block is the ONLY thing the 18414 client reads for an in-progress run: the
    // group-list apply path is the sole writer of the LFG fields on its group object, and it
    // ZEROES them when the packet's flag is clear. IsPartyLFG(), GetPartyLFGID(),
    // HasLFGRestrictions() and IsInLFGDungeon() all hang off those fields, so with an empty
    // block the minimap eye, "Teleport out of dungeon", "Leave Instance Group" and the Vote
    // Kick gate simply do not exist. Observed live 2026-08-06: a player logged back into
    // Deadmines after a restart, still grouped, with no eye.
    //
    // Everything needed is recoverable, so restoring the INPUTS is enough -- every existing
    // send path then behaves normally:
    //   dungeonID    <- the LfgDungeons row matching the bind's (map, difficulty)
    //   madeProgress <- encountersMask != 0, which is stronger than any reference fork
    //                   manages; none of them persist it at all
    //   leaderGuid   <- the group
    //   roles        <- not persisted anywhere, so LFG_ROLE_NONE (see below)
    // randomDungeonID is genuinely lost. Its only consequence is the random-dungeon
    // completion bonus for a run a restart interrupted, which is not worth a schema change.
    if (!pGroup || !pGroup->isLFGGroup())
    {
        return;
    }

    ObjectGuid const groupGuid = pGroup->GetObjectGuid();

    // A group can hold several binds; the first one that resolves wins.
    if (GetGroupStatus(groupGuid))
    {
        return;
    }

    // Resolve the dungeon by (map, internal difficulty). LfgDungeons.dbc carries a RAW
    // client DifficultyID, so it has to be translated before comparing against the bind's
    // internal Difficulty -- comparing them directly is the key-space bug PR #81 fixed.
    uint32 dungeonId = 0;
    uint32 matches = 0;
    for (uint32 id = 0; id < sLfgDungeonsStore.GetNumRows(); ++id)
    {
        LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(id);
        if (!dungeon || uint32(dungeon->MapID) != mapId)
        {
            continue;
        }

        int32 const mode = ToInternalDifficulty(dungeon->DifficultyID);
        if (mode < 0 || uint32(mode) != difficulty)
        {
            continue;
        }

        ++matches;
        if (!dungeonId)
        {
            dungeonId = dungeon->ID;
        }
    }

    if (!dungeonId || matches != 1)
    {
        // Ambiguous or absent: log once and leave the group ALONE. Do not clear
        // GROUPTYPE_LFD and do not eject anyone -- a party that is merely missing its eye is
        // a great deal better off than one teleported out from under itself.
        sLog.outError("LFGMgr::RestoreDungeonGroup: group %s bound to map %u difficulty %u "
                      "resolves to %u LfgDungeons rows; leaving its LFG status unrestored.",
                      groupGuid.GetString().c_str(), mapId, difficulty, matches);
        return;
    }

    LFGGroupStatus status;
    status.state = LFG_STATE_IN_DUNGEON;
    status.dungeonID = dungeonId;
    status.madeProgress = (encountersMask != 0);
    status.randomDungeonID = 0;
    status.leaderGuid = pGroup->GetLeaderGuid();

    // Member GUIDs come from the persisted slots, NOT from GroupReference: no player is in
    // world yet at group-load time, so the live member list is empty.
    //
    // Roles are not persisted, so everyone comes back as PLAYER_ROLE_NONE. That is honest
    // rather than invented: the roles are only read for backfill role matching and the UI
    // role icons, and a wrong guess there would mis-fill a replacement slot.
    for (Group::MemberSlotList::const_iterator itr = pGroup->GetMemberSlots().begin();
         itr != pGroup->GetMemberSlots().end(); ++itr)
    {
        status.playerRoles[itr->guid] = uint8(PLAYER_ROLE_NONE);

        LFGPlayerStatus plrStatus;
        plrStatus.state = LFG_STATE_IN_DUNGEON;
        plrStatus.updateType = LFG_UPDATE_DEFAULT;
        plrStatus.dungeonList.insert(dungeonId);
        m_playerStatusMap[itr->guid] = plrStatus;
    }

    m_groupSet.insert(groupGuid);
    m_groupStatusMap[groupGuid] = status;

    sLog.outString("LFGMgr: restored dungeon group %s -- dungeon %u (map %u, difficulty %u), "
                   "%u member(s), progress %s.",
                   groupGuid.GetString().c_str(), dungeonId, mapId, difficulty,
                   uint32(pGroup->GetMemberSlots().size()),
                   status.madeProgress ? "yes" : "no");
}

bool LFGMgr::IsPlayerInLfgDungeon(Player* pPlayer)
{
    if (!pPlayer)
    {
        return false;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup || !pGroup->isLFGGroup())
    {
        return false;
    }

    LFGGroupStatus const* status = GetGroupStatus(pGroup->GetObjectGuid());
    if (!status)
    {
        return false;
    }

    LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(status->dungeonID);
    return dungeon && pPlayer->GetMapId() == uint32(dungeon->MapID);
}

/// Drop a departing player's own LFG state, and their vote if one is in flight.
///
/// Distinct from OnPlayerLeftDungeonGroup, which decides Deserter and returns early in
/// several cases -- a finished run, a run past its protected opening, a group with no
/// status. Those early returns are correct for Deserter and wrong for cleanup, so the
/// cleanup lives here and always runs.
///
/// Without it a voluntary leaver keeps whatever state they held. That matters most for
/// LFG_STATE_BOOT: ReleaseGroupLfgStatus clears the members of a group that DISBANDS, and
/// RemoveOldBoots clears the polled players when the group is GONE, but someone who simply
/// walks out mid-vote is in neither set. They kept LFG_STATE_BOOT across relog, and
/// HandleLfgGetStatusOpcode would hand their client a boot dialog for a vote that no
/// longer existed.
///
/// Their vote is withdrawn too. Leaving it in `answers` counts an absent player toward a
/// threshold they can no longer be persuaded to change.
void LFGMgr::OnPlayerLeftLfgGroup(Player* pPlayer, Group* pGroup)
{
    if (!pPlayer || !pGroup)
    {
        return;
    }

    ObjectGuid const plrGuid = pPlayer->GetObjectGuid();

    ObjectGuid const groupGuid = pGroup->GetObjectGuid();
    bootStatusMap::iterator bootIt = m_bootStatusMap.find(groupGuid);
    if (bootIt != m_bootStatusMap.end())
    {
        // If the leaver WAS the target, the vote has nothing left to decide.
        if (bootIt->second.playerVotedOn == plrGuid)
        {
            LFGBoot const boot = bootIt->second;
            FinishBootVote(groupGuid, pGroup, boot, true, true);
        }
        else
        {
            bootIt->second.answers.erase(plrGuid);
        }
    }

    SetPlayerState(plrGuid, LFG_STATE_NONE);
}

void LFGMgr::OnPlayerLeftDungeonGroup(Player* pPlayer)
{
    if (!pPlayer)
    {
        return;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup || !pGroup->isLFGGroup())
    {
        return;
    }

    LFGGroupStatus const* status = GetGroupStatus(pGroup->GetObjectGuid());
    if (!status || status->state == LFG_STATE_FINISHED_DUNGEON || status->madeProgress)
    {
        return;                 // run finished, or past the protected opening
    }

    // NOT gated on the player's current map.
    //
    // It used to require them to be standing on the dungeon's map, which handed out a
    // free abandon: teleport out through the dropdown -- which is allowed -- then leave
    // the group, and no Deserter. A player refused the entry teleport (dead, falling, in
    // a vehicle) was exempt for the same reason while still holding a place in the run.
    //
    // Membership of a live finder run is the right test and the server already keeps it:
    // GROUPTYPE_LFD is set when the finder forms the group, persisted in
    // `groups`.`groupType`, and never cleared. Where the deserter happens to be standing
    // when they quit is not the question.
    //
    // Accepting a proposal teleports the whole group in immediately, so "in the group but
    // never zoned in" is not a state a player can choose to sit in anyway.

    // Same null-map hazard as ApplyDungeonCooldown: casting on a player whose far teleport
    // has already begun dereferences a NULL Map inside Spell::CheckCast. Every current
    // caller runs this BEFORE the leave teleport, so this is a backstop -- and a loud one,
    // because if it ever fires a deserter walked away without the debuff.
    if (!pPlayer->IsInWorld())
    {
        sLog.outError("LFG: %s left dungeon %u mid-teleport -- Deserter NOT applied, "
                      "the caller must apply it before starting the teleport",
                      pPlayer->GetName(), status->dungeonID);
        return;
    }

    DEBUG_LOG("LFG: %s left dungeon %u before any encounter was credited -- Deserter",
              pPlayer->GetName(), status->dungeonID);
    pPlayer->CastSpell(pPlayer, LFG_DESERTER_SPELL, true);
}

void LFGMgr::ReleaseGroupLfgStatus(Group* pGroup)
{
    if (!pGroup)
    {
        return;
    }

    ObjectGuid const groupGuid = pGroup->GetObjectGuid();

    bootStatusMap::iterator boot = m_bootStatusMap.find(groupGuid);
    if (boot != m_bootStatusMap.end())
    {
        LFGBoot const terminal = boot->second;
        FinishBootVote(groupGuid, NULL, terminal, false, false);
    }

    // Reset the MEMBERS too, not just the group maps.
    //
    // m_playerStatusMap is keyed by player guid and outlives the group entirely: nothing
    // on the leave or disband path ever cleared it, because until vote kick landed no
    // player state survived long enough to matter. LFG_STATE_BOOT changed that. A boot
    // vote sets every member to LFG_STATE_BOOT, and if the group then disbands -- members
    // leaving one by one, or the last one quitting -- the group maps go and the player
    // states stay.
    //
    // They stay across relog, because the map is never cleared on login or logout, and
    // HandleLfgGetStatusOpcode ships whatever it finds: the client is handed
    // LFG_STATE_BOOT for a vote that no longer exists, in a group the player has left,
    // and can raise a boot dialog nothing can ever resolve. It only clears if the player
    // happens to re-queue, or the world restarts.
    //
    // Found by a whole-branch review; the range reviews could not see it, because the
    // commit that made the leak reachable and the code that leaks are far apart.
    for (Group::MemberSlotList::const_iterator itr = pGroup->GetMemberSlots().begin();
         itr != pGroup->GetMemberSlots().end(); ++itr)
    {
        SetPlayerState(itr->guid, LFG_STATE_NONE);
    }

    m_groupStatusMap.erase(groupGuid);
    m_groupSet.erase(groupGuid);
}

uint32 LFGMgr::GetGroupRandomDungeonEntry(ObjectGuid groupGuid)
{
    LFGGroupStatus const* status = GetGroupStatus(groupGuid);
    return (status && status->randomDungeonID) ? GetDungeonEntry(status->randomDungeonID) : 0;
}

LFGState LFGMgr::GetGroupLfgState(ObjectGuid groupGuid)
{
    LFGGroupStatus const* status = GetGroupStatus(groupGuid);
    return status ? status->state : LFG_STATE_NONE;
}

uint32 LFGMgr::GetDungeonEntry(uint32 ID)
{
    LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(ID);
    if (dungeon)
    {
        return dungeon->Entry();
    }
    else
    {
        return 0;
    }
}

uint8 LFGMgr::GetDungeonCategory(uint32 ID)
{
    LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(ID);
    return dungeon ? uint8(dungeon->Subtype) : 0;
}
