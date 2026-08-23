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

/**
 * @file GuildHandler.cpp
 * @brief Guild opcode handlers
 *
 * This file handles guild-related opcodes including:
 * - CMSG_GUILD_QUERY: Query guild information
 * - CMSG_GUILD_CREATE: Create new guild
 * - CMSG_GUILD_INVITE: Invite player to guild
 * - CMSG_GUILD_ACCEPT: Accept guild invitation
 * - CMSG_GUILD_DECLINE: Decline guild invitation
 * - CMSG_GUILD_ROSTER: Request guild roster
 * - CMSG_GUILD_LEAVE: Leave guild
 * - CMSG_GUILD_DISBAND: Disband guild
 * - CMSG_GUILD_LEADER: Transfer guild leadership
 * - CMSG_GUILD_MOTD: Set guild message of the day
 * - CMSG_GUILD_ADD_RANK: Add guild rank
 * - CMSG_GUILD_DELETE_RANK: Delete guild rank
 * - CMSG_GUILD_DEMOTE: Demote guild member
 * - CMSG_GUILD_PROMOTE: Promote guild member
 * - CMSG_GUILD_REMOVE: Remove guild member
 * - CMSG_GUILD_CHAT: Send guild chat message
 * - CMSG_GUILD_BANK: Guild bank operations
 */

#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "Opcodes.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GossipDef.h"
#include "SocialMgr.h"
#include "Calendar.h"
#include "MopGuildBankPackets.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

namespace
{
    /// Retail answers a guild bank money change with the new total and nothing
    /// else -- capture-000888 sequence 307424, eight bytes, after a deposit at
    /// 307413. It does NOT push a bank list: none follows that deposit at all,
    /// and the client re-queries its own remaining allowance itself with
    /// CMSG_GUILD_BANK_MONEY_WITHDRAWN_QUERY. The inherited handlers pushed a
    /// full bank list to every online member here, which is traffic no retail
    /// server produces and would have cost a thirty-member guild thirty-one
    /// packets per deposit.
    void BroadcastGuildBankMoney(Guild* guild)
    {
        WorldPacket data(SMSG_GUILD_EVENT_BANK_MONEY_CHANGED, 8);
        MopGuildBankPackets::BuildGuildBankMoneyChanged(data, guild->GetGuildBankMoney());
        guild->BroadcastPacket(&data);
    }

    /// CommitTransactionDirect returning false is ambiguous: the statements may
    /// have been rolled back, leaving nothing durable, or the COMMIT may have
    /// applied and only its result been unreadable. Both balances have already
    /// been published to memory by this point, and no ordering of those two
    /// mutations is safe against both outcomes -- so do not guess. Read what the
    /// database actually holds for each and adopt it, which makes the character
    /// save that follows a no-op either way.
    ///
    /// If either balance cannot be re-read there is no safe value to hold, and
    /// the logout save would persist whichever way it is currently wrong.
    /// Discard the session's character state instead: losing unsaved progress is
    /// cheaper than minting or burning gold.
    void RecoverGuildBankMoneyAfterFailedCommit(WorldSession* session,
        Guild* guild, uint32 guildId, char const* opcodeName)
    {
        Player* player = session->GetPlayer();
        bool recovered = true;

        if (QueryResult* durable = CharacterDatabase.PQuery(
            "SELECT `money` FROM `characters` WHERE `guid` = '%u'", player->GetGUIDLow()))
        {
            player->SetMoney((*durable)[0].GetUInt64());
            delete durable;
        }
        else
        {
            recovered = false;
        }

        if (QueryResult* durable = CharacterDatabase.PQuery(
            "SELECT `BankMoney` FROM `guild` WHERE `guildid` = '%u'", guildId))
        {
            guild->AdoptBankMoneyFromDB((*durable)[0].GetUInt64());
            delete durable;
        }
        else
        {
            // The bank total in memory may now disagree with the row and there
            // is no way to tell. Left alone, the next deposit by ANY member adds
            // to it and writes the result out durably, so the error compounds
            // and lands on someone else. Refuse money operations until the guild
            // is reloaded.
            guild->MarkBankStateUntrusted();
            recovered = false;
        }

        // A withdraw moves a third balance: the member's remaining daily
        // allowance, decremented in memory and queued in the same transaction by
        // Guild::MemberMoneyWithdraw. Re-read it too, or a rolled-back withdraw
        // leaves the player charged for an allowance they never spent.
        if (QueryResult* durable = CharacterDatabase.PQuery(
            "SELECT `BankRemMoney` FROM `guild_member` WHERE `guildid` = '%u' AND `guid` = '%u'",
            guildId, player->GetGUIDLow()))
        {
            guild->AdoptMemberRemainingWithdrawFromDB(player->GetGUIDLow(),
                (*durable)[0].GetUInt32());
            delete durable;
        }
        else
        {
            recovered = false;
        }

        if (!recovered)
        {
            sLog.outError("%s: could not re-read the durable balances for player %u "
                "and guild %u after a failed commit; discarding in-memory character state",
                opcodeName, player->GetGUIDLow(), guildId);
            session->SuppressCharacterSave();
        }
        else
        {
            sLog.outBasic("%s: commit could not be confirmed for player %u and guild %u; "
                "all three balances re-read from the database",
                opcodeName, player->GetGUIDLow(), guildId);
        }

        session->KickPlayer();
    }
}


void WorldSession::HandleGuildQueryOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_QUERY");

    // The inherited reader took two raw uint64, i.e. a fixed 16 bytes, against a wire
    // body that is 9..17 and bit-packed. Orders now come from the client's own send
    // serializer sub_665EE4; see MopGuildPackets::ReadGuildQuery. Note the wire order
    // is player first, then guild -- the reverse of how this was named.
    uint64 rawPlayerGuid = 0;
    uint64 rawGuildGuid = 0;
    MopGuildPackets::ReadGuildQuery(recvPacket, rawPlayerGuid, rawGuildGuid);

    ObjectGuid const guildGuid(rawGuildGuid);
    ObjectGuid const playerGuid(rawPlayerGuid);

    if (Guild* guild = sGuildMgr.GetGuildByGuid(guildGuid))
    {
        if (guild->GetMemberSlot(playerGuid))
        {
            guild->Query(this);
            return;
        }
    }

    SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
}

/**
 * @brief Creates a new guild for the current player.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildCreateOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_CREATE");

    std::string gname;
    recvPacket >> gname;

    if (GetPlayer()->GetGuildId())                          // already in guild
    {
        return;
    }

    Guild* guild = new Guild;
    if (!guild->Create(GetPlayer(), gname))
    {
        delete guild;
        return;
    }

    sGuildMgr.AddGuild(guild);
}

/**
 * @brief Sends a guild invitation packet to another player.
 *
 * @param player The invited player.
 * @param alreadyInGuild Unused legacy flag for prior guild membership checks.
 */
bool WorldSession::SendGuildInvite(Player* player, bool /*alreadyInGuild = false*/)
{
    // Both ways into the invite reply come through here: the CMSG_GUILD_INVITE
    // handler and Eluna's Player:SendGuildInvite, which is a live path because
    // SCRIPT_LIB_ELUNA defaults ON.
    //
    // Returns false without sending if the body cannot be built, so the caller
    // can decline to flag the target as invited. The previous version set that
    // flag first and sent afterwards, which left targets invited to a guild
    // whose invitation they could never see.
    if (!player || !player->GetSession() || !_player)
    {
        return false;
    }

    Guild* guild = sGuildMgr.GetGuildById(_player->GetGuildId());
    if (!guild)
    {
        return false;
    }

    // The invitee's current guild, if any, is what the client shows in the
    // "you will lose reputation with" warning; with no old guild it sends a zero
    // guid, an empty name and a zero realm, as the retail capture does.
    bool const hasOldGuild = player->GetGuildId() != 0;

    WorldPacket data;
    if (!MopGuildPackets::BuildGuildInvite(data,
            guild->GetObjectGuid().GetRawValue(),
            hasOldGuild ? player->GetGuildGuid().GetRawValue() : uint64(0),
            _player->GetName(), guild->GetName(),
            hasOldGuild ? player->GetGuildName() : std::string(),
            guild->GetLevel(),
            guild->GetEmblemStyle(), guild->GetEmblemColor(),
            guild->GetBorderStyle(), guild->GetBorderColor(),
            guild->GetBackgroundColor(),
            realmID, hasOldGuild ? realmID : 0, realmID))
    {
        sLog.outError("WorldSession::SendGuildInvite: guild %u has a field too long "
                      "for the 18414 invite body; not sending to %s",
                      guild->GetId(), player->GetGuidStr().c_str());
        return false;
    }

    player->GetSession()->SendPacket(&data);

    // Flag the target here rather than in the caller. Eluna's
    // Player:SendGuildInvite goes straight through this function and ignores the
    // return value, so a flag set only by the CMSG handler would leave a scripted
    // invitation showing a popup that HandleGuildAcceptOpcode then refuses --
    // it resolves the guild from GetGuildIdInvited(), which would still be 0.
    // Setting it after SendPacket keeps the send-first ordering that stops a
    // target being marked invited for a packet that was never built.
    player->SetGuildIdInvited(_player->GetGuildId());

    DEBUG_LOG("WORLD: Sent (SMSG_GUILD_INVITE)");
    return true;
}

