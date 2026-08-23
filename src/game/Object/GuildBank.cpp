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
#include "ObjectAccessor.h"
#include "World.h"
#include "Item.h"
#include "Player.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "Util.h"
#include "Log.h"
#include "MopGuildBankPackets.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

/**
 * @file GuildBank.cpp
 * @brief Cohesion split of Guild.cpp -- guild bank storage: tab/content
 * display, item store/withdraw/swap, money and rights tracking, and bank
 * event logging. Same Guild class; no behaviour change. CMake
 * file(GLOB Object/*.cpp) picks this file up automatically; Guild.h is unchanged.
 */

// *************************************************
// Guild Bank part
// *************************************************
// Bank content related
namespace
{
    bool SendGuildBankList(WorldSession* session,
        MopGuildBankPackets::GuildBankList const& list)
    {
        WorldPacket data(SMSG_GUILD_BANK_LIST, 1200);
        if (!MopGuildBankPackets::BuildListBody(data, list))
        {
            sLog.outError("GuildBank: failed to build a bounded 18414 bank list");
            return false;
        }

        session->SendPacket(&data);
        return true;
    }
}

void Guild::DisplayGuildBankContent(WorldSession* session, uint8 TabId,
    bool sendAllSlots, bool withTabInfo)
{
    bool const emptyMetadata = GetPurchasedTabs() == 0 && withTabInfo && TabId == 0;
    if (TabId >= GetPurchasedTabs() && !emptyMetadata)
    {
        return;
    }

    bool const canViewTab = !emptyMetadata &&
        IsMemberHaveRights(session->GetPlayer()->GetGUIDLow(), TabId,
            GUILD_BANK_RIGHT_VIEW_TAB);
    if (!canViewTab && !withTabInfo)
    {
        return;
    }

    MopGuildBankPackets::GuildBankList list;
    list.tabId = TabId;
    list.money = GetGuildBankMoney();
    list.withdrawRemaining = emptyMetadata ? 0 : int32(GetMemberSlotWithdrawRem(
        session->GetPlayer()->GetGUIDLow(), TabId));
    list.fullUpdate = sendAllSlots && !emptyMetadata;

    if (withTabInfo)
    {
        for (uint8 i = 0; i < GetPurchasedTabs(); ++i)
        {
            MopGuildBankPackets::TabRecord tab;
            tab.index = i;
            tab.icon = MopGuildBankPackets::TruncateUtf8(m_TabListMap[i]->Icon,
                MopGuildBankPackets::MAX_TAB_ICON_BYTES);
            tab.name = MopGuildBankPackets::TruncateUtf8(m_TabListMap[i]->Name,
                MopGuildBankPackets::MAX_TAB_NAME_BYTES);
            list.tabs.push_back(tab);
        }
    }

    if (sendAllSlots && canViewTab)
    {
        GuildBankTab const* tab = m_TabListMap[TabId];
        for (int32 slot = 0; slot < GUILD_BANK_MAX_SLOTS; ++slot)
        {
            if (tab->Slots[slot] && !AppendDisplayGuildBankSlot(list, tab, slot))
            {
                return;
            }
        }
    }

    if (!SendGuildBankList(session, list))
    {
        return;
    }

    DEBUG_LOG("WORLD: Sent (SMSG_GUILD_BANK_LIST), tabid %u itemCount %u",
        TabId, uint32(list.items.size()));
}

void Guild::DisplayGuildBankContentUpdate(uint8 TabId, int32 slot1, int32 slot2)
{
    if (TabId >= GetPurchasedTabs() || slot1 < 0 || slot1 >= GUILD_BANK_MAX_SLOTS ||
            (slot2 != -1 && (slot2 < 0 || slot2 >= GUILD_BANK_MAX_SLOTS)))
    {
        return;
    }

    GuildBankTab const* tab = m_TabListMap[TabId];
    if (slot2 != -1 && slot1 > slot2)
    {
        std::swap(slot1, slot2);
    }

    MopGuildBankPackets::GuildBankList list;
    list.tabId = TabId;
    list.money = GetGuildBankMoney();
    if (!AppendDisplayGuildBankSlot(list, tab, slot1) ||
            (slot2 != -1 && slot2 != slot1 &&
             !AppendDisplayGuildBankSlot(list, tab, slot2)))
    {
        return;
    }

    for (MemberList::const_iterator itr = members.begin(); itr != members.end(); ++itr)
    {
        Player* player = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, itr->first));
        if (!player)
        {
            continue;
        }

        if (!IsMemberHaveRights(itr->first, TabId, GUILD_BANK_RIGHT_VIEW_TAB))
        {
            continue;
        }

        list.withdrawRemaining = int32(GetMemberSlotWithdrawRem(player->GetGUIDLow(), TabId));
        SendGuildBankList(player->GetSession(), list);
    }

    DEBUG_LOG("WORLD: Sent (SMSG_GUILD_BANK_LIST)");
}

void Guild::DisplayGuildBankContentUpdate(uint8 TabId, GuildItemPosCountVec const& slots)
{
    if (TabId >= GetPurchasedTabs() || slots.size() > GUILD_BANK_MAX_SLOTS)
    {
        return;
    }

    GuildBankTab const* tab = m_TabListMap[TabId];
    std::vector<int32> orderedSlots;
    orderedSlots.reserve(slots.size());
    for (GuildItemPosCount const& slot : slots)
    {
        if (slot.Slot >= GUILD_BANK_MAX_SLOTS)
        {
            return;
        }
        orderedSlots.push_back(slot.Slot);
    }
    std::sort(orderedSlots.begin(), orderedSlots.end());
    if (std::adjacent_find(orderedSlots.begin(), orderedSlots.end()) != orderedSlots.end())
    {
        return;
    }

    MopGuildBankPackets::GuildBankList list;
    list.tabId = TabId;
    list.money = GetGuildBankMoney();
    for (int32 slot : orderedSlots)
    {
        if (!AppendDisplayGuildBankSlot(list, tab, slot))
        {
            return;
        }
    }

    for (MemberList::const_iterator itr = members.begin(); itr != members.end(); ++itr)
    {
        Player* player = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, itr->first));
        if (!player)
        {
            continue;
        }

        if (!IsMemberHaveRights(itr->first, TabId, GUILD_BANK_RIGHT_VIEW_TAB))
        {
            continue;
        }

        list.withdrawRemaining = int32(GetMemberSlotWithdrawRem(player->GetGUIDLow(), TabId));
        SendGuildBankList(player->GetSession(), list);
    }

    DEBUG_LOG("WORLD: Sent (SMSG_GUILD_BANK_LIST)");
}

/// Every item mutation below moves the item in memory first and writes the rows
/// inside a transaction. All nine of those commits used to throw their result
/// away, and a failure here is not benign: the item has already moved in memory
/// and its Item state is already back to unchanged, so a rollback leaves the
/// bank holding one arrangement and the database another, with nothing to say so.
///
/// It must be CommitTransactionDirect. The plain CommitTransaction only queues
/// once the world has loaded -- AllowAsyncTransactions is on from Master.cpp --
/// handing the statements to the delay thread and returning true before MySQL
/// has seen them, and the delay thread discards the result. A first version of
/// this helper called it and claimed to report whether the write landed; it
/// could not have, and would have reported success for every failure there is.
///
/// A false return is ambiguous in the usual way: the statements may have rolled
/// back, or the COMMIT may have applied with only its result unreadable. Unlike
/// the money paths there is no small set of balances to re-read -- and as the
/// tab-purchase path already reasons, correcting item state in memory is the
/// part that cannot be done safely, since undoing a mutation whose commit
/// actually landed creates the inverse phantom. So this does not guess.
///
/// Marking the bank untrusted is necessary but NOT sufficient on its own. For a
/// withdrawal the player is already holding the item in memory, and that flag
/// only stops further BANK packets -- it does nothing about using, trading or
/// mailing what they now hold. So the session is quarantined the way
/// WorldSession::SuppressCharacterSave documents: refuse to persist the
/// character's in-memory state, then disconnect, so the reconnect loads whatever
/// the database actually holds. Losing unsaved progress is the cheaper error.
bool Guild::CommitBankMutation(Player* pl, char const* context)
{
    if (CharacterDatabase.CommitTransactionDirect())
    {
        return true;
    }

    MarkBankStateUntrusted();

    if (WorldSession* session = pl ? pl->GetSession() : NULL)
    {
        session->SuppressCharacterSave();
        sLog.outError("Guild::%s: commit could not be confirmed for player %u and guild %u. "
            "Items may have moved in memory without reaching the database, so the bank is "
            "untrusted until reload and this character's state is discarded rather than saved.",
            context, pl->GetGUIDLow(), m_Id);
        session->KickPlayer();
    }
    else
    {
        sLog.outError("Guild::%s: commit could not be confirmed for guild %u and no session "
            "was available to quarantine; the bank is untrusted until it is reloaded.",
            context, m_Id);
    }

    return false;
}

