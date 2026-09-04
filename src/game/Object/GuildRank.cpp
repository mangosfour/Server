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

#include "Guild.h"
#include "Database/DatabaseEnv.h"
#include "ObjectMgr.h"
#include "Log.h"

/**
 * @file GuildRank.cpp
 * @brief Cohesion split of Guild.cpp -- guild rank create/delete/reorder and
 * rank name/rights management. Same Guild class; no behaviour change. CMake
 * file(GLOB Object/*.cpp) picks this file up automatically; Guild.h is unchanged.
 */

/**
 * @brief Creates and persists a new guild rank.
 *
 * @param name_ The rank name.
 * @param rights The rights mask for the new rank.
 */
void Guild::CreateRank(std::string name_, uint32 rights)
{
    if (m_Ranks.size() >= GUILD_RANKS_MAX_COUNT)
    {
        return;
    }

    // ranks are sequence 0,1,2,... where 0 means guildmaster
    uint32 new_rank_id = m_Ranks.size();

    std::string dbName = name_;
    CharacterDatabase.escape_string(dbName);
    AddRank(name_, rights, 0);

    // existing records in db should be deleted before calling this procedure and m_PurchasedTabs must be loaded already

    for (uint32 i = 0; i < uint32(GetPurchasedTabs()); ++i)
    {
        // create bank rights with 0
        CharacterDatabase.PExecute("INSERT INTO `guild_bank_right` (`guildid`,`TabId`,`rid`) VALUES ('%u','%u','%u')", m_Id, i, new_rank_id);
    }
    CharacterDatabase.PExecute("INSERT INTO `guild_rank` (`guildid`,`rid`,`rname`,`rights`) VALUES ('%u', '%u', '%s', '%u')", m_Id, new_rank_id, dbName.c_str(), rights);
}

/**
 * @brief Adds a rank to the in-memory rank list.
 *
 * @param name_ The rank name.
 * @param rights The rights mask.
 */
void Guild::AddRank(const std::string& name_, uint32 rights, uint32 money)
{
    m_Ranks.push_back(RankInfo(name_, rights, money));
}

/**
 * @brief Deletes the lowest guild rank if allowed.
 */
bool Guild::DelRank(uint32 rankId)
{
    if (rankId >= m_Ranks.size())
    {
        return false;
    }

    // client won't allow to have less than GUILD_RANKS_MIN_COUNT ranks in guild
    if (m_Ranks.size() <= GUILD_RANKS_MIN_COUNT || rankId < GUILD_RANKS_MIN_COUNT)
    {
        return false;
    }

    if (HasMembersWithRank(rankId))
    {
        return false;
    }

    m_Ranks.erase(m_Ranks.begin() + rankId);

    // Rank definitions and their members must move together.
    for (MemberList::iterator itr = members.begin(); itr != members.end(); ++itr)
    {
        if (itr->second.RankId > rankId)
        {
            --itr->second.RankId;

            if (Player* member = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, itr->first), false))
            {
                member->SetRank(itr->second.RankId);
            }
        }
    }

    for (GuildEventLog::iterator itr = m_GuildEventLog.begin(); itr != m_GuildEventLog.end(); ++itr)
    {
        if (itr->NewRank > rankId)
        {
            --itr->NewRank;
        }
    }

    // delete lowest guild_rank
    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM `guild_rank` WHERE `rid` ='%u' AND `guildid` ='%u'", rankId, m_Id);
    CharacterDatabase.PExecute("DELETE FROM `guild_bank_right` WHERE `rid` ='%u' AND `guildid` ='%u'", rankId, m_Id);
    CharacterDatabase.PExecute("UPDATE `guild_rank` SET `rid` = `rid` - 1 WHERE `rid` > '%u' AND `guildid` ='%u'", rankId, m_Id);
    CharacterDatabase.PExecute("UPDATE `guild_bank_right` SET `rid` = `rid` - 1 WHERE `rid` > '%u' AND `guildid` ='%u'", rankId, m_Id);
    CharacterDatabase.PExecute("UPDATE `guild_member` SET `rank` = `rank` - 1 WHERE `rank` > '%u' AND `guildid` ='%u'", rankId, m_Id);
    CharacterDatabase.PExecute("UPDATE `guild_eventlog` SET `NewRank` = `NewRank` - 1 WHERE `NewRank` > '%u' AND `guildid` ='%u'", rankId, m_Id);
    CharacterDatabase.CommitTransaction();

    return true;
}