/**
 * @brief Invites another player to the current guild.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildInviteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_INVITE");

    std::string Invitedname, plname;
    Player* player = NULL;

    if (!MopGuildPackets::ReadGuildInvite(recvPacket, Invitedname))
    {
        sLog.outError("WORLD: Malformed CMSG_GUILD_INVITE from player %s",
            GetPlayer()->GetName());
        return;
    }

    // The 18414 roster and who-list hand GuildInvite a realm-qualified name, so
    // the same home-realm suffix that broke whisper replies has to come off here
    // before the lookup.
    StripHomeRealmSuffix(Invitedname);

    if (normalizePlayerName(Invitedname))
    {
        player = sObjectAccessor.FindPlayerByName(Invitedname.c_str());
    }

    if (!player)
    {
        SendGuildCommandResult(GUILD_INVITE_S, Invitedname, ERR_GUILD_PLAYER_NOT_FOUND_S);
        return;
    }

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    // OK result but not send invite
    if (player->GetSocial()->HasIgnore(GetPlayer()->GetObjectGuid()))
    {
        return;
    }

    // not let enemies sign guild charter
    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) && player->GetTeam() != GetPlayer()->GetTeam())
    {
        SendGuildCommandResult(GUILD_INVITE_S, Invitedname, ERR_GUILD_NOT_ALLIED);
        return;
    }

    if (player->GetGuildId())
    {
        plname = player->GetName();
        SendGuildCommandResult(GUILD_INVITE_S, plname, ERR_ALREADY_IN_GUILD_S);
        return;
    }

    if (player->GetGuildIdInvited())
    {
        plname = player->GetName();
        SendGuildCommandResult(GUILD_INVITE_S, plname, ERR_ALREADY_INVITED_TO_GUILD_S);
        return;
    }

    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_INVITE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    DEBUG_LOG("Player %s Invited %s to Join his Guild", GetPlayer()->GetName(), Invitedname.c_str());

    // SendGuildInvite sends and flags the target, in that order, and reports
    // failure without doing either.
    if (!SendGuildInvite(player))
    {
        SendGuildCommandResult(GUILD_INVITE_S, Invitedname, ERR_GUILD_INTERNAL);
        return;
    }

    // Put record into guildlog
    guild->LogGuildEvent(GUILD_EVENT_LOG_INVITE_PLAYER, GetPlayer()->GetObjectGuid(), player->GetObjectGuid());
}

/**
 * @brief Removes a member from the current guild.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildRemoveOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_REMOVE");

    std::string plName;
    ObjectGuid targetGuid;
    // Writer sub_C868E0 (thunk sub_C84F18).
    recvPacket.ReadGuidMask<7, 3, 4, 2, 5, 6, 1, 0>(targetGuid);
    recvPacket.ReadGuidBytes<0, 2, 5, 6, 7, 1, 4, 3>(targetGuid);

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_REMOVE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    if (!sObjectMgr.GetPlayerNameByGUID(targetGuid, plName))
    {
        return;
    }

    MemberSlot* slot = guild->GetMemberSlot(targetGuid);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    if (slot->RankId == GR_GUILDMASTER)
    {
        SendGuildCommandResult(GUILD_QUIT_S, "", ERR_GUILD_LEADER_LEAVE);
        return;
    }

    // do not allow to kick player with same or higher rights
    if (GetPlayer()->GetRank() >= slot->RankId)
    {
        SendGuildCommandResult(GUILD_QUIT_S, plName, ERR_GUILD_RANK_TOO_HIGH_S);
        return;
    }

    // do not delete guilds that level are higher than undeletable
    if (guild->GetMemberSize() == 1 && guild->GetLevel() >= sWorld.getConfig(CONGIG_UINT32_GUILD_UNDELETABLE_LEVEL))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_UNDELETABLE_DUE_TO_LEVEL);
        return;
    }

    ObjectGuid const removedGuid = slot->guid;
    std::string const removedName = slot->Name;

    // possible last member removed, do cleanup, and no need events
    if (guild->DelMember(removedGuid))
    {
        guild->Disband();
        delete guild;
        return;
    }

    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_UNINVITE_PLAYER,
        GetPlayer()->GetObjectGuid(), removedGuid);

    guild->BroadcastMemberRemoved(removedGuid, removedName,
        _player->GetObjectGuid(), _player->GetName());
}

/**
 * @brief Accepts a pending guild invitation.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildAcceptOpcode(WorldPacket& /*recvPacket*/)
{
    Guild* guild;
    Player* player = GetPlayer();

    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_ACCEPT");

    guild = sGuildMgr.GetGuildById(player->GetGuildIdInvited());
    if (!guild || player->GetGuildId())
    {
        return;
    }

    // not let enemies sign guild charter
    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) && player->GetTeam() != sObjectMgr.GetPlayerTeamByGUID(guild->GetLeaderGuid()))
    {
        return;
    }

    if (!guild->AddMember(GetPlayer()->GetObjectGuid(), guild->GetLowestRank()))
    {
        return;
    }
    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_JOIN_GUILD, GetPlayer()->GetObjectGuid());

    guild->BroadcastMemberJoined(player->GetObjectGuid(), player->GetName());
}