Item* Guild::GetItem(uint8 TabId, uint8 SlotId)
{
    if (TabId >= GetPurchasedTabs() || SlotId >= GUILD_BANK_MAX_SLOTS)
    {
        return NULL;
    }
    return m_TabListMap[TabId]->Slots[SlotId];
}

// *************************************************
// Tab related

void Guild::DisplayGuildBankTabsInfo(WorldSession* session, uint8 TabId)
{
    DisplayGuildBankContent(session, TabId, true, true);

    DEBUG_LOG("WORLD: Sent SMSG_GUILD_BANK_LIST (Guild::DisplayGuildBankTabsInfo)");
}

void Guild::CreateNewBankTab()
{
    if (GetPurchasedTabs() >= GUILD_BANK_MAX_TABS)
    {
        return;
    }

    uint32 tabId = GetPurchasedTabs();                      // next free id
    m_TabListMap.push_back(new GuildBankTab);

    // Queue only -- the CALLER owns the transaction. This used to open its own,
    // which was worse than redundant: Database::BeginTransaction calls init() on
    // the per-thread storage, and init() opens with MANGOS_ASSERT(!m_pTrans). So
    // nesting aborts the server where that assert is live, and silently discards
    // whatever the outer had already queued where NDEBUG compiles it out. Buying
    // a tab has to be one transaction with the gold it costs, so the begin and
    // the commit belong to the handler.
    CharacterDatabase.PExecute("DELETE FROM `guild_bank_tab` WHERE `guildid`='%u' AND `TabId`='%u'", m_Id, tabId);
    CharacterDatabase.PExecute("INSERT INTO `guild_bank_tab` (`guildid`,`TabId`) VALUES ('%u','%u')", m_Id, tabId);
}

void Guild::SetGuildBankTabInfo(uint8 TabId, std::string Name, std::string Icon)
{
    // TabId reaches here straight off the wire (CMSG_GUILD_BANK_UPDATE_TAB), and
    // the only thing standing between it and this operator[] is the caller's
    // range check. Guard in-function too, as GetBankRights below does (it bounds
    // against the GUILD_BANK_MAX_TABS constant rather than a container size, so
    // the parallel is the practice, not the bound). An out-of-range TabId is not
    // a bad name: operator[] past the end is undefined behaviour, and the code
    // below both dereferences the pointer it yields and assigns Name and Icon
    // through it -- so the consequence is unbounded, not a mere stray read.
    if (TabId >= m_TabListMap.size())
    {
        return;
    }

    if (m_TabListMap[TabId]->Name == Name && m_TabListMap[TabId]->Icon == Icon)
    {
        return;
    }

    m_TabListMap[TabId]->Name = Name;
    m_TabListMap[TabId]->Icon = Icon;

    CharacterDatabase.escape_string(Name);
    CharacterDatabase.escape_string(Icon);
    CharacterDatabase.PExecute("UPDATE `guild_bank_tab` SET `TabName`='%s',`TabIcon`='%s' WHERE `guildid`='%u' AND `TabId`='%u'", Name.c_str(), Icon.c_str(), m_Id, uint32(TabId));
}

uint32 Guild::GetBankRights(uint32 rankId, uint8 TabId) const
{
    if (rankId >= m_Ranks.size() || TabId >= GUILD_BANK_MAX_TABS)
    {
        return 0;
    }

    return m_Ranks[rankId].TabRight[TabId];
}

// *************************************************
// Guild bank loading related

// This load should be called on startup only
void Guild::LoadGuildBankFromDB()
{
    //                                                      0        1          2          3
    QueryResult* result = CharacterDatabase.PQuery("SELECT `TabId`, `TabName`, `TabIcon`, `TabText` FROM `guild_bank_tab` WHERE `guildid`='%u' ORDER BY `TabId`", m_Id);
    if (!result)
    {
        m_TabListMap.clear();
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint8 tabId = fields[0].GetUInt8();
        if (tabId >= GetPurchasedTabs())
        {
            sLog.outError("Table `guild_bank_tab` have not purchased tab %u for guild %u, skipped", tabId, m_Id);
            continue;
        }

        GuildBankTab* NewTab = new GuildBankTab;

        NewTab->Name = fields[1].GetCppString();
        NewTab->Icon = fields[2].GetCppString();
        NewTab->Text = fields[3].GetCppString();

        m_TabListMap[tabId] = NewTab;
    }
    while (result->NextRow());

    delete result;

    // data needs to be at first place for Item::LoadFromDB
    //                                        0     1     2      3       4          5
    result = CharacterDatabase.PQuery("SELECT `data`, `text`, `TabId`, `SlotId`, `item_guid`, `item_entry` FROM `guild_bank_item` JOIN `item_instance` ON `item_guid` = `guid` WHERE `guildid`='%u' ORDER BY `TabId`", m_Id);
    if (!result)
    {
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint8 TabId = fields[2].GetUInt8();
        uint8 SlotId = fields[3].GetUInt8();
        uint32 ItemGuid = fields[4].GetUInt32();
        uint32 ItemEntry = fields[5].GetUInt32();

        if (TabId >= GetPurchasedTabs())
        {
            sLog.outError("Guild::LoadGuildBankFromDB: Invalid tab for item (GUID: %u id: #%u) in guild bank, skipped.", ItemGuid, ItemEntry);
            continue;
        }

        if (SlotId >= GUILD_BANK_MAX_SLOTS)
        {
            sLog.outError("Guild::LoadGuildBankFromDB: Invalid slot for item (GUID: %u id: #%u) in guild bank, skipped.", ItemGuid, ItemEntry);
            continue;
        }

        ItemPrototype const* proto = ObjectMgr::GetItemPrototype(ItemEntry);

        if (!proto)
        {
            sLog.outError("Guild::LoadGuildBankFromDB: Unknown item (GUID: %u id: #%u) in guild bank, skipped.", ItemGuid, ItemEntry);
            continue;
        }

        Item* pItem = NewItemOrBag(proto);
        if (!pItem->LoadFromDB(ItemGuid, fields))
        {
            CharacterDatabase.PExecute("DELETE FROM `guild_bank_item` WHERE `guildid`='%u' AND `TabId`='%u' AND `SlotId`='%u'", m_Id, uint32(TabId), uint32(SlotId));
            sLog.outError("Item GUID %u not found in item_instance, deleting from Guild Bank!", ItemGuid);
            delete pItem;
            continue;
        }

        pItem->AddToWorld();
        m_TabListMap[TabId]->Slots[SlotId] = pItem;
    }
    while (result->NextRow());

    delete result;
}

// *************************************************
// Money deposit/withdraw related

void Guild::SendMoneyInfo(WorldSession* session, uint32 LowGuid)
{
    WorldPacket data(SMSG_GUILD_BANK_MONEY_WITHDRAWN, 8);
    data << uint64(GetMemberMoneyWithdrawRem(LowGuid));
    session->SendPacket(&data);
    DEBUG_LOG("WORLD: Sent SMSG_GUILD_BANK_MONEY_WITHDRAWN");
}

bool Guild::MemberMoneyWithdraw(uint64 amount, uint32 LowGuid)
{
    uint64 MoneyWithDrawRight = GetMemberMoneyWithdrawRem(LowGuid);

    if (MoneyWithDrawRight < amount || GetGuildBankMoney() < amount)
    {
        return false;
    }

    SetBankMoney(GetGuildBankMoney() - amount);

    if (MoneyWithDrawRight < WITHDRAW_MONEY_UNLIMITED)
    {
        MemberList::iterator itr = members.find(LowGuid);
        if (itr == members.end())
        {
            return false;
        }
        itr->second.BankRemMoney -= amount;
        CharacterDatabase.PExecute("UPDATE `guild_member` SET `BankRemMoney`='%u' WHERE `guildid`='%u' AND `guid`='%u'",
                                   itr->second.BankRemMoney, m_Id, LowGuid);
    }

#ifdef ENABLE_ELUNA
    Player* player = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, LowGuid));
    if (Eluna* e = sWorld.GetEluna())
    {
        e->OnMemberWitdrawMoney(this, player, amount, false); // IsRepair not a part of Mangos, implement?
    }
#endif

    return true;
}

void Guild::SetBankMoney(int64 money)
{
    if (money < 0)                                          // I don't know how this happens, it does!!
    {
        money = 0;
    }
    m_GuildBankMoney = money;

    CharacterDatabase.PExecute("UPDATE `guild` SET `BankMoney`='" UI64FMTD "' WHERE `guildid`='%u'", money, m_Id);
}