bool Guild::SwitchRank(uint32 rankId, bool up)
{
    if (rankId >= m_Ranks.size())
    {
        return false;
    }

    if (rankId == GR_GUILDMASTER)
    {
        DEBUG_LOG("Guild::SwitchRank: guild %u refused a swap involving the guildmaster rank", m_Id);
        return false;
    }

    if (rankId == GetLowestRank() && !up)
    {
        return false;
    }

    uint32 otherRankId = rankId + (up ? -1 : 1);

    if (otherRankId == GR_GUILDMASTER)
    {
        DEBUG_LOG("Guild::SwitchRank: guild %u refused a swap involving the guildmaster rank", m_Id);
        return false;
    }
    DEBUG_LOG("rank: %u otherrank %u", rankId, otherRankId);

    std::string rankName = m_Ranks[rankId].Name;
    std::string otherRankName = m_Ranks[otherRankId].Name;
    CharacterDatabase.escape_string(rankName);
    CharacterDatabase.escape_string(otherRankName);

    CharacterDatabase.BeginTransaction();
    std::swap(m_Ranks[rankId], m_Ranks[otherRankId]);
    for (uint32 i = 0; i < uint32(GetPurchasedTabs()); ++i)
    {
        CharacterDatabase.PExecute("REPLACE INTO guild_bank_right (guildid,TabId,rid,gbright,SlotPerDay) "
            "VALUES ('%u','%u','%u','%u','%u')", m_Id, i, rankId, m_Ranks[rankId].TabRight[i], m_Ranks[rankId].TabSlotPerDay[i]);
        CharacterDatabase.PExecute("REPLACE INTO guild_bank_right (guildid,TabId,rid,gbright,SlotPerDay) "
            "VALUES ('%u','%u','%u','%u','%u')", m_Id, i, otherRankId, m_Ranks[otherRankId].TabRight[i], m_Ranks[otherRankId].TabSlotPerDay[i]);
    }

    CharacterDatabase.PExecute("REPLACE INTO guild_rank (guildid,rid,rname,rights,BankMoneyPerDay) "
        "VALUES ('%u', '%u', '%s', '%u', '%u')", m_Id, rankId, otherRankName.c_str(),m_Ranks[rankId].Rights,m_Ranks[rankId].BankMoneyPerDay);
    CharacterDatabase.PExecute("REPLACE INTO guild_rank (guildid,rid,rname,rights,BankMoneyPerDay) "
        "VALUES ('%u', '%u', '%s', '%u', '%u')", m_Id, otherRankId, rankName.c_str(),m_Ranks[otherRankId].Rights,m_Ranks[otherRankId].BankMoneyPerDay);

    for (MemberList::iterator itr = members.begin(); itr != members.end(); ++itr)
        if (itr->second.RankId == rankId)
        {
            itr->second.ChangeRank(otherRankId);
        }
        else if (itr->second.RankId == otherRankId)
        {
            itr->second.ChangeRank(rankId);
        }

    for (GuildEventLog::iterator itr = m_GuildEventLog.begin(); itr != m_GuildEventLog.end(); ++itr)
    {
        if (itr->NewRank == rankId)
        {
            itr->NewRank = otherRankId;
        }
        else if (itr->NewRank == otherRankId)
        {
            itr->NewRank = rankId;
        }
    }

    CharacterDatabase.PExecute("UPDATE `guild_eventlog` SET `NewRank` = CASE `NewRank` "
        "WHEN '%u' THEN '%u' WHEN '%u' THEN '%u' ELSE `NewRank` END "
        "WHERE `guildid` = '%u' AND `NewRank` IN ('%u', '%u')",
        rankId, otherRankId, otherRankId, rankId, m_Id, rankId, otherRankId);

    CharacterDatabase.CommitTransaction();

    return true;
}

/**
 * @brief Gets the name of a guild rank.
 *
 * @param rankId The rank identifier.
 * @return The rank name, or a placeholder if the rank is invalid.
 */
std::string Guild::GetRankName(uint32 rankId)
{
    if (rankId >= m_Ranks.size())
    {
        return "<unknown>";
    }

    return m_Ranks[rankId].Name;
}

/**
 * @brief Gets the rights mask for a guild rank.
 *
 * @param rankId The rank identifier.
 * @return The rights mask for the rank.
 */
uint32 Guild::GetRankRights(uint32 rankId)
{
    if (rankId >= m_Ranks.size())
    {
        return 0;
    }

    return m_Ranks[rankId].Rights;
}

/**
 * @brief Renames a guild rank.
 *
 * @param rankId The rank identifier.
 * @param name_ The new rank name.
 */
void Guild::SetRankName(uint32 rankId, std::string name_)
{
    if (rankId >= m_Ranks.size())
    {
        return;
    }

    std::string dbName = name_;
    CharacterDatabase.escape_string(dbName);
    m_Ranks[rankId].Name = name_;

    CharacterDatabase.PExecute("UPDATE `guild_rank` SET `rname`='%s' WHERE `rid`='%u' AND `guildid`='%u'", dbName.c_str(), rankId, m_Id);
}

/**
 * @brief Updates the rights mask for a guild rank.
 *
 * @param rankId The rank identifier.
 * @param rights The new rights mask.
 */
void Guild::SetRankRights(uint32 rankId, uint32 rights)
{
    if (rankId >= m_Ranks.size())
    {
        return;
    }

    m_Ranks[rankId].Rights = rights;

    CharacterDatabase.PExecute("UPDATE `guild_rank` SET `rights`='%u' WHERE `rid`='%u' AND `guildid`='%u'", rights, rankId, m_Id);
}