/**
 * @brief Declines a pending guild invitation.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildDeclineOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s", LookupOpcodeName(DIR_CLIENT, recvPacket.GetOpcode()));
    if (GetPlayer()->GetGuildId())
    {
        return;
    }

    if (Player* inviter = sObjectMgr.GetPlayer(GetPlayer()->GetGuildInviterGuid()))
    {
        inviter->SendGuildDeclined(GetPlayer()->GetName(), recvPacket.GetOpcode() == CMSG_GUILD_AUTO_DECLINE);
    }

    GetPlayer()->SetGuildIdInvited(0);
    GetPlayer()->SetGuildLevel(0);
}

/**
 * @brief Sends the guild roster to the current player.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildRosterOpcode(WorldPacket& recvPacket)
{
    // The body is two interleaved bit-packed guids, in the order the 18414
    // client's own serializer sub_C85E7C writes them -- but the VALUES are
    // meaningless and must never be used.
    //
    // Its constructor sub_C8590D sets only the vtable and +0x0C; it leaves the
    // sixteen bytes at +0x10..+0x1F alone, and both callers (sub_966847 and
    // sub_96A078) build the request on the stack and send it without filling
    // them. The client therefore serializes whatever was on its stack. That is
    // what the odd traffic is: values shaped like addresses, small leftovers such
    // as 288, identical bodies decoding identically because the stack is the same
    // at the same call site, and a body length of 4-6 bytes that varies purely
    // with how many junk bytes happened to be non-zero.
    //
    // Retail traffic says the same. Two requests from one session in
    // capture-000019 (sequences 852 and 5962, generation 2BE10C89...) are the
    // same player in the same guild, and four of their seventeen bytes differ --
    // real guids would be byte-identical. Their masks are FF FB, fifteen of
    // sixteen bytes present, where a guild guid such as 0x1FF7000000000001 has
    // three. Retail servers are handed the same rubbish.
    //
    // So this reads the body only to consume it correctly. The roster is served
    // from the session's own guild, which is the only trustworthy source.
    uint64 rawGuid1 = 0;
    uint64 rawGuid2 = 0;
    MopGuildPackets::ReadGuildRoster(recvPacket, rawGuid1, rawGuid2);

    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_ROSTER (body ignored: the client sends uninitialised stack here)");

    if (Guild* guild = sGuildMgr.GetGuildById(_player->GetGuildId()))
    {
        guild->Roster(this);
    }
}

/**
 * @brief Promotes a guild member to a higher rank.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildPromoteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_PROMOTE");

    std::string plName;
    ObjectGuid targetGuid;
    // Writer sub_C85476 (thunk sub_C849CD). The permutations below were wrong in
    // both runs and are re-derived from it; IDA and Binary Ninja agree exactly.
    recvPacket.ReadGuidMask<6, 0, 4, 3, 1, 7, 2, 5>(targetGuid);
    recvPacket.ReadGuidBytes<1, 7, 2, 5, 3, 4, 0, 6>(targetGuid);

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }
    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_PROMOTE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    if (!sObjectMgr.GetPlayerNameByGUID(targetGuid, plName))
    {
        return;
    }

    MemberSlot* slot = guild->GetMemberSlot(targetGuid);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    if (slot->guid == GetPlayer()->GetObjectGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_NAME_INVALID);
        return;
    }

    // allow to promote only to lower rank than member's rank
    // guildmaster's rank = 0
    // GetPlayer()->GetRank() + 1 is highest rank that current player can promote to
    if (GetPlayer()->GetRank() + 1 >= slot->RankId)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_RANK_TOO_HIGH_S);
        return;
    }

    uint32 newRankId = slot->RankId - 1;                    // when promoting player, rank is decreased

    slot->ChangeRank(newRankId);
    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_PROMOTE_PLAYER, GetPlayer()->GetObjectGuid(), slot->guid, newRankId);

    guild->BroadcastMemberRankUpdate(_player->GetObjectGuid(), slot->guid,
        newRankId, true);
}

/**
 * @brief Demotes a guild member to a lower rank.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildDemoteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_DEMOTE");

    std::string plName;
    ObjectGuid targetGuid;
    // Writer sub_C86553 (thunk sub_C84E1B). Note this permutation differs from
    // GUILD_PROMOTE's and GUILD_REMOVE's -- MoP randomises it per opcode, so no
    // sibling's order may be carried across.
    recvPacket.ReadGuidMask<3, 6, 0, 2, 7, 5, 4, 1>(targetGuid);
    recvPacket.ReadGuidBytes<7, 4, 2, 5, 1, 3, 0, 6>(targetGuid);

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());

    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_DEMOTE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    if (!sObjectMgr.GetPlayerNameByGUID(targetGuid, plName))
    {
        return;
    }

    MemberSlot* slot = guild->GetMemberSlot(targetGuid);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    if (slot->guid == GetPlayer()->GetObjectGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_NAME_INVALID);
        return;
    }

    // do not allow to demote same or higher rank
    if (GetPlayer()->GetRank() >= slot->RankId)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_RANK_TOO_HIGH_S);
        return;
    }

    // do not allow to demote lowest rank
    if (slot->RankId >= guild->GetLowestRank())
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_RANK_TOO_LOW_S);
        return;
    }

    uint32 newRankId = slot->RankId + 1;                    // when demoting player, rank is increased

    slot->ChangeRank(newRankId);
    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_DEMOTE_PLAYER, GetPlayer()->GetObjectGuid(), slot->guid, newRankId);

    guild->BroadcastMemberRankUpdate(_player->GetObjectGuid(), slot->guid,
        newRankId, false);
}

void WorldSession::HandleGuildSetRankOpcode(WorldPacket& recvPacket)
{
    // Rebuilt from writer sub_C866DC (thunk sub_C84EA8). The previous reader took
    // a rank id and two bit-packed GUIDs -- a member rank ASSIGNMENT -- which is
    // a different packet entirely. This one DEFINES a rank.
    //
    // No capture of this opcode exists in any build, so every field is named by
    // the builder route, from sub_9679B8 which fills the object the writer
    // serialises:
    //   the eight pairs are (tab RIGHTS, tab SLOTS-PER-DAY) -- the Lua setter
    //     SetGuildBankTabItemWithdraw writes the second array, clamped to 100000;
    //   rights is the field GuildControlGetRankFlags reads (client record +0),
    //     and the writer emits it TWICE, at +20 and +100;
    //   money-per-day is the field SetGuildBankWithdrawGoldLimit writes
    //     (record +12), emitted at +96.
    uint32 rankId, rights, moneyPerDay;
    uint32 tabRights[GUILD_BANK_MAX_TABS] = {};
    uint32 tabSlots[GUILD_BANK_MAX_TABS] = {};

    recvPacket >> rankId;                                   // client rank identifier
    for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
    {
        recvPacket >> tabRights[tab];
        recvPacket >> tabSlots[tab];
    }
    recvPacket >> moneyPerDay;
    recvPacket >> rights;
    recvPacket.read_skip<uint32>();                         // rights again, +100
    uint32 rankIndex;
    recvPacket >> rankIndex;

    uint32 const nameLen = recvPacket.ReadBits(7);
    if (recvPacket.rpos() + nameLen > recvPacket.size())
    {
        sLog.outError("CMSG_GUILD_SET_RANK: %s sent a %u byte rank name with %zu remaining, refusing",
                      GetPlayer()->GetGuidStr().c_str(), nameLen, recvPacket.size() - recvPacket.rpos());
        recvPacket.rfinish();
        return;
    }
    std::string rankName = recvPacket.ReadString(nameLen);

    DEBUG_LOG("WORLD: Received CMSG_GUILD_SET_RANK rank %u (index %u) rights %u money %u name %s",
              rankId, rankIndex, rights, moneyPerDay, rankName.c_str());

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    // Only the leader may redefine ranks. HasRankRight is not enough here: this
    // packet can grant any right to any rank, so anyone able to send it could
    // grant themselves everything.
    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    if (rankIndex >= guild->GetRanksSize())
    {
        return;
    }

    // The guildmaster rank IS editable -- the retail rank UI renames it, and
    // GuildDefaultRanks says so explicitly -- so it cannot simply be rejected.
    // But SetRankRights, unlike SetBankMoneyPerDay and SetBankRightsAndSlots,
    // does not force the guildmaster back to full rights, so a packet naming
    // rank 0 with rights=0 would lock the guild out of its own management.
    // Force the invariant instead of refusing the packet: the rename still
    // lands, the rights cannot be dropped.
    if (rankIndex == GR_GUILDMASTER)
    {
        // OR rather than assign: the load path does the same (Guild.cpp), and
        // assigning would strip any stored extension bits on what may be nothing
        // more than a rename.
        rights |= GR_RIGHT_ALL;
    }

    // Match the client's own ceiling. It clamps both of these to 100000 before
    // sending, so a larger value did not come from the stock UI; storing one
    // would grant effectively unlimited withdrawal.
    if (moneyPerDay > GUILD_WITHDRAW_MONEY_CLIENT_MAX)
    {
        moneyPerDay = GUILD_WITHDRAW_MONEY_CLIENT_MAX;
    }
    for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
    {
        if (tabSlots[tab] > GUILD_WITHDRAW_SLOTS_CLIENT_MAX)
        {
            tabSlots[tab] = GUILD_WITHDRAW_SLOTS_CLIENT_MAX;
        }
    }

    guild->SetRankName(rankIndex, rankName);
    guild->SetRankRights(rankIndex, rights);
    guild->SetBankMoneyPerDay(rankIndex, moneyPerDay);
    for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
    {
        guild->SetBankRightsAndSlots(rankIndex, tab, tabRights[tab], tabSlots[tab], true);
    }

    // Rank definitions are guild-wide state, so every member needs the update --
    // not just the leader who sent it. Roster(this) alone left everyone else
    // showing the old rank names and rights until they relogged.
    guild->BroadcastRankDefinitions();
}

void WorldSession::HandleGuildSwitchRankOpcode(WorldPacket& recvPacket)
{
    uint32 rankId;
    bool up;

    recvPacket >> rankId;
    up = recvPacket.ReadBit();

    DEBUG_LOG("WORLD: Received CMSG_GUILD_SWITCH_RANK rank %u up %u", rankId, up);

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    if (!guild->SwitchRank(rankId, up))
    {
        return;
    }

    // Reordering ranks changes two definitions, so every member needs them --
    // Roster alone carries only rank IDs. Same gap as CMSG_GUILD_SET_RANK had.
    guild->BroadcastRankDefinitions();
}

/**
 * @brief Removes the current player from the guild.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildLeaveOpcode(WorldPacket& /*recvPacket*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_LEAVE");

    Guild* guild = sGuildMgr.GetGuildById(_player->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (_player->GetObjectGuid() == guild->GetLeaderGuid() && guild->GetMemberSize() > 1)
    {
        SendGuildCommandResult(GUILD_QUIT_S, "", ERR_GUILD_LEADER_LEAVE);
        return;
    }

    // do not delete guilds that level are higher than undeletable
    if (guild->GetMemberSize() == 1 && guild->GetLevel() >= sWorld.getConfig(CONGIG_UINT32_GUILD_UNDELETABLE_LEVEL))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_UNDELETABLE_DUE_TO_LEVEL);
        return;
    }

    sCalendarMgr.RemoveGuildCalendar(_player->GetObjectGuid(), guild->GetId());

    if (_player->GetObjectGuid() == guild->GetLeaderGuid())
    {
        guild->Disband();
        delete guild;
        return;
    }

    SendGuildCommandResult(GUILD_QUIT_S, guild->GetName(), ERR_PLAYER_NO_MORE_IN_GUILD);

    if (guild->DelMember(_player->GetObjectGuid()))
    {
        guild->Disband();
        delete guild;
        return;
    }

    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_LEAVE_GUILD, _player->GetObjectGuid());

    guild->BroadcastMemberLeft(_player->GetObjectGuid(), _player->GetName());
}

/**
 * @brief Disbands the current guild if the player is its leader.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildDisbandOpcode(WorldPacket& /*recvPacket*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_DISBAND");

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    // do not delete guilds that level are higher than undeletable
    if (guild->GetMemberSize() == 1 && guild->GetLevel() >= sWorld.getConfig(CONGIG_UINT32_GUILD_UNDELETABLE_LEVEL))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_UNDELETABLE_DUE_TO_LEVEL);
        return;
    }

    guild->Disband();
    delete guild;

    DEBUG_LOG("WORLD: Guild Successfully Disbanded");
}

/**
 * @brief Transfers guild leadership to another member.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildLeaderOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_LEADER");

    std::string name = recvPacket.ReadString(recvPacket.ReadBits(7));

    Player* oldLeader = GetPlayer();

    // Promoting from the guild roster hands us "Name-Realm"; same strip the
    // guild INVITE path above already performs.
    StripHomeRealmSuffix(name);

    if (!normalizePlayerName(name))
    {
        return;
    }

    Guild* guild = sGuildMgr.GetGuildById(oldLeader->GetGuildId());

    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (oldLeader->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    MemberSlot* oldSlot = guild->GetMemberSlot(oldLeader->GetObjectGuid());
    if (!oldSlot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    MemberSlot* slot = guild->GetMemberSlot(name);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, name, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    if (slot->guid == oldLeader->GetObjectGuid())
    {
        return;
    }

    guild->SetLeader(slot->guid);
    // NOTE: GR_OFFICER might not actually be officer rank
    oldSlot->ChangeRank(GR_OFFICER);

    guild->BroadcastNewLeader(oldLeader->GetObjectGuid(), oldLeader->GetName(),
        slot->guid, slot->Name);
}

/**
 * @brief Updates the guild message of the day.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildMOTDOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_MOTD");

    // Writer sub_C872E6 -> sub_66B79F: sub_665185 writes the top EIGHT bits and
    // sub_664CD1 the low TWO, so the length field is 10 bits, not 11. Reading one
    // bit too many shifts the length and desynchronises the string after it.
    std::string MOTD = recvPacket.ReadString(recvPacket.ReadBits(10));

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }
    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_SETMOTD))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    guild->SetMOTD(MOTD);

    guild->BroadcastMotd(MOTD);
}

/**
 * @brief Updates a guild member's public note.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildSetNoteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_SET_NOTE");

    std::string name, note;
    ObjectGuid targetGuid;
    bool isPublic = false;

    // Parsed by MopGuildPackets::ParseGuildSetNote, which carries the derivation
    // and is driven by real captured bodies in mop_guild_packets. A set flag is
    // the PUBLIC note, so officer is its inverse.
    MopGuildPackets::ParseGuildSetNote(recvPacket, targetGuid, isPublic, note);
    bool const officer = !isPublic;

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (!guild->HasRankRight(GetPlayer()->GetRank(), officer ? GR_RIGHT_EOFFNOTE : GR_RIGHT_EPNOTE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    if (!sObjectMgr.GetPlayerNameByGUID(targetGuid, name))
    {
        return;
    }

    MemberSlot* slot = guild->GetMemberSlot(targetGuid);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, name, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    if (officer)
    {
        slot->SetOFFNOTE(note);
    }
    else
    {
        slot->SetPNOTE(note);
    }

    guild->Roster(this);
}


/**
 * @brief Adds a new guild rank.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildAddRankOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_ADD_RANK");

    recvPacket >> Unused<uint32>(); // rank id
    std::string rankname = recvPacket.ReadString(recvPacket.ReadBits(7));

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    if (guild->GetRanksSize() >= GUILD_RANKS_MAX_COUNT)     // client not let create more 10 than ranks
    {
        return;
    }

    guild->CreateRank(rankname, GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);

    guild->BroadcastRankDefinitions();
}

/**
 * @brief Deletes the lowest removable guild rank.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildDelRankOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_DEL_RANK");

    uint32 rankId;
    recvPacket >> rankId;

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    // do not allow delete rank if there are still members using it
    if (guild->HasMembersWithRank(rankId))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_RANK_IN_USE);
        return;
    }

    if (!guild->DelRank(rankId))
    {
        return;
    }

    guild->BroadcastRankDefinitions();
}

/**
 * @brief Sends the result of a guild command back to the client.
 *
 * @param typecmd The guild command type.
 * @param str The related player or guild name.
 * @param cmdresult The result code.
 */