// *************************************************
// Item per day and money per day related

bool Guild::MemberItemWithdraw(uint8 TabId, uint32 LowGuid)
{
    uint32 SlotsWithDrawRight = GetMemberSlotWithdrawRem(LowGuid, TabId);

    if (SlotsWithDrawRight == 0)
    {
        return false;
    }

    if (SlotsWithDrawRight < WITHDRAW_SLOT_UNLIMITED)
    {
        MemberList::iterator itr = members.find(LowGuid);
        if (itr == members.end())
        {
            return false;
        }
        --itr->second.BankRemSlotsTab[TabId];
        CharacterDatabase.PExecute("UPDATE `guild_member` SET `BankRemSlotsTab%u`='%u' WHERE `guildid`='%u' AND `guid`='%u'",
                                   uint32(TabId), itr->second.BankRemSlotsTab[TabId], m_Id, LowGuid);
    }
    return true;
}

bool Guild::IsMemberHaveRights(uint32 LowGuid, uint8 TabId, uint32 rights) const
{
    MemberList::const_iterator itr = members.find(LowGuid);
    if (itr == members.end())
    {
        return false;
    }

    if (itr->second.RankId == GR_GUILDMASTER)
    {
        return true;
    }

    return (GetBankRights(itr->second.RankId, TabId) & rights) == rights;
}

uint32 Guild::GetMemberSlotWithdrawRem(uint32 LowGuid, uint8 TabId)
{
    MemberList::iterator itr = members.find(LowGuid);
    if (itr == members.end())
    {
        return 0;
    }

    MemberSlot& member = itr->second;
    if (member.RankId == GR_GUILDMASTER)
    {
        return WITHDRAW_SLOT_UNLIMITED;
    }

    if ((GetBankRights(member.RankId, TabId) & GUILD_BANK_RIGHT_VIEW_TAB) != GUILD_BANK_RIGHT_VIEW_TAB)
    {
        return 0;
    }

    uint32 curTime = uint32(time(NULL) / MINUTE);
    if (curTime - member.BankResetTimeTab[TabId] >= 24 * HOUR / MINUTE)
    {
        member.BankResetTimeTab[TabId] = curTime;
        member.BankRemSlotsTab[TabId] = GetBankSlotPerDay(member.RankId, TabId);
        CharacterDatabase.PExecute("UPDATE `guild_member` SET `BankResetTimeTab%u`='%u', `BankRemSlotsTab%u`='%u' WHERE `guildid`='%u' AND `guid`='%u'",
                                   uint32(TabId), member.BankResetTimeTab[TabId], uint32(TabId), member.BankRemSlotsTab[TabId], m_Id, LowGuid);
    }
    return member.BankRemSlotsTab[TabId];
}

uint64 Guild::GetMemberMoneyWithdrawRem(uint32 LowGuid)
{
    MemberList::iterator itr = members.find(LowGuid);
    if (itr == members.end())
    {
        return 0;
    }

    MemberSlot& member = itr->second;
    if (member.RankId == GR_GUILDMASTER)
    {
        return WITHDRAW_MONEY_UNLIMITED;
    }

    uint32 curTime = uint32(time(NULL) / MINUTE);           // minutes
    // 24 hours
    if (curTime > member.BankResetTimeMoney + 24 * HOUR / MINUTE)
    {
        member.BankResetTimeMoney = curTime;
        member.BankRemMoney = GetBankMoneyPerDay(member.RankId);
        CharacterDatabase.PExecute("UPDATE `guild_member` SET `BankResetTimeMoney`='%u', `BankRemMoney`='%u' WHERE `guildid`='%u' AND `guid`='%u'",
                                   member.BankResetTimeMoney, member.BankRemMoney, m_Id, LowGuid);
    }
    return member.BankRemMoney;
}

void Guild::SetBankMoneyPerDay(uint32 rankId, uint32 money)
{
    if (rankId >= m_Ranks.size())
    {
        return;
    }

    if (rankId == GR_GUILDMASTER)
    {
        money = (uint32)WITHDRAW_MONEY_UNLIMITED;
    }

    m_Ranks[rankId].BankMoneyPerDay = money;

    for (MemberList::iterator itr = members.begin(); itr != members.end(); ++itr)
    {
        MemberSlot& member = itr->second;
        if (member.RankId == rankId)
        {
            member.BankResetTimeMoney = 0;
        }
    }

    CharacterDatabase.PExecute("UPDATE `guild_rank` SET `BankMoneyPerDay`='%u' WHERE `rid`='%u' AND `guildid`='%u'", money, rankId, m_Id);
    CharacterDatabase.PExecute("UPDATE `guild_member` SET `BankResetTimeMoney`='0' WHERE `guildid`='%u' AND `rank`='%u'", m_Id, rankId);
}

void Guild::SetBankRightsAndSlots(uint32 rankId, uint8 TabId, uint32 right, uint32 nbSlots, bool db)
{
    if (rankId >= m_Ranks.size() || TabId >= GetPurchasedTabs())
    {
        // TODO remove next line, It is there just to repair existing bug in deleting guild rank
        CharacterDatabase.PExecute("DELETE FROM `guild_bank_right` WHERE `guildid`='%u' AND `rid`='%u' AND `TabId`='%u'", m_Id, rankId, TabId);
        return;
    }

    if (rankId == GR_GUILDMASTER)
    {
        nbSlots = WITHDRAW_SLOT_UNLIMITED;
        right = GUILD_BANK_RIGHT_FULL;
    }

    m_Ranks[rankId].TabSlotPerDay[TabId] = nbSlots;
    m_Ranks[rankId].TabRight[TabId] = right;

    if (db)
    {
        for (MemberList::iterator itr = members.begin(); itr != members.end(); ++itr)
            if (itr->second.RankId == rankId)
                for (int i = 0; i < GUILD_BANK_MAX_TABS; ++i)
                {
                    itr->second.BankResetTimeTab[i] = 0;
                }

        CharacterDatabase.PExecute("DELETE FROM `guild_bank_right` WHERE `guildid`='%u' AND `TabId`='%u' AND `rid`='%u'", m_Id, uint32(TabId), rankId);
        CharacterDatabase.PExecute("INSERT INTO `guild_bank_right` (`guildid`,`TabId`,`rid`,`gbright`,`SlotPerDay`) VALUES "
                                   "('%u','%u','%u','%u','%u')", m_Id, uint32(TabId), rankId, m_Ranks[rankId].TabRight[TabId], m_Ranks[rankId].TabSlotPerDay[TabId]);
        CharacterDatabase.PExecute("UPDATE `guild_member` SET `BankResetTimeTab%u`='0' WHERE `guildid`='%u' AND `rank`='%u'", uint32(TabId), m_Id, rankId);
    }
}

uint32 Guild::GetBankMoneyPerDay(uint32 rankId)
{
    if (rankId >= m_Ranks.size())
    {
        return 0;
    }

    if (rankId == GR_GUILDMASTER)
    {
        return (uint32)WITHDRAW_MONEY_UNLIMITED;
    }
    return m_Ranks[rankId].BankMoneyPerDay;
}

uint32 Guild::GetBankSlotPerDay(uint32 rankId, uint8 TabId)
{
    if (rankId >= m_Ranks.size() || TabId >= GUILD_BANK_MAX_TABS)
    {
        return 0;
    }

    if (rankId == GR_GUILDMASTER)
    {
        return WITHDRAW_SLOT_UNLIMITED;
    }
    return m_Ranks[rankId].TabSlotPerDay[TabId];
}

// *************************************************
// Rights per day related

bool Guild::LoadBankRightsFromDB(QueryResult* guildBankTabRightsResult)
{
    if (!guildBankTabRightsResult)
    {
        return true;
    }

    do
    {
        Field* fields      = guildBankTabRightsResult->Fetch();
        // prevent crash when all rights in result are already processed
        if (!fields)
        {
            break;
        }
        uint32 guildId     = fields[0].GetUInt32();
        if (guildId < m_Id)
        {
            // there is in table guild_bank_right record which doesn't have guildid in guild table, report error
            sLog.outErrorDb("Guild %u does not exist but it has a record in guild_bank_right table, deleting it!", guildId);
            CharacterDatabase.PExecute("DELETE FROM `guild_bank_right` WHERE `guildid` = '%u'", guildId);
            continue;
        }

        if (guildId > m_Id)
            // we loaded all ranks for this guild bank already, break cycle
            break;
        uint8 TabId        = fields[1].GetUInt8();
        uint32 rankId      = fields[2].GetUInt32();
        // 32-bit: the column is int unsigned and the client permits values up to
        // 100000, which a 16-bit read silently folded to 34464 on every restart.
        uint32 right       = fields[3].GetUInt32();
        uint32 SlotPerDay  = fields[4].GetUInt32();

        SetBankRightsAndSlots(rankId, TabId, right, SlotPerDay, false);
    }
    while (guildBankTabRightsResult->NextRow());

    return true;
}