void WorldSession::SendGuildCommandResult(uint32 typecmd, const std::string& str, uint32 cmdresult)
{
    WorldPacket data;
    if (!MopGuildPackets::BuildGuildCommandResult(data, typecmd, str, cmdresult))
    {
        sLog.outError("WORLD: Guild command result name is too long (%u bytes)", uint32(str.size()));
        return;
    }
    SendPacket(&data);

    DEBUG_LOG("WORLD: Sent (SMSG_GUILD_COMMAND_RESULT)");
}

/**
 * @brief Updates the guild information text.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildChangeInfoTextOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_INFO_TEXT");

    // Writer sub_C87297 -> sub_66B7DA: eight bits via sub_665185 plus three via
    // sub_664D4F, so 11 bits and not 12.
    std::string GINFO = recvPacket.ReadString(recvPacket.ReadBits(11));

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_MODIFY_GUILD_INFO))
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    guild->SetGINFO(GINFO);
}

/**
 * @brief Saves a new guild emblem design.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleSaveGuildEmblemOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_SAVE_GUILD_EMBLEM");

    MopGuildPackets::EmblemDesign const design = MopGuildPackets::ReadSaveGuildEmblem(recvPacket);
    ObjectGuid const vendorGuid(design.vendorGuid);

    Creature* pCreature = GetPlayer()->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_TABARDDESIGNER);
    if (!pCreature)
    {
        //"That's not an emblem vendor!"
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_INVALIDVENDOR);
        DEBUG_LOG("WORLD: HandleSaveGuildEmblemOpcode - %s not found or you can't interact with him.", vendorGuid.GetString().c_str());
        return;
    }

    // remove fake death
    if (GetPlayer()->hasUnitState(UNIT_STAT_DIED))
    {
        GetPlayer()->RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);
    }

    if (design.emblemStyle >= 196 || design.emblemColor >= 17 ||
        design.borderStyle >= 6 || design.borderColor >= 17 ||
        design.backgroundColor >= 51)
    {
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_INVALID_TABARD_COLORS);
        return;
    }

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        //"You are not part of a guild!";
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_NOGUILD);
        return;
    }

    if (guild->GetLeaderGuid() != GetPlayer()->GetObjectGuid())
    {
        //"Only guild leaders can create emblems."
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_NOTGUILDMASTER);
        return;
    }

    if (GetPlayer()->GetMoney() < 10 * GOLD)
    {
        //"You can't afford to do that."
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_NOTENOUGHMONEY);
        return;
    }

    GetPlayer()->ModifyMoney(-10 * GOLD);
    guild->SetEmblem(design.emblemStyle, design.emblemColor, design.borderStyle,
        design.borderColor, design.backgroundColor);

    //"Guild Emblem saved."
    SendSaveGuildEmblem(ERR_GUILDEMBLEM_SUCCESS);

    guild->Query(this);
}

/**
 * @brief Sends the guild event log to the current player.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::HandleGuildEventLogQueryOpcode(WorldPacket& /* recvPacket */)
{
    // empty
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_EVENT_LOG_QUERY)");

    if (uint32 GuildId = GetPlayer()->GetGuildId())
        if (Guild* pGuild = sGuildMgr.GetGuildById(GuildId))
        {
            pGuild->DisplayGuildEventLog(this);
        }
}