// *************************************************
// Bank log related

void Guild::LoadGuildBankEventLogFromDB()
{
    // Money log is in TabId = GUILD_BANK_MONEY_LOGS_TAB

    // uint32 configCount = sWorld.getConfig(CONFIG_UINT32_GUILD_BANK_EVENT_LOG_COUNT);
    // cycle through all purchased guild bank item tabs
    for (uint32 tabId = 0; tabId < uint32(GetPurchasedTabs()); ++tabId)
    {
        //                                                      0          1            2             3              4                 5            6
        QueryResult* result = CharacterDatabase.PQuery("SELECT `LogGuid`, `EventType`, `PlayerGuid`, `ItemOrMoney`, `ItemStackCount`, `DestTabId`, `TimeStamp` FROM `guild_bank_eventlog` WHERE `guildid`='%u' AND `TabId`='%u' ORDER BY `TimeStamp` DESC,`LogGuid` DESC LIMIT %u", m_Id, tabId, GUILD_BANK_MAX_LOGS);
        if (!result)
        {
            continue;
        }

        bool isNextLogGuidSet = false;
        do
        {
            Field* fields = result->Fetch();

            GuildBankEventLogEntry NewEvent;
            NewEvent.EventType = fields[1].GetUInt8();
            NewEvent.PlayerGuid = fields[2].GetUInt32();
            NewEvent.ItemOrMoney = fields[3].GetUInt32();
            NewEvent.ItemStackCount = fields[4].GetUInt8();
            NewEvent.DestTabId = fields[5].GetUInt8();
            NewEvent.TimeStamp = fields[6].GetUInt64();

            // if newEvent is moneyEvent, move it to moneyEventTab in DB and report error
            if (NewEvent.isMoneyEvent())
            {
                uint32 logGuid = fields[0].GetUInt32();
                CharacterDatabase.PExecute("UPDATE `guild_bank_eventlog` SET `TabId`='%u' WHERE `guildid`='%u' AND `TabId`='%u' AND `LogGuid`='%u'", GUILD_BANK_MONEY_LOGS_TAB, m_Id, tabId, logGuid);
                sLog.outError("GuildBankEventLog ERROR: MoneyEvent LogGuid %u for Guild %u had incorrectly set its TabId to %u, correcting it to %u TabId", logGuid, m_Id, tabId, GUILD_BANK_MONEY_LOGS_TAB);
                continue;
            }
            else
                // add event to list
                // events are ordered from oldest (in beginning) to latest (in the end)
                m_GuildBankEventLog_Item[tabId].push_front(NewEvent);

            if (!isNextLogGuidSet)
            {
                m_GuildBankEventLogNextGuid_Item[tabId] = fields[0].GetUInt32();
                // we don't have to do m_GuildBankEventLogNextGuid_Item[tabId] %= configCount; - it will be done when creating new record
                isNextLogGuidSet = true;
            }
        }
        while (result->NextRow());
        delete result;
    }

    // special handle for guild bank money log
    //                                                      0          1            2             3              4                 5            6
    QueryResult* result = CharacterDatabase.PQuery("SELECT `LogGuid`, `EventType`, `PlayerGuid`, `ItemOrMoney`, `ItemStackCount`, `DestTabId`, `TimeStamp` FROM `guild_bank_eventlog` WHERE `guildid`='%u' AND `TabId`='%u' ORDER BY `TimeStamp` DESC,`LogGuid` DESC LIMIT %u", m_Id, GUILD_BANK_MONEY_LOGS_TAB, GUILD_BANK_MAX_LOGS);
    if (!result)
    {
        return;
    }

    bool isNextMoneyLogGuidSet = false;
    do
    {
        Field* fields = result->Fetch();
        if (!isNextMoneyLogGuidSet)
        {
            m_GuildBankEventLogNextGuid_Money = fields[0].GetUInt32();
            // we don't have to do m_GuildBankEventLogNextGuid_Money %= configCount; - it will be done when creating new record
            isNextMoneyLogGuidSet = true;
        }
        GuildBankEventLogEntry NewEvent;

        NewEvent.EventType = fields[1].GetUInt8();
        NewEvent.PlayerGuid = fields[2].GetUInt32();
        NewEvent.ItemOrMoney = fields[3].GetUInt32();
        NewEvent.ItemStackCount = fields[4].GetUInt8();
        NewEvent.DestTabId = fields[5].GetUInt8();
        NewEvent.TimeStamp = fields[6].GetUInt64();

        // if newEvent is not moneyEvent, then report error
        if (!NewEvent.isMoneyEvent())
            sLog.outError("GuildBankEventLog ERROR: MoneyEvent LogGuid %u for Guild %u is not MoneyEvent - ignoring...", fields[0].GetUInt32(), m_Id);
        else
            // add event to list
            // events are ordered from oldest (in beginning) to latest (in the end)
            m_GuildBankEventLog_Money.push_front(NewEvent);
    }
    while (result->NextRow());
    delete result;
}

void Guild::DisplayGuildBankLogs(WorldSession* session, uint8 TabId)
{
    if (TabId > GUILD_BANK_MAX_TABS)
    {
        return;
    }

    ByteBuffer buffer;
    bool hasCashFlow = GetLevel() >= 5 && TabId == GUILD_BANK_MAX_TABS; // has Cash Flow perk
    WorldPacket data(SMSG_GUILD_BANK_LOG_QUERY_RESULT, m_GuildBankEventLog_Money.size() * (4 * 4 + 1) + 1 + 1);
    data.WriteBit(hasCashFlow);

    if (TabId == GUILD_BANK_MAX_TABS)
    {
        // Here we display money logs
        data.WriteBits(m_GuildBankEventLog_Money.size(), 23);
        for (GuildBankEventLog::iterator itr = m_GuildBankEventLog_Money.begin(); itr != m_GuildBankEventLog_Money.end(); ++itr)
        {
            itr->WriteData(data, buffer);
        }
    }
    else
    {
        // here we display current tab logs
        // number of log entries
        data.WriteBits(m_GuildBankEventLog_Item[TabId].size(), 23);
        for (GuildBankEventLog::iterator itr = m_GuildBankEventLog_Item[TabId].begin(); itr != m_GuildBankEventLog_Item[TabId].end(); ++itr)
        {
            itr->WriteData(data, buffer);
        }
    }
    if (!buffer.empty())
    {
        data.FlushBits();
        data.append(buffer);
    }

    data << uint32(TabId);
    if (hasCashFlow)
    {
        data << uint64(0);                                  // cash flow contribution
    }

    session->SendPacket(&data);

    DEBUG_LOG("WORLD: Sent (SMSG_GUILD_BANK_LOG_QUERY_RESULT)");
}

void Guild::LogBankEvent(uint8 EventType, uint8 TabId, uint32 PlayerGuidLow, uint32 ItemOrMoney, uint8 ItemStackCount, uint8 DestTabId)
{
    // create Event
    GuildBankEventLogEntry NewEvent;
    NewEvent.EventType = EventType;
    NewEvent.PlayerGuid = PlayerGuidLow;
    NewEvent.ItemOrMoney = ItemOrMoney;
    NewEvent.ItemStackCount = ItemStackCount;
    NewEvent.DestTabId = DestTabId;
    NewEvent.TimeStamp = uint32(time(NULL));

    // add new event to the end of event list
    uint32 currentTabId = TabId;
    uint32 currentLogGuid = 0;
    if (NewEvent.isMoneyEvent())
    {
        m_GuildBankEventLogNextGuid_Money = (m_GuildBankEventLogNextGuid_Money + 1) % sWorld.getConfig(CONFIG_UINT32_GUILD_BANK_EVENT_LOG_COUNT);
        currentLogGuid = m_GuildBankEventLogNextGuid_Money;
        currentTabId = GUILD_BANK_MONEY_LOGS_TAB;
        if (m_GuildBankEventLog_Money.size() >= GUILD_BANK_MAX_LOGS)
        {
            m_GuildBankEventLog_Money.pop_front();
        }

        m_GuildBankEventLog_Money.push_back(NewEvent);
    }
    else
    {
        m_GuildBankEventLogNextGuid_Item[TabId] = ((m_GuildBankEventLogNextGuid_Item[TabId]) + 1) % sWorld.getConfig(CONFIG_UINT32_GUILD_BANK_EVENT_LOG_COUNT);
        currentLogGuid = m_GuildBankEventLogNextGuid_Item[TabId];
        if (m_GuildBankEventLog_Item[TabId].size() >= GUILD_BANK_MAX_LOGS)
        {
            m_GuildBankEventLog_Item[TabId].pop_front();
        }

        m_GuildBankEventLog_Item[TabId].push_back(NewEvent);
    }

#ifdef ENABLE_ELUNA
    if (Eluna* e = sWorld.GetEluna())
    {
        e->OnBankEvent(this, EventType, TabId, PlayerGuidLow, ItemOrMoney, ItemStackCount, DestTabId);
    }
#endif

    // save event to database
    CharacterDatabase.PExecute("DELETE FROM `guild_bank_eventlog` WHERE `guildid`='%u' AND `LogGuid`='%u' AND `TabId`='%u'", m_Id, currentLogGuid, currentTabId);

    CharacterDatabase.PExecute("INSERT INTO `guild_bank_eventlog` (`guildid`,`LogGuid`,`TabId`,`EventType`,`PlayerGuid`,`ItemOrMoney`,`ItemStackCount`,`DestTabId`,`TimeStamp`) VALUES ('%u','%u','%u','%u','%u','%u','%u','%u','" UI64FMTD "')",
                               m_Id, currentLogGuid, currentTabId, uint32(NewEvent.EventType), NewEvent.PlayerGuid, NewEvent.ItemOrMoney, uint32(NewEvent.ItemStackCount), uint32(NewEvent.DestTabId), NewEvent.TimeStamp);
}

bool Guild::AddGBankItemToDB(uint32 GuildId, uint32 BankTab , uint32 BankTabSlot , uint32 GUIDLow, uint32 Entry)
{
    CharacterDatabase.PExecute("DELETE FROM `guild_bank_item` WHERE `guildid` = '%u' AND `TabId` = '%u'AND `SlotId` = '%u'", GuildId, BankTab, BankTabSlot);
    CharacterDatabase.PExecute("INSERT INTO `guild_bank_item` (`guildid`,`TabId`,`SlotId`,`item_guid`,`item_entry`) "
                               "VALUES ('%u', '%u', '%u', '%u', '%u')", GuildId, BankTab, BankTabSlot, GUIDLow, Entry);
    return true;
}

bool Guild::AppendDisplayGuildBankSlot(MopGuildBankPackets::GuildBankList& list,
    GuildBankTab const* tab, int32 slot) const
{
    if (!tab || slot < 0 || slot >= GUILD_BANK_MAX_SLOTS)
    {
        return false;
    }

    Item* pItem = tab->Slots[slot];
    MopGuildBankPackets::ItemRecord record;
    record.slotId = uint32(slot);
    if (pItem)
    {
        record.present = true;
        // The client copies this field to bank-cache +0x40 (the routine at
        // 0x971019, which IDA does not define as a function), and of
        // the ten functions reaching a cache record through sub_96EDC2 only
        // sub_8D02D8 -- SetGuildBankItem, the tooltip path -- reads it, testing
        // bit 2. Bit 2 is ITEM_DYNFLAG_UNLOCKED, which this server does model and
        // does set: opening a lockbox flags the item in SpellEffectSummonLock,
        // and an unlocked box can then be deposited. Sending a flat 0 would tell
        // the client every such box is still locked, so send the item's real
        // flags.
        //
        // Retail also carries 0x00030000, sometimes with 0x20 (capture-000601
        // seq 1289646). Those bits are not modelled here and are read by nothing
        // in the guild bank path -- across all 607 build-18414 bank replies,
        // 12,261 item records, bit 2 is never set, so retail's own traffic
        // exercises only the branch we already reproduce.
        record.dynamicFlags = pItem->GetUInt32Value(ITEM_FIELD_FLAGS);
        for (uint32 socketIndex = 0; socketIndex < MAX_GEM_SOCKETS; ++socketIndex)
        {
            uint32 enchantmentId = pItem->GetEnchantmentId(
                EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + socketIndex));
            if (enchantmentId)
            {
                record.socketEnchants.push_back({ socketIndex, enchantmentId });
            }
        }
        record.permanentEnchantId = pItem->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
        record.entry = pItem->GetEntry();
        // Raw, sign bit and all. A negative charge count is what marks an item
        // consumed on use, and every other serializer in this core sends it
        // unchanged -- AuctionHouseMgr, TradeHandler and MailHandler all cast
        // straight to uint32. Taking the absolute value here turned a -1 into a
        // 1, which tells the client the opposite of the truth. No corpus record
        // exercises it: all fifteen items in capture-000601 seq 1289646 carry 0.
        record.spellCharges = uint32(pItem->GetSpellCharges());
        record.stackCount = pItem->GetCount();
        record.randomPropertyId = pItem->GetItemRandomPropertyId();
        record.suffixFactor = pItem->GetItemSuffixFactor();
    }
    list.items.push_back(record);
    return true;
}

Item* Guild::StoreItem(uint8 tabId, GuildItemPosCountVec const& dest, Item* pItem)
{
    if (!pItem)
    {
        return NULL;
    }

    Item* lastItem = pItem;

    for (GuildItemPosCountVec::const_iterator itr = dest.begin(); itr != dest.end();)
    {
        uint8 slot = itr->Slot;
        uint32 count = itr->Count;

        ++itr;

        if (itr == dest.end())
        {
            lastItem = _StoreItem(tabId, slot, pItem, count, false);
            break;
        }

        lastItem = _StoreItem(tabId, slot, pItem, count, true);
    }

    return lastItem;
}

// Return stored item (if stored to stack, it can diff. from pItem). And pItem ca be deleted in this case.
Item* Guild::_StoreItem(uint8 tab, uint8 slot, Item* pItem, uint32 count, bool clone)
{
    if (!pItem)
    {
        return NULL;
    }

    DEBUG_LOG("GUILD STORAGE: StoreItem tab = %u, slot = %u, item = %u, count = %u", tab, slot, pItem->GetEntry(), count);

    Item* pItem2 = m_TabListMap[tab]->Slots[slot];

    if (!pItem2)
    {
        if (clone)
        {
            pItem = pItem->CloneItem(count);
        }
        else
        {
            pItem->SetCount(count);
        }

        if (!pItem)
        {
            return NULL;
        }

        m_TabListMap[tab]->Slots[slot] = pItem;

        pItem->SetGuidValue(ITEM_FIELD_CONTAINED, ObjectGuid());
        pItem->SetGuidValue(ITEM_FIELD_OWNER, ObjectGuid());
        AddGBankItemToDB(GetId(), tab, slot, pItem->GetGUIDLow(), pItem->GetEntry());
        pItem->FSetState(ITEM_NEW);
        pItem->SaveToDB();                                  // not in inventory and can be save standalone

        return pItem;
    }
    else
    {
        pItem2->SetCount(pItem2->GetCount() + count);
        pItem2->FSetState(ITEM_CHANGED);
        pItem2->SaveToDB();                                 // not in inventory and can be save standalone

        if (!clone)
        {
            pItem->RemoveFromWorld();
            pItem->DeleteFromDB();
            delete pItem;
        }

        return pItem2;
    }
}

void Guild::RemoveItem(uint8 tab, uint8 slot)
{
    m_TabListMap[tab]->Slots[slot] = NULL;
    CharacterDatabase.PExecute("DELETE FROM `guild_bank_item` WHERE `guildid`='%u' AND `TabId`='%u' AND `SlotId`='%u'",
                               GetId(), uint32(tab), uint32(slot));
}

InventoryResult Guild::_CanStoreItem_InSpecificSlot(uint8 tab, uint8 slot, GuildItemPosCountVec& dest, uint32& count, bool swap, Item* pSrcItem) const
{
    Item* pItem2 = m_TabListMap[tab]->Slots[slot];

    // ignore move item (this slot will be empty at move)
    if (pItem2 == pSrcItem)
    {
        pItem2 = NULL;
    }

    uint32 need_space;

    // empty specific slot - check item fit to slot
    if (!pItem2 || swap)
    {
        // non empty stack with space
        need_space = pSrcItem->GetMaxStackCount();
    }
    // non empty slot, check item type
    else
    {
        // check item type
        if (pItem2->GetEntry() != pSrcItem->GetEntry())
        {
            return EQUIP_ERR_ITEM_CANT_STACK;
        }

        // check free space
        if (pItem2->GetCount() >= pSrcItem->GetMaxStackCount())
        {
            return EQUIP_ERR_ITEM_CANT_STACK;
        }

        need_space = pSrcItem->GetMaxStackCount() - pItem2->GetCount();
    }

    if (need_space > count)
    {
        need_space = count;
    }

    GuildItemPosCount newPosition = GuildItemPosCount(slot, need_space);
    if (!newPosition.isContainedIn(dest))
    {
        dest.push_back(newPosition);
        count -= need_space;
    }

    return EQUIP_ERR_OK;
}