/******  GUILD BANK  *******/

void WorldSession::HandleGuildBankMoneyWithdrawn(WorldPacket& /* recv_data */)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_MONEY_WITHDRAWN)");

    if (uint32 GuildId = GetPlayer()->GetGuildId())
        if (Guild* pGuild = sGuildMgr.GetGuildById(GuildId))
        {
            pGuild->SendMoneyInfo(this, GetPlayer()->GetGUIDLow());
        }
}

void WorldSession::HandleGuildPermissions(WorldPacket& /* recv_data */)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_PERMISSIONS)");

    if (uint32 GuildId = GetPlayer()->GetGuildId())
    {
        if (Guild* pGuild = sGuildMgr.GetGuildById(GuildId))
        {
            uint32 rankId = GetPlayer()->GetRank();

            // Layout is fixed by retail capture (capture-000006 seq 1959, 83 bytes,
            // and 2,080 packets all exactly 83). The inherited body had three separate
            // faults, each visible in those bytes:
            //
            //   field order   bytes 0..15 are 05 00000000 07 00106053. Read as
            //                 rank/money/tabs/rights that is rank 5, no money left,
            //                 7 purchased tabs and a rights mask -- and the tab array
            //                 below indeed carries rights on 7 tabs and 0 on the 8th.
            //                 Read as the old rank/tabs/rights/money it claims 0
            //                 purchased tabs while still describing 7 of them.
            //   count width   bytes 16..18 are 00 00 40. At 21 bits that is 8, which is
            //                 GUILD_BANK_MAX_TABS. At the old 23 bits it is 32.
            //   tab pairing   the pairs run (4,3) (3,3) (2,3) (1,3) (0,3) (0,3) (0,3)
            //                 (0,0), i.e. remaining slots first and rights second, not
            //                 the other way round.
            uint32 remainingSlots[GUILD_BANK_MAX_TABS];
            uint32 tabRights[GUILD_BANK_MAX_TABS];
            for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
            {
                remainingSlots[tab] = pGuild->GetMemberSlotWithdrawRem(GetPlayer()->GetGUIDLow(), tab);
                tabRights[tab] = pGuild->GetBankRights(rankId, tab);
            }

            WorldPacket data;
            MopGuildPackets::BuildGuildPermissions(data, rankId,
                pGuild->GetMemberMoneyWithdrawRem(GetPlayer()->GetGUIDLow()),
                pGuild->GetPurchasedTabs(), pGuild->GetRankRights(rankId),
                remainingSlots, tabRights);
            SendPacket(&data);
            DEBUG_LOG("WORLD: Sent (SMSG_GUILD_PERMISSIONS)");
        }
    }
}

/* Called when clicking on Guild bank gameobject */
void WorldSession::HandleGuildBankerActivate(WorldPacket& recv_data)
{
    ObjectGuid goGuid;
    bool fullSlotRefresh = false;
    if (!MopCompactPackets::ReadGuildBankerActivate(
            recv_data, goGuid, fullSlotRefresh))
    {
        DEBUG_LOG("WORLD: Rejected malformed CMSG_GUILD_BANKER_ACTIVATE");
        return;
    }

    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANKER_ACTIVATE) FullSlotRefresh %u",
        uint32(fullSlotRefresh));

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        return;
    }

    if (uint32 GuildId = GetPlayer()->GetGuildId())
    {
        if (Guild* pGuild = sGuildMgr.GetGuildById(GuildId))
        {
            pGuild->DisplayGuildBankTabsInfo(this);         // this also will load guild bank if not yet
            return;
        }
    }

    SendGuildCommandResult(GUILD_UNK1, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
}

/* Called when opening guild bank tab only (first one) */
void WorldSession::HandleGuildBankQueryTab(WorldPacket& recv_data)
{
    // 18414 leads with the tab id; the "unk1" that followed the raw GUID is a
    // single bit inside the mask, asking for every slot rather than a page.
    uint8 TabId;
    bool sendAllSlots = false;
    ObjectGuid goGuid = MopCompactPackets::ReadGuildBankQueryTab(recv_data, TabId, sendAllSlots);

    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_QUERY_TAB) TabId %u", TabId);

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        return;
    }

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    if (TabId >= pGuild->GetPurchasedTabs())
    {
        return;
    }

    pGuild->DisplayGuildBankContent(this, TabId, sendAllSlots);
}

void WorldSession::HandleGuildBankDepositMoney(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_DEPOSIT_MONEY)");

    ObjectGuid goGuid;
    uint64 money = 0;
    if (!MopCompactPackets::ReadGuildBankDepositMoney(recv_data, money, goGuid))
    {
        DEBUG_LOG("WORLD: Rejected malformed CMSG_GUILD_BANK_DEPOSIT_MONEY");
        return;
    }

    if (!money)
    {
        return;
    }

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        return;
    }

    if (GetPlayer()->GetMoney() < money)
    {
        return;
    }

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    // Refuse while the bank state is untrusted: an earlier ambiguous commit left
    // memory possibly disagreeing with the rows and they could not be re-read,
    // so any arithmetic on the total would write a wrong one durably.
    if (!pGuild->IsBankStateTrusted())
    {
        sLog.outError("%s: refused for player %u -- guild %u bank state is untrusted "
            "after an unrecoverable commit; reload the guild",
            "CMSG_GUILD_BANK_DEPOSIT_MONEY", GetPlayer()->GetGUIDLow(), GuildId);
        return;
    }

    // Gated, as the mail money paths are. Without an active transaction the
    // writes below queue on the async thread while the recovery reads come from
    // a different pooled connection, so the recovery would adopt values those
    // writes then overwrite.
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("%s: could not begin a transaction for player %u",
            "CMSG_GUILD_BANK_DEPOSIT_MONEY", GetPlayer()->GetGUIDLow());
        return;
    }

    pGuild->SetBankMoney(pGuild->GetGuildBankMoney() + money);
    GetPlayer()->ModifyMoney(-int64(money));
    GetPlayer()->SaveGoldToDB();

    if (!CharacterDatabase.CommitTransactionDirect())
    {
        RecoverGuildBankMoneyAfterFailedCommit(this, pGuild, GuildId,
            "CMSG_GUILD_BANK_DEPOSIT_MONEY");
        return;
    }

    // logging money
    if (_player->GetSession()->GetSecurity() > SEC_PLAYER && sWorld.getConfig(CONFIG_BOOL_GM_LOG_TRADE))
    {
        sLog.outCommand(_player->GetSession()->GetAccountId(), "GM %s (Account: %u) deposit money (Amount: %u) to guild bank (Guild ID %u)",
                        _player->GetName(), _player->GetSession()->GetAccountId(), money, GuildId);
    }

    // log
    pGuild->LogBankEvent(GUILD_BANK_LOG_DEPOSIT_MONEY, uint8(0), GetPlayer()->GetGUIDLow(), money);

#ifdef ENABLE_ELUNA
    // TODO: ELUNAFIX NEEDED
    //if (Eluna* e = xxx->GetEluna())
    //{
    //    e->OnMemberDepositMoney(pGuild, GetPlayer(), money);
    //}
#endif

    BroadcastGuildBankMoney(pGuild);
}