InventoryResult Guild::_CanStoreItem_InTab(uint8 tab, GuildItemPosCountVec& dest, uint32& count, bool merge, Item* pSrcItem, uint8 skip_slot) const
{
    for (uint32 j = 0; j < GUILD_BANK_MAX_SLOTS; ++j)
    {
        // skip specific slot already processed in first called _CanStoreItem_InSpecificSlot
        if (j == skip_slot)
        {
            continue;
        }

        Item* pItem2 = m_TabListMap[tab]->Slots[j];

        // ignore move item (this slot will be empty at move)
        if (pItem2 == pSrcItem)
        {
            pItem2 = NULL;
        }

        // if merge skip empty, if !merge skip non-empty
        if ((pItem2 != NULL) != merge)
        {
            continue;
        }

        if (pItem2)
        {
            if (pItem2->GetEntry() == pSrcItem->GetEntry() && pItem2->GetCount() < pSrcItem->GetMaxStackCount())
            {
                uint32 need_space = pSrcItem->GetMaxStackCount() - pItem2->GetCount();
                if (need_space > count)
                {
                    need_space = count;
                }

                GuildItemPosCount newPosition = GuildItemPosCount(j, need_space);
                if (!newPosition.isContainedIn(dest))
                {
                    dest.push_back(newPosition);
                    count -= need_space;

                    if (count == 0)
                    {
                        return EQUIP_ERR_OK;
                    }
                }
            }
        }
        else
        {
            uint32 need_space = pSrcItem->GetMaxStackCount();
            if (need_space > count)
            {
                need_space = count;
            }

            GuildItemPosCount newPosition = GuildItemPosCount(j, need_space);
            if (!newPosition.isContainedIn(dest))
            {
                dest.push_back(newPosition);
                count -= need_space;

                if (count == 0)
                {
                    return EQUIP_ERR_OK;
                }
            }
        }
    }
    return EQUIP_ERR_OK;
}

InventoryResult Guild::CanStoreItem(uint8 tab, uint8 slot, GuildItemPosCountVec& dest, uint32 count, Item* pItem, bool swap) const
{
    DEBUG_LOG("GUILD STORAGE: CanStoreItem tab = %u, slot = %u, item = %u, count = %u", tab, slot, pItem->GetEntry(), count);

    if (count > pItem->GetCount())
    {
        return EQUIP_ERR_COULDNT_SPLIT_ITEMS;
    }

    if (pItem->IsSoulBound())
    {
        return EQUIP_ERR_CANT_DROP_SOULBOUND;
    }

    // in specific slot
    if (slot != NULL_SLOT)
    {
        InventoryResult res = _CanStoreItem_InSpecificSlot(tab, slot, dest, count, swap, pItem);
        if (res != EQUIP_ERR_OK)
        {
            return res;
        }

        if (count == 0)
        {
            return EQUIP_ERR_OK;
        }
    }

    // not specific slot or have space for partly store only in specific slot

    // search stack in tab for merge to
    if (pItem->GetMaxStackCount() > 1)
    {
        InventoryResult res = _CanStoreItem_InTab(tab, dest, count, true, pItem, slot);
        if (res != EQUIP_ERR_OK)
        {
            return res;
        }

        if (count == 0)
        {
            return EQUIP_ERR_OK;
        }
    }

    // search free slot in bag for place to
    InventoryResult res = _CanStoreItem_InTab(tab, dest, count, false, pItem, slot);
    if (res != EQUIP_ERR_OK)
    {
        return res;
    }

    if (count == 0)
    {
        return EQUIP_ERR_OK;
    }

    return EQUIP_ERR_BANK_FULL;
}

void Guild::SetGuildBankTabText(uint8 TabId, std::string text)
{
    if (TabId >= GetPurchasedTabs())
    {
        return;
    }

    if (!m_TabListMap[TabId])
    {
        return;
    }

    if (m_TabListMap[TabId]->Text == text)
    {
        return;
    }

    utf8truncate(text, 500);                                // DB and client size limitation

    m_TabListMap[TabId]->Text = text;

    CharacterDatabase.escape_string(text);
    CharacterDatabase.PExecute("UPDATE `guild_bank_tab` SET `TabText`='%s' WHERE `guildid`='%u' AND `TabId`='%u'", text.c_str(), m_Id, uint32(TabId));

    // announce
    SendGuildBankTabText(NULL, TabId);
}

void Guild::SendGuildBankTabText(WorldSession* session, uint8 TabId)
{
    GuildBankTab const* tab = m_TabListMap[TabId];

    WorldPacket data;
    if (!MopGuildPackets::BuildGuildBankText(data, uint32(TabId), tab->Text))
        return;

    if (session)
    {
        session->SendPacket(&data);
    }
    else
    {
        BroadcastPacket(&data);
    }
}

void Guild::SwapItems(Player* pl, uint8 BankTab, uint8 BankTabSlot, uint8 BankTabDst, uint8 BankTabSlotDst, uint32 SplitedAmount)
{
    // empty operation
    if (BankTab == BankTabDst && BankTabSlot == BankTabSlotDst)
    {
        return;
    }

    Item* pItemSrc = GetItem(BankTab, BankTabSlot);
    if (!pItemSrc)                                      // may prevent crash
    {
        return;
    }

    if (SplitedAmount > pItemSrc->GetCount())
    {
        return;                                         // cheating?
    }
    else if (SplitedAmount == pItemSrc->GetCount())
    {
        SplitedAmount = 0;                              // no split
    }

    Item* pItemDst = GetItem(BankTabDst, BankTabSlotDst);

    // Rights are checked on EVERY move, not only on one that crosses tabs. Both
    // checks below used to sit behind `BankTab != BankTabDst`, which left a
    // same-tab rearrange with no permission check whatsoever -- and same-tab is
    // the ordinary case rather than a corner one: every bank-to-bank body decoded
    // from the corpus at build 18414 moves within a single tab. A forged client could
    // therefore merge, split and reorder items in any purchased tab, including a
    // tab its rank cannot so much as view. The real client never sends that,
    // which is exactly why the gap survived: its producer checks the destination
    // tab's deposit permission before it will build the packet at all.
    if (!IsMemberHaveRights(pl->GetGUIDLow(), BankTabDst, GUILD_BANK_RIGHT_DEPOSIT_ITEM))
    {
        return;
    }

    // The source side is a withdrawal only when the item leaves its tab. Within
    // one tab nothing leaves the guild, so require sight of the tab but do not
    // spend the member's daily allowance on tidying it -- otherwise a member who
    // had used up their withdrawals could not reorder a tab they can see.
    if (BankTab != BankTabDst)
    {
        if (GetMemberSlotWithdrawRem(pl->GetGUIDLow(), BankTab) == 0)
        {
            return;
        }
    }
    else if (!IsMemberHaveRights(pl->GetGUIDLow(), BankTab, GUILD_BANK_RIGHT_VIEW_TAB))
    {
        return;
    }

    if (SplitedAmount)
    {
        // Bank -> Bank item split (in empty or non empty slot
        GuildItemPosCountVec dest;
        InventoryResult msg = CanStoreItem(BankTabDst, BankTabSlotDst, dest, SplitedAmount, pItemSrc, false);
        if (msg != EQUIP_ERR_OK)
        {
            pl->SendEquipError(msg, pItemSrc, NULL);
            return;
        }

        Item* pNewItem = pItemSrc->CloneItem(SplitedAmount);
        if (!pNewItem)
        {
            pl->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItemSrc, NULL);
            return;
        }

        CharacterDatabase.BeginTransaction();

        if (BankTab != BankTabDst)
        {
            LogBankEvent(GUILD_BANK_LOG_MOVE_ITEM, BankTab, pl->GetGUIDLow(), pItemSrc->GetEntry(), SplitedAmount, BankTabDst);
        }

        pl->ItemRemovedQuestCheck(pItemSrc->GetEntry(), SplitedAmount);
        pItemSrc->SetCount(pItemSrc->GetCount() - SplitedAmount);
        pItemSrc->FSetState(ITEM_CHANGED);
        pItemSrc->SaveToDB();                               // not in inventory and can be save standalone
        StoreItem(BankTabDst, dest, pNewItem);

        // Spend the allowance the check above tested. It used to be tested and
        // never spent, so a member with a single withdrawal left could relay any
        // number of items out of a restricted tab into one with looser rights and
        // draw them from there -- the source tab's configured daily limit only
        // ever had to be non-zero, never sufficient. MoveFromBankToChar has
        // always consumed it this way; this path simply did not.
        if (BankTab != BankTabDst)
        {
            MemberItemWithdraw(BankTab, pl->GetGUIDLow());
        }

        if (!CommitBankMutation(pl, "SwapItems"))
        {
            return;
        }
    }
    else                                                    // non split
    {
        GuildItemPosCountVec gDest;
        InventoryResult msg = CanStoreItem(BankTabDst, BankTabSlotDst, gDest, pItemSrc->GetCount(), pItemSrc, false);
        if (msg == EQUIP_ERR_OK)                            // merge to
        {
            CharacterDatabase.BeginTransaction();

            if (BankTab != BankTabDst)
            {
                LogBankEvent(GUILD_BANK_LOG_MOVE_ITEM, BankTab, pl->GetGUIDLow(), pItemSrc->GetEntry(), pItemSrc->GetCount(), BankTabDst);
            }

            RemoveItem(BankTab, BankTabSlot);
            StoreItem(BankTabDst, gDest, pItemSrc);

            if (BankTab != BankTabDst)                      // see the split branch
            {
                MemberItemWithdraw(BankTab, pl->GetGUIDLow());
            }

            if (!CommitBankMutation(pl, "SwapItems"))
            {
                return;
            }
        }
        else                                                // swap
        {
            gDest.clear();
            msg = CanStoreItem(BankTabDst, BankTabSlotDst, gDest, pItemSrc->GetCount(), pItemSrc, true);
            if (msg != EQUIP_ERR_OK)
            {
                pl->SendEquipError(msg, pItemSrc, NULL);
                return;
            }

            GuildItemPosCountVec gSrc;
            msg = CanStoreItem(BankTab, BankTabSlot, gSrc, pItemDst->GetCount(), pItemDst, true);
            if (msg != EQUIP_ERR_OK)
            {
                pl->SendEquipError(msg, pItemDst, NULL);
                return;
            }

            if (BankTab != BankTabDst)
            {
                // check source pos rights (item swapped to src)
                if (!IsMemberHaveRights(pl->GetGUIDLow(), BankTab, GUILD_BANK_RIGHT_DEPOSIT_ITEM))
                {
                    return;
                }

                // check dest pos rights (item swapped to src)
                uint32 remRightDst = GetMemberSlotWithdrawRem(pl->GetGUIDLow(), BankTabDst);
                if (remRightDst <= 0)
                {
                    return;
                }
            }

            CharacterDatabase.BeginTransaction();

            if (BankTab != BankTabDst)
            {
                LogBankEvent(GUILD_BANK_LOG_MOVE_ITEM, BankTab,    pl->GetGUIDLow(), pItemSrc->GetEntry(), pItemSrc->GetCount(), BankTabDst);
                LogBankEvent(GUILD_BANK_LOG_MOVE_ITEM, BankTabDst, pl->GetGUIDLow(), pItemDst->GetEntry(), pItemDst->GetCount(), BankTab);
            }

            RemoveItem(BankTab, BankTabSlot);
            RemoveItem(BankTabDst, BankTabSlotDst);
            StoreItem(BankTab, gSrc, pItemDst);
            StoreItem(BankTabDst, gDest, pItemSrc);

            // A cross-tab swap takes an item OUT of both tabs, and the checks
            // above already tested both allowances, so spend both. Same tab, and
            // nothing has left the guild at all.
            if (BankTab != BankTabDst)
            {
                MemberItemWithdraw(BankTab, pl->GetGUIDLow());
                MemberItemWithdraw(BankTabDst, pl->GetGUIDLow());
            }

            if (!CommitBankMutation(pl, "SwapItems"))
            {
                return;
            }
        }
    }
    DisplayGuildBankContentUpdate(BankTab, BankTabSlot, BankTab == BankTabDst ? BankTabSlotDst : -1);
    if (BankTab != BankTabDst)
    {
        DisplayGuildBankContentUpdate(BankTabDst, BankTabSlotDst);
    }
}