void WorldSession::HandleGuildBankWithdrawMoney(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_WITHDRAW_MONEY)");

    ObjectGuid goGuid;
    uint64 money = 0;
    if (!MopCompactPackets::ReadGuildBankWithdrawMoney(recv_data, money, goGuid))
    {
        DEBUG_LOG("WORLD: Rejected malformed CMSG_GUILD_BANK_WITHDRAW_MONEY");
        return;
    }

    if (!money)
    {
        return;
    }

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        return;
    }

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (GuildId == 0)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    // Refuse while the bank state is untrusted: an earlier ambiguous commit left
    // memory possibly disagreeing with the rows and they could not be re-read,
    // so any arithmetic on the total would write a wrong one durably.
    if (!pGuild->IsBankStateTrusted())
    {
        sLog.outError("%s: refused for player %u -- guild %u bank state is untrusted "
            "after an unrecoverable commit; reload the guild",
            "CMSG_GUILD_BANK_WITHDRAW_MONEY", GetPlayer()->GetGUIDLow(), GuildId);
        return;
    }

    if (pGuild->GetGuildBankMoney() < money)                // not enough money in bank
    {
        return;
    }

    if (!pGuild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_WITHDRAW_GOLD))
    {
        return;
    }

    // Refuse before anything moves, the way every other money-in path in this
    // core does -- selling to a vendor and taking mail money both check the cap
    // and return with EQUIP_ERR_TOO_MUCH_GOLD before mutating. ModifyMoney
    // CLAMPS to MAX_MONEY_AMOUNT and only reports the error afterwards, so
    // debiting the bank first and adding second destroys the difference: a
    // player near the cap withdrawing a large balance empties the guild bank
    // durably and receives only what fits, with a clean commit and nothing to
    // tell either party the rest ceased to exist. The client cannot prevent it
    // either -- PickupGuildBankMoney checks the bank total and the absolute cap,
    // never the withdrawer's current money.
    if (GetPlayer()->GetMoney() >= MAX_MONEY_AMOUNT - money)
    {
        GetPlayer()->SendEquipError(EQUIP_ERR_TOO_MUCH_GOLD, nullptr, nullptr);
        return;
    }

    // Gated, as the mail money paths are. Without an active transaction the
    // writes below queue on the async thread while the recovery reads come from
    // a different pooled connection, so the recovery would adopt values those
    // writes then overwrite.
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("%s: could not begin a transaction for player %u",
            "CMSG_GUILD_BANK_WITHDRAW_MONEY", GetPlayer()->GetGUIDLow());
        return;
    }

    if (!pGuild->MemberMoneyWithdraw(money, GetPlayer()->GetGUIDLow()))
    {
        CharacterDatabase.RollbackTransaction();
        return;
    }

    GetPlayer()->ModifyMoney(money);
    GetPlayer()->SaveGoldToDB();

    if (!CharacterDatabase.CommitTransactionDirect())
    {
        RecoverGuildBankMoneyAfterFailedCommit(this, pGuild, GuildId,
            "CMSG_GUILD_BANK_WITHDRAW_MONEY");
        return;
    }

    // Log
    pGuild->LogBankEvent(GUILD_BANK_LOG_WITHDRAW_MONEY, uint8(0), GetPlayer()->GetGUIDLow(), money);

    BroadcastGuildBankMoney(pGuild);
}

void WorldSession::HandleGuildBankSwapItems(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_SWAP_ITEMS)");

    // FOUR player actions build this one opcode, and at 18414 they are four
    // different bodies -- 20, 21, 23 and 25 bytes are all observed at that build.
    // The inherited code read a raw GUID first and then branched on a plain
    // BankToBank byte; neither is where it thought, and both flags now live in
    // the bit stream, so the shape has to come out of the reader.
    MopCompactPackets::GuildBankSwapItems req;
    if (!MopCompactPackets::ReadGuildBankSwapItems(recv_data, req))
    {
        DEBUG_LOG("WORLD: Rejected malformed CMSG_GUILD_BANK_SWAP_ITEMS");
        return;
    }

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    // Closed ahead of this opcode being registered, because the consequence is
    // permanent. A tab purchase whose commit could not be confirmed can leave a
    // tab in memory with no guild_bank_tab row; storing an item into it writes a
    // guild_bank_item row with a TabId that LoadGuildBankFromDB then drops, and
    // the item is gone for good. Refuse while the bank state is untrusted.
    if (!pGuild->IsBankStateTrusted())
    {
        sLog.outError("CMSG_GUILD_BANK_SWAP_ITEMS: refused for player %u -- guild %u "
            "bank state is untrusted after an unrecoverable commit; reload the guild",
            GetPlayer()->GetGUIDLow(), GuildId);
        return;
    }

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(req.bankGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        return;
    }

    // Bank <-> Bank. bankTab/bankSlot is the DESTINATION on this path and
    // srcTab/srcSlot the source -- the opposite of what the field names in a
    // reference fork suggest. ReadGuildBankSwapItems carries the evidence.
    //
    // srcTab defaults to the client's own "none" value of 0xFF when the field is
    // absent, so a bank-to-bank body that names no source fails the tab bound
    // below rather than silently moving out of tab 0.
    if (req.bankToBank)
    {
        if (req.srcTab >= pGuild->GetPurchasedTabs() ||
                req.bankTab >= pGuild->GetPurchasedTabs() ||
                req.srcSlot >= GUILD_BANK_MAX_SLOTS ||
                req.bankSlot >= GUILD_BANK_MAX_SLOTS ||
                (req.srcTab == req.bankTab && req.srcSlot == req.bankSlot))
        {
            return;
        }

        pGuild->SwapItems(_player, req.srcTab, req.srcSlot, req.bankTab, req.bankSlot, req.splitAmount);
        return;
    }

    // Player <-> Bank. 0xFF stays legal for the bank slot here: it is the
    // client's "anywhere in this tab" for a deposit.
    if (req.bankTab >= pGuild->GetPurchasedTabs() ||
            (req.bankSlot >= GUILD_BANK_MAX_SLOTS && req.bankSlot != 0xFF))
    {
        return;
    }

    // The auto-store body omits both player-side fields and the reader leaves
    // them zero. NULL_BAG is itself 0 so the bag needs no translation, but slot 0
    // is a real slot while NULL_SLOT is 255.
    //
    // Without this line an auto-store is REFUSED, not misdirected: (NULL_BAG, 0)
    // is not an inventory position -- IsInventoryPos accepts bag 0 only with
    // NULL_SLOT or a backpack slot index -- so it fails the guard below and the
    // player is told the move is impossible. An earlier version of this comment
    // said the item would land in the first backpack slot instead; it would not.
    // The translation is still required, or auto-store never works at all.
    //
    // Key this on autoStore and never on the fields being absent. That absence
    // bit is INVERTED presence: the client omits playerSlot whenever it is zero,
    // so an ordinary deposit out of bag 19 slot 0 -- capture-000067 sequence
    // 550753 is one, at 20 bytes -- arrives with playerSlot absent and genuinely
    // meaning slot 0. Translating on absence would silently redirect it.
    uint8 playerBag = req.playerBag;
    uint8 playerSlot = req.playerSlot;
    if (req.autoStore)
    {
        playerBag = NULL_BAG;
        playerSlot = NULL_SLOT;
    }

    // allow work with inventory only
    if (!Player::IsInventoryPos(playerBag, playerSlot) &&
            !(playerBag == NULL_BAG && playerSlot == NULL_SLOT))
    {
        _player->SendEquipError(EQUIP_ERR_NONE, NULL, NULL);
        return;
    }

    if (req.toChar)                                         // Bank -> Char cases
    {
        pGuild->MoveFromBankToChar(_player, req.bankTab, req.bankSlot, playerBag, playerSlot, req.splitAmount);
    }
    else                                                    // Char -> Bank cases
    {
        pGuild->MoveFromCharToBank(_player, playerBag, playerSlot, req.bankTab, req.bankSlot, req.splitAmount);
    }
}