void Guild::MoveFromBankToChar(Player* pl, uint8 BankTab, uint8 BankTabSlot, uint8 PlayerBag, uint8 PlayerSlot, uint32 SplitedAmount)
{
    Item* pItemBank = GetItem(BankTab, BankTabSlot);
    Item* pItemChar = pl->GetItemByPos(PlayerBag, PlayerSlot);

    if (!pItemBank)                                     // Problem to get bank item
    {
        return;
    }

    if (SplitedAmount > pItemBank->GetCount())
    {
        return;                                         // cheating?
    }
    else if (SplitedAmount == pItemBank->GetCount())
    {
        SplitedAmount = 0;                              // no split
    }

    if (SplitedAmount)
    {
        // Bank -> Char split to slot (patly move)
        Item* pNewItem = pItemBank->CloneItem(SplitedAmount);
        if (!pNewItem)
        {
            pl->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItemBank, NULL);
            return;
        }

        ItemPosCountVec dest;
        InventoryResult msg = pl->CanStoreItem(PlayerBag, PlayerSlot, dest, pNewItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            pl->SendEquipError(msg, pNewItem, NULL);
            delete pNewItem;
            return;
        }

        // check source pos rights (item moved to inventory)
        uint32 remRight = GetMemberSlotWithdrawRem(pl->GetGUIDLow(), BankTab);
        if (remRight <= 0)
        {
            delete pNewItem;
            return;
        }

        CharacterDatabase.BeginTransaction();
        LogBankEvent(GUILD_BANK_LOG_WITHDRAW_ITEM, BankTab, pl->GetGUIDLow(), pItemBank->GetEntry(), SplitedAmount);

        pItemBank->SetCount(pItemBank->GetCount() - SplitedAmount);
        pItemBank->FSetState(ITEM_CHANGED);
        pItemBank->SaveToDB();                              // not in inventory and can be save standalone
        pl->MoveItemToInventory(dest, pNewItem, true);
        pl->SaveInventoryAndGoldToDB();

        MemberItemWithdraw(BankTab, pl->GetGUIDLow());
        if (!CommitBankMutation(pl, "MoveFromBankToChar"))
        {
            return;
        }
    }
    else                                                    // Bank -> Char swap with slot (move)
    {
        ItemPosCountVec dest;
        InventoryResult msg = pl->CanStoreItem(PlayerBag, PlayerSlot, dest, pItemBank, false);
        if (msg == EQUIP_ERR_OK)                            // merge case
        {
            // check source pos rights (item moved to inventory)
            uint32 remRight = GetMemberSlotWithdrawRem(pl->GetGUIDLow(), BankTab);
            if (remRight <= 0)
            {
                return;
            }

            CharacterDatabase.BeginTransaction();
            LogBankEvent(GUILD_BANK_LOG_WITHDRAW_ITEM, BankTab, pl->GetGUIDLow(), pItemBank->GetEntry(), pItemBank->GetCount());

            RemoveItem(BankTab, BankTabSlot);
            pl->MoveItemToInventory(dest, pItemBank, true);
            pl->SaveInventoryAndGoldToDB();

            MemberItemWithdraw(BankTab, pl->GetGUIDLow());
            if (!CommitBankMutation(pl, "MoveFromBankToChar"))
            {
                return;
            }
        }
        else                                                // Bank <-> Char swap items
        {
            // check source pos rights (item swapped to bank)
            if (!IsMemberHaveRights(pl->GetGUIDLow(), BankTab, GUILD_BANK_RIGHT_DEPOSIT_ITEM))
            {
                return;
            }

            if (pItemChar)
            {
                if (!pItemChar->CanBeTraded())
                {
                    pl->SendEquipError(EQUIP_ERR_ITEMS_CANT_BE_SWAPPED, pItemChar, NULL);
                    return;
                }
            }

            ItemPosCountVec iDest;
            msg = pl->CanStoreItem(PlayerBag, PlayerSlot, iDest, pItemBank, true);
            if (msg != EQUIP_ERR_OK)
            {
                pl->SendEquipError(msg, pItemBank, NULL);
                return;
            }

            GuildItemPosCountVec gDest;
            if (pItemChar)
            {
                msg = CanStoreItem(BankTab, BankTabSlot, gDest, pItemChar->GetCount(), pItemChar, true);
                if (msg != EQUIP_ERR_OK)
                {
                    pl->SendEquipError(msg, pItemChar, NULL);
                    return;
                }
            }

            // check source pos rights (item moved to inventory)
            uint32 remRight = GetMemberSlotWithdrawRem(pl->GetGUIDLow(), BankTab);
            if (remRight <= 0)
            {
                return;
            }

            if (pItemChar)
            {
                // logging item move to bank
                if (pl->GetSession()->GetSecurity() > SEC_PLAYER && sWorld.getConfig(CONFIG_BOOL_GM_LOG_TRADE))
                {
                    sLog.outCommand(pl->GetSession()->GetAccountId(), "GM %s (Account: %u) deposit item: %s (Entry: %d Count: %u) to guild bank (Guild ID: %u )",
                                    pl->GetName(), pl->GetSession()->GetAccountId(),
                                    pItemChar->GetProto()->Name1, pItemChar->GetEntry(), pItemChar->GetCount(),
                                    m_Id);
                }
            }

            CharacterDatabase.BeginTransaction();
            LogBankEvent(GUILD_BANK_LOG_WITHDRAW_ITEM, BankTab, pl->GetGUIDLow(), pItemBank->GetEntry(), pItemBank->GetCount());
            if (pItemChar)
            {
                LogBankEvent(GUILD_BANK_LOG_DEPOSIT_ITEM, BankTab, pl->GetGUIDLow(), pItemChar->GetEntry(), pItemChar->GetCount());
            }

            RemoveItem(BankTab, BankTabSlot);
            if (pItemChar)
            {
                pl->MoveItemFromInventory(PlayerBag, PlayerSlot, true);
                pItemChar->DeleteFromInventoryDB();
                StoreItem(BankTab, gDest, pItemChar);
            }

            pl->MoveItemToInventory(iDest, pItemBank, true);
            pl->SaveInventoryAndGoldToDB();

            MemberItemWithdraw(BankTab, pl->GetGUIDLow());
            if (!CommitBankMutation(pl, "MoveFromBankToChar"))
            {
                return;
            }
        }
    }
    DisplayGuildBankContentUpdate(BankTab, BankTabSlot);
}