void WorldSession::HandleGuildBankBuyTab(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_BUY_TAB)");

    ObjectGuid goGuid;
    uint8 TabId = 0;
    if (!MopCompactPackets::ReadGuildBankBuyTab(recv_data, TabId, goGuid))
    {
        DEBUG_LOG("WORLD: Rejected malformed CMSG_GUILD_BANK_BUY_TAB");
        return;
    }

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        return;
    }

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    // Only the leader may buy a tab, and this is a privilege check rather than a
    // fidelity one. The purchase grants GUILD_BANK_RIGHT_FULL and
    // WITHDRAW_SLOT_UNLIMITED to the BUYER'S OWN RANK a few lines below. For the
    // guild master that does nothing -- rank 0 short-circuits true in
    // IsMemberHaveRights and SetBankRightsAndSlots already forces it full -- so
    // the grant only ever changes anything when the buyer is NOT the master,
    // which is precisely the case the client never offers: Blizzard_GuildBankUI
    // enables Purchase behind IsGuildLeader(), and BuyGuildBankTab() takes no
    // arguments. Without this, any member who could afford a tab could buy the
    // guild's first one and hand their own rank unlimited withdrawal on it.
    if (GetPlayer()->GetObjectGuid() != pGuild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    // Same gate the money handlers carry, and this one is load-bearing rather
    // than diagnostic -- do not remove it on the assumption that the
    // TabId != GetPurchasedTabs() check below already refuses. It does not.
    // CreateNewBankTab pushes onto m_TabListMap before the commit and
    // GetPurchasedTabs() is that vector's size, so an ambiguous commit leaves the
    // count already incremented: the leader relogs, the client offers tab N+1,
    // and N+1 != N+1 is false, so the retry sails through and buys a second tab
    // on top of a phantom one.
    if (!pGuild->IsBankStateTrusted())
    {
        sLog.outError("CMSG_GUILD_BANK_BUY_TAB: refused for player %u -- guild %u "
            "bank state is untrusted after an unrecoverable commit; reload the guild",
            GetPlayer()->GetGUIDLow(), GuildId);
        return;
    }

    // m_PurchasedTabs = 0 when buying Tab 0, that is why this check can be made
    // also don't allow buy tabs that are obtained through guild perks
    if (TabId != pGuild->GetPurchasedTabs() || TabId >= GUILD_BANK_MAX_BOUGHT_TABS)
    {
        return;
    }

    uint64 TabCost = GetGuildBankTabPrice(TabId) * GOLD;
    if (!TabCost)
    {
        return;
    }

    if (GetPlayer()->GetMoney() < TabCost)                  // Should not happen, this is checked by client
    {
        return;
    }

    // The tab rows, the gold and the rank's rights on the new tab are one
    // purchase and must land together. They did not: the tab rows went out in
    // CreateNewBankTab's own sub-transaction, the rights as a bare PExecute, and
    // the gold was only a memory change left for the next periodic save. Nothing
    // tied them, so a failure between them left a tab paid for and not created,
    // or created and not paid for.
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("CMSG_GUILD_BANK_BUY_TAB: could not begin a transaction for player %u",
            GetPlayer()->GetGUIDLow());
        return;
    }

    pGuild->CreateNewBankTab();
    GetPlayer()->ModifyMoney(-int64(TabCost));
    GetPlayer()->SaveGoldToDB();
    // GR_GUILDMASTER, not the buyer's rank. Those are the same thing only while
    // the leader sits at rank 0, and that invariant is not this file's to rely
    // on: .guild rank calls ChangeRank on any member with no leader check and no
    // repair, so a GM can move the leader to rank 3 while m_LeaderGuid stays put.
    // The gate above would still pass, and granting to the buyer's rank would
    // then hand every rank-3 member full unlimited rights on the new tab -- the
    // exact escalation the gate exists to stop. Naming the rank says what a tab
    // purchase actually means and cannot drift.
    pGuild->SetBankRightsAndSlots(GR_GUILDMASTER, TabId, GUILD_BANK_RIGHT_FULL, WITHDRAW_SLOT_UNLIMITED, true);

    if (!CharacterDatabase.CommitTransactionDirect())
    {
        // Same ambiguity as the money handlers, with more state behind it: the
        // purchased-tab count, the tab list entry and the rank's rights on that
        // tab are all published to memory by now. Only the player's gold is
        // re-read: the tab and its rights could be read back too, but correcting
        // them in memory is the thing that cannot be done safely, since popping a
        // tab whose commit actually landed creates the inverse phantom. So adopt
        // the gold and mark the whole bank state untrusted, which stops money AND
        // item operations until a reload settles what really happened.
        if (QueryResult* durable = CharacterDatabase.PQuery(
            "SELECT `money` FROM `characters` WHERE `guid` = '%u'", GetPlayer()->GetGUIDLow()))
        {
            GetPlayer()->SetMoney((*durable)[0].GetUInt64());
            delete durable;
        }
        else
        {
            SuppressCharacterSave();
        }

        pGuild->MarkBankStateUntrusted();
        sLog.outError("CMSG_GUILD_BANK_BUY_TAB: commit could not be confirmed for player %u "
            "and guild %u; the guild's bank state is untrusted until it is reloaded",
            GetPlayer()->GetGUIDLow(), GuildId);
        KickPlayer();
        return;
    }

    pGuild->Roster();                                       // broadcast for tab rights update
    pGuild->DisplayGuildBankTabsInfo(this, TabId);
}

void WorldSession::HandleGuildBankUpdateTab(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_UPDATE_TAB)");

    // The inherited reader took a raw eight-byte GUID, then the tab, then two
    // NUL-terminated strings. At 18414 the tab leads as a plain byte, the GUID
    // is packed, both lengths live in the bit stream, and the strings are NOT
    // NUL terminated -- so every field it read was wrong. See
    // MopCompactPackets::ReadGuildBankUpdateTab for the derivation.
    ObjectGuid goGuid;
    uint8 TabId = 0;
    std::string Name;
    std::string IconIndex;

    if (!MopCompactPackets::ReadGuildBankUpdateTab(recv_data, TabId, Name, IconIndex, goGuid))
    {
        DEBUG_LOG("WORLD: CMSG_GUILD_BANK_UPDATE_TAB body rejected");
        return;
    }

    if (Name.empty())
    {
        return;
    }

    if (IconIndex.empty())
    {
        return;
    }

    // `guild_bank_tab`.`TabName` and `.TabIcon` are both varchar(100), in the
    // database repository at Character/Setup/characterLoadDB.sql:101223. Refuse
    // rather than hand the column something it would silently truncate: a
    // truncated icon path renders as a missing texture, and whether anything is
    // logged depends on SQL mode -- strict mode rejects the UPDATE and
    // DatabaseMysql reports it, non-strict truncates silently.
    //
    // These are not equal units. The table is CHARSET=utf8, so varchar(100) holds
    // 100 CHARACTERS -- up to 300 bytes -- while the comparison below is on bytes.
    // The guard is therefore deliberately conservative rather than exact, which
    // costs little because neither bound is reachable through the stock UI: it
    // sanitises a name to 15 characters, and its icon picker sends a bare macro
    // filename of about thirty bytes. The Lua binding itself is less restrained --
    // SetGuildBankTabInfo accepts any non-empty icon string and its copy loop
    // permits 256 bytes -- so the icon limb IS reachable by a modified client,
    // which is the case this guard exists for. Only that limb can fire at all,
    // the reader already capping the name at 64; the name limb is kept so the
    // guard still holds if that cap is ever widened. Do not "simplify" the
    // sanitiser's 16 to a byte count -- 16 characters is not 16 bytes.
    if (Name.size() > 100 || IconIndex.size() > 100)
    {
        DEBUG_LOG("WORLD: CMSG_GUILD_BANK_UPDATE_TAB refused, name %zu / icon %zu bytes exceeds storage",
                  Name.size(), IconIndex.size());
        return;
    }

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
    {
        return;
    }

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    if (TabId >= pGuild->GetPurchasedTabs())
    {
        return;
    }

    if (!pGuild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_MODIFY_BANK_TABS))
    {
        return;
    }

    pGuild->SetGuildBankTabInfo(TabId, Name, IconIndex);

    // Every online member is told, not just the actor. A tab name is shared
    // state, and a rename only its author can see leaves everyone else reading a
    // stale label until they reopen the bank.
    //
    // 0x0BF1 carries this. Two earlier versions of this comment claimed nothing
    // confirmed that, reasoning from the absence of any capture at 18414 and from
    // the fork-sourced name in Opcodes.h. Both were wrong, because neither asked
    // the client. Its INBOUND parser settles it: sub_6A224B reads exactly the
    // shape BuildGuildBankTabModified writes, and sub_96ED66 takes the result,
    // sanitises the name through the same 16-character limiter the outbound path
    // uses, stores both strings in the tab cache at 0x11F4140, and raises event
    // 0x1AF. The step that binds the VALUE to that parser -- the guild SMSG
    // dispatcher sub_68EC4C and its two tables -- is spelled out on the opcode's
    // own line in Opcodes.h, because it is the part a re-derivation cannot guess:
    // sub_6A224B holds no opcode literal, and the only push 0xbf1 in the image is
    // an FMOD line number. (Not Opcodes_reference.h: its rows are generated and
    // carry only the status overlay.) Corpus silence is not evidence of absence
    // when the binary itself can be asked.
    ByteBuffer tabModified;
    if (MopGuildBankPackets::BuildGuildBankTabModified(tabModified, TabId, Name, IconIndex))
    {
        WorldPacket modified(SMSG_GUILD_EVENT_BANK_TAB_MODIFIED, tabModified.size());
        modified.append(tabModified.contents(), tabModified.size());
        pGuild->BroadcastPacket(&modified);
    }
    else
    {
        // Unreachable behind the guards above. By this point the rename is applied
        // in memory and its UPDATE has been QUEUED -- not committed: once the world
        // has loaded, AllowAsyncTransactions is on and Database::Execute only hands
        // the statement to the delay thread, whose result nobody checks. So what a
        // silent skip here would cost is the notification, not the rename: every
        // member, the actor included, would hold the old name until they reopened
        // the bank, with nothing in the log to explain it.
        sLog.outError("CMSG_GUILD_BANK_UPDATE_TAB: guild %u tab %u renamed but the "
            "event body was refused; members will hold a stale name until they reopen",
            GuildId, uint32(TabId));
    }

    // No bank list follows. The inherited handler sent one, and an earlier
    // version of this comment justified keeping it by claiming the event only
    // refreshed cached metadata while the list did the repainting. That is
    // backwards. Event 0x1AF is GUILDBANK_UPDATE_TABS, and its handler at
    // Blizzard_GuildBankUI.lua:125 calls GuildBankFrame_SelectAvailableTab(),
    // which calls GuildBankFrame_UpdateTabs() and GuildBankFrame_Update() on
    // every path -- so the event repaints the frame by itself, from the tab
    // cache sub_96ED66 has just written. BroadcastPacket includes the actor, so
    // the renamer is repainted by the same packet as everyone else.
    //
    // A bank list additionally ships every slot of the tab, which a rename has
    // not changed. There is no capture of this exchange at 18414 to say what
    // retail sends, so the choice is between a send that is provably sufficient
    // and one that is merely inherited; keeping the extra list would be assuming
    // evidence rather than having it.
}