void Guild::MoveFromCharToBank(Player* pl, uint8 PlayerBag, uint8 PlayerSlot, uint8 BankTab, uint8 BankTabSlot, uint32 SplitedAmount)
{
    Item* pItemBank = GetItem(BankTab, BankTabSlot);
    Item* pItemChar = pl->GetItemByPos(PlayerBag, PlayerSlot);

    if (!pItemChar)                                         // Problem to get item from player
    {
        return;
    }

    if (!pItemChar->CanBeTraded())
    {
        pl->SendEquipError(EQUIP_ERR_ITEMS_CANT_BE_SWAPPED, pItemChar, NULL);
        return;
    }

    // check source pos rights (item moved to bank)
    if (!IsMemberHaveRights(pl->GetGUIDLow(), BankTab, GUILD_BANK_RIGHT_DEPOSIT_ITEM))
    {
        return;
    }

    if (SplitedAmount > pItemChar->GetCount())
    {
        return;                                             // cheating?
    }
    else if (SplitedAmount == pItemChar->GetCount())
    {
        SplitedAmount = 0;                                  // no split
    }

    if (SplitedAmount)
    {
        // Char -> Bank split to empty or non-empty slot (partly move)
        GuildItemPosCountVec dest;
        InventoryResult msg = CanStoreItem(BankTab, BankTabSlot, dest, SplitedAmount, pItemChar, false);
        if (msg != EQUIP_ERR_OK)
        {
            pl->SendEquipError(msg, pItemChar, NULL);
            return;
        }

        Item* pNewItem = pItemChar->CloneItem(SplitedAmount);
        if (!pNewItem)
        {
            pl->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItemChar, NULL);
            return;
        }

        // logging item move to bank (before items merge
        if (pl->GetSession()->GetSecurity() > SEC_PLAYER && sWorld.getConfig(CONFIG_BOOL_GM_LOG_TRADE))
        {
            sLog.outCommand(pl->GetSession()->GetAccountId(), "GM %s (Account: %u) deposit item: %s (Entry: %d Count: %u) to guild bank (Guild ID: %u )",
                            pl->GetName(), pl->GetSession()->GetAccountId(),
                            pItemChar->GetProto()->Name1, pItemChar->GetEntry(), SplitedAmount, m_Id);
        }

        CharacterDatabase.BeginTransaction();
        LogBankEvent(GUILD_BANK_LOG_DEPOSIT_ITEM, BankTab, pl->GetGUIDLow(), pItemChar->GetEntry(), SplitedAmount);

        pl->ItemRemovedQuestCheck(pItemChar->GetEntry(), SplitedAmount);
        pItemChar->SetCount(pItemChar->GetCount() - SplitedAmount);
        pItemChar->SetState(ITEM_CHANGED);
        pl->SaveInventoryAndGoldToDB();
        StoreItem(BankTab, dest, pNewItem);
        if (!CommitBankMutation(pl, "MoveFromCharToBank"))
        {
            return;
        }

        DisplayGuildBankContentUpdate(BankTab, dest);
    }
    else                                                    // Char -> Bank swap with empty or non-empty (move)
    {
        GuildItemPosCountVec dest;
        InventoryResult msg = CanStoreItem(BankTab, BankTabSlot, dest, pItemChar->GetCount(), pItemChar, false);
        if (msg == EQUIP_ERR_OK)                            // merge
        {
            // logging item move to bank
            if (pl->GetSession()->GetSecurity() > SEC_PLAYER && sWorld.getConfig(CONFIG_BOOL_GM_LOG_TRADE))
            {
                sLog.outCommand(pl->GetSession()->GetAccountId(), "GM %s (Account: %u) deposit item: %s (Entry: %d Count: %u) to guild bank (Guild ID: %u )",
                                pl->GetName(), pl->GetSession()->GetAccountId(),
                                pItemChar->GetProto()->Name1, pItemChar->GetEntry(), pItemChar->GetCount(),
                                m_Id);
            }

            CharacterDatabase.BeginTransaction();
            LogBankEvent(GUILD_BANK_LOG_DEPOSIT_ITEM, BankTab, pl->GetGUIDLow(), pItemChar->GetEntry(), pItemChar->GetCount());

            pl->MoveItemFromInventory(PlayerBag, PlayerSlot, true);
            pItemChar->DeleteFromInventoryDB();

            StoreItem(BankTab, dest, pItemChar);
            pl->SaveInventoryAndGoldToDB();
            if (!CommitBankMutation(pl, "MoveFromCharToBank"))
            {
                return;
            }

            DisplayGuildBankContentUpdate(BankTab, dest);
        }
        else                                                // Char <-> Bank swap items (posible NULL bank item)
        {
            ItemPosCountVec iDest;
            if (pItemBank)
            {
                msg = pl->CanStoreItem(PlayerBag, PlayerSlot, iDest, pItemBank, true);
                if (msg != EQUIP_ERR_OK)
                {
                    pl->SendEquipError(msg, pItemBank, NULL);
                    return;
                }
            }

            GuildItemPosCountVec gDest;
            msg = CanStoreItem(BankTab, BankTabSlot, gDest, pItemChar->GetCount(), pItemChar, true);
            if (msg != EQUIP_ERR_OK)
            {
                pl->SendEquipError(msg, pItemChar, NULL);
                return;
            }

            if (pItemBank)
            {
                // check bank pos rights (item swapped with inventory)
                uint32 remRight = GetMemberSlotWithdrawRem(pl->GetGUIDLow(), BankTab);
                if (remRight <= 0)
                {
                    return;
                }
            }

            // logging item move to bank
            if (pl->GetSession()->GetSecurity() > SEC_PLAYER && sWorld.getConfig(CONFIG_BOOL_GM_LOG_TRADE))
            {
                sLog.outCommand(pl->GetSession()->GetAccountId(), "GM %s (Account: %u) deposit item: %s (Entry: %d Count: %u) to guild bank (Guild ID: %u )",
                                pl->GetName(), pl->GetSession()->GetAccountId(),
                                pItemChar->GetProto()->Name1, pItemChar->GetEntry(), pItemChar->GetCount(),
                                m_Id);
            }

            CharacterDatabase.BeginTransaction();
            if (pItemBank)
            {
                LogBankEvent(GUILD_BANK_LOG_WITHDRAW_ITEM, BankTab, pl->GetGUIDLow(), pItemBank->GetEntry(), pItemBank->GetCount());
            }
            LogBankEvent(GUILD_BANK_LOG_DEPOSIT_ITEM, BankTab, pl->GetGUIDLow(), pItemChar->GetEntry(), pItemChar->GetCount());

            pl->MoveItemFromInventory(PlayerBag, PlayerSlot, true);
            pItemChar->DeleteFromInventoryDB();
            if (pItemBank)
            {
                RemoveItem(BankTab, BankTabSlot);
            }

            StoreItem(BankTab, gDest, pItemChar);
            if (pItemBank)
            {
                pl->MoveItemToInventory(iDest, pItemBank, true);
            }
            pl->SaveInventoryAndGoldToDB();
            if (pItemBank)
            {
                MemberItemWithdraw(BankTab, pl->GetGUIDLow());
            }
            if (!CommitBankMutation(pl, "MoveFromCharToBank"))
            {
                return;
            }

            DisplayGuildBankContentUpdate(BankTab, gDest);
        }
    }
}