void WorldSession::HandleGuildBankLogQuery(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_LOG_QUERY)");

    uint32 TabId;
    recv_data >> TabId;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    // GUILD_BANK_MAX_TABS send by client for money log
    if (TabId >= pGuild->GetPurchasedTabs() && TabId != GUILD_BANK_MAX_TABS)
    {
        return;
    }

    pGuild->DisplayGuildBankLogs(this, TabId);
}

void WorldSession::HandleQueryGuildBankTabText(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_QUERY_GUILD_BANK_TEXT");

    uint32 TabId;
    recv_data >> TabId;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    if (TabId >= pGuild->GetPurchasedTabs())
    {
        return;
    }

    pGuild->SendGuildBankTabText(this, TabId);
}

void WorldSession::HandleSetGuildBankTabText(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_SET_GUILD_BANK_TEXT");

    uint32 TabId;
    std::string Text;

    recv_data >> TabId;
    Text = recv_data.ReadString(recv_data.ReadBits(14));

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        return;
    }

    if (TabId >= pGuild->GetPurchasedTabs())
    {
        return;
    }

    pGuild->SetGuildBankTabText(TabId, Text);
}

/**
 * @brief Sends the result of a guild emblem save request.
 *
 * @param msg The guild emblem result code.
 */
void WorldSession::SendSaveGuildEmblem(uint32 msg)
{
    WorldPacket data;
    MopGuildPackets::BuildSaveGuildEmblemResult(data, msg);
    SendPacket(&data);
}

void WorldSession::HandleGuildQueryRanksOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received CMSG_GUILD_QUERY_RANKS");

    // Order is taken from the 18414 client's own send serializer sub_C860F3, which
    // writes the mask bits from guid bytes 0,2,5,4,3,7,6,1 and then the present bytes
    // in order 6,0,1,7,3,2,5,4 (each XOR 1). The order this handler inherited from
    // MaNGOS Three is a different permutation, so it decoded the wrong guild.
    ObjectGuid guildGuid(MopGuildPackets::ReadGuildQueryRanks(recv_data));
    Guild* guild = sGuildMgr.GetGuildByGuid(guildGuid);
    if (!guild || guild->GetId() != GetPlayer()->GetGuildId() ||
        !guild->GetMemberSlot(GetPlayer()->GetObjectGuid()))
    {
        return;
    }

    guild->QueryRanks(this);
}

void WorldSession::HandleGuildRequestPartyStateOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received CMSG_GUILD_REQUEST_PARTY_STATE");

    ObjectGuid guildGuid(MopGuildPackets::ReadGuildRequestPartyState(recv_data));
    Guild* guild = sGuildMgr.GetGuildByGuid(guildGuid);
    if (!guild || guild->GetId() != GetPlayer()->GetGuildId() ||
        !guild->GetMemberSlot(GetPlayer()->GetObjectGuid()))
    {
        return;
    }

    // Required and the multiplier belong to the guild-group bonus, which does
    // not exist in this core, so the honest reply is that the group is not one:
    // the flag is false and both of those are zero. Hiding behind the flag is
    // not a reason to be careless with the rest -- InGuildParty (sub_9658A6)
    // pushes all four values every call, so an addon reads the count whatever
    // the flag says. Only the stock GuildInstanceDifficulty frame is gated on it
    // (MiniMapInstanceDifficulty_Update).
    //
    // So present has to mean what it says: guild members actually here with the
    // requester. Someone in another dungeon is not present with you, whatever
    // the group roster says, hence the map and instance filter. Ungrouped is
    // zero rather than one -- 20921 of the 62866 corpus replies are thirteen
    // zero bytes, which is consistent with that though it does not on its own
    // prove those sessions were ungrouped.
    uint32 numPresent = 0;
    if (Group* group = GetPlayer()->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr != NULL; itr = itr->next())
        {
            // IsInWorld before the ids: GetMapId and GetInstanceId are cached
            // fields, so a member taken out of the world for a far transfer
            // still reports the map it left and would otherwise be counted as
            // standing here.
            Player* member = itr->getSource();
            if (member && member->IsInWorld() &&
                member->GetGuildId() == guild->GetId() &&
                member->GetMapId() == GetPlayer()->GetMapId() &&
                member->GetInstanceId() == GetPlayer()->GetInstanceId())
            {
                ++numPresent;
            }
        }
    }

    WorldPacket data;
    MopGuildPackets::BuildGuildPartyState(data, 0, 0.0f, numPresent, false);
    SendPacket(&data);
}

void WorldSession::HandleGuildRequestChallengeUpdateOpcode(WorldPacket& /*recv_data*/)
{
    DEBUG_LOG("WORLD: Received CMSG_GUILD_REQUEST_CHALLENGE_UPDATE");

    // No body to read. The client's writer for this message is nullsub_2 --
    // vtable off_EBDA08 slot +4, with slot +8 the thunk pushing 0x147A -- and
    // all 166 requests in the 18414 corpus are zero bytes.

    // This core has no guild challenges: no definitions, no per-guild counters,
    // no weekly reset. Answering with retail's reward table and zero progress
    // would advertise five challenges that can never be completed here, so the
    // reply is empty instead. Zero in the max count is what makes it empty
    // rather than merely unstarted: GetNumGuildChallenges counts only types
    // whose max is above zero, so GuildInfoFrame_UpdateChallenges loops zero
    // times and never fills in a progress count or tooltip. The five rows
    // themselves are static XML ($parentChallenge1..5) and keep their labels --
    // an empty reply does not remove them, and neither does sending nothing.
    //
    // Filling these five arrays from the retail table quoted in
    // BuildGuildChallengeUpdate is all that is needed once challenges exist.
    uint32 const none[GUILD_CHALLENGE_TYPES] = {};

    WorldPacket data;
    MopGuildPackets::BuildGuildChallengeUpdate(data, none, none, none, none, none);
    SendPacket(&data);
}

void WorldSession::HandleGuildSetAchievementTracking(WorldPacket& recvPacket)
{
    std::vector<uint32> achievementIds;
    if (!MopGuildPackets::ReadGuildAchievementTracking(recvPacket, achievementIds))
        return;

    // The current core has no guild-achievement tracking backend. Parse the
    // client's complete snapshot only as a bounded compatibility sink.
    // Intentionally no persistence or gameplay side effects.
    (void)achievementIds;
}

void WorldSession::HandleGuildAutoDeclineToggleOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received CMSG_GUILD_AUTO_DECLINE_TOGGLE");

    bool apply;
    recv_data >> apply;

    GetPlayer()->ApplyModFlag(PLAYER_FLAGS, PLAYER_FLAGS_AUTO_DECLINE_GUILDS, apply);
}
