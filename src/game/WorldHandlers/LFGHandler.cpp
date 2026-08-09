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
 * @file LFGHandler.cpp
 * @brief Looking For Group (Meeting Stone) opcode handlers
 *
 * This file handles player interactions with meeting stones (LFG system).
 * Meeting stones allow players/groups to queue for dungeons and be matched
 * with other players automatically.
 *
 * Opcodes handled:
 * - CMSG_MEETINGSTONE_JOIN: Join LFG queue at a meeting stone
 * - CMSG_MEETINGSTONE_LEAVE: Leave LFG queue
 * - CMSG_MEETINGSTONE_INFO: Request current queue status
 *
 * @see LFGMgr for the queue management implementation
 * @see LFGQueue for matching algorithm
 */

#include "WorldSession.h"
#include "DBCStores.h"
#include "Group.h"
#include "LFGMgr.h"
#include "LFGStatePolicy.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include "ObjectMgr.h"
#include "World.h"


void WorldSession::HandleLfrJoinOpcode(WorldPacket& recv_data)
{
    MopLfgPackets::LfrSearchRequest request;
    if (!MopLfgPackets::ParseLfrSearchRequest(recv_data, request))
    {
        sLog.outError("WORLD: malformed CMSG_LFG_LFR_JOIN from %s",
            GetPlayerName());
        return;
    }

    LfgDungeonsEntry const* dungeon =
        sLfgDungeonsStore.LookupEntry(request.lfgId);
    if (!dungeon || dungeon->TypeID != request.typeId)
    {
        sLog.outError("WORLD: invalid CMSG_LFG_LFR_JOIN key %u:%u from %s",
            uint32(request.typeId), request.lfgId, GetPlayerName());
        return;
    }

    WorldPacket data(SMSG_LFG_UPDATE_SEARCH, 37);
    MopLfgPackets::BuildEmptyLfrSearchResponse(data, request);
    SendPacket(&data);
}

void WorldSession::HandleLfrLeaveOpcode(WorldPacket& recv_data)
{
    MopLfgPackets::LfrSearchRequest request;
    if (!MopLfgPackets::ParseLfrSearchRequest(recv_data, request))
    {
        sLog.outError("WORLD: malformed CMSG_LFG_LFR_LEAVE from %s",
            GetPlayerName());
        return;
    }

    LfgDungeonsEntry const* dungeon =
        sLfgDungeonsStore.LookupEntry(request.lfgId);
    if (!dungeon || dungeon->TypeID != request.typeId)
    {
        sLog.outError("WORLD: invalid CMSG_LFG_LFR_LEAVE key %u:%u from %s",
            uint32(request.typeId), request.lfgId, GetPlayerName());
        return;
    }

    DEBUG_LOG("WORLD: recognized CMSG_LFG_LFR_LEAVE key %u:%u from %s",
        uint32(request.typeId), request.lfgId, GetPlayerName());
}

void WorldSession::HandleLfgJoinOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_LFG_JOIN");

    // The inherited body was the 3.3.5 shape and shared no field with 18414.
    // See MopCompactPackets::ReadLfgJoin for the layout and for why the dungeon
    // count is bounded before anything is read.
    uint8 partyIndex = 0;
    uint32 roles = 0;
    uint32 flag = 0;
    std::string comment;
    std::vector<uint32> dungeons;

    if (!MopCompactPackets::ReadLfgJoin(recv_data, partyIndex, roles, flag, dungeons, comment))
    {
        sLog.outError("Malformed CMSG_LFG_JOIN body from %s: its declared dungeon "
                      "count and comment length do not account for its size.",
                      GetPlayerName());
        return;
    }

    // The high byte of each slot is the LFG type tag, not part of the id.
    for (size_t i = 0; i < dungeons.size(); ++i)
    {
        dungeons[i] &= 0x00FFFFFF;
    }

    DEBUG_LOG("CMSG_LFG_JOIN: %s roles %u, %u dungeon(s), comment %u byte(s).",
              GetPlayerName(), roles, uint32(dungeons.size()), uint32(comment.size()));

    if (dungeons.empty())
    {
        return;
    }

    // The two lines that stood here were commented-out 3.3.5 session sends with
    // the wrong arity for today's signatures; they would not have compiled, let
    // alone worked. The real entry point is LFGMgr::JoinLFG, which had no
    // callers at all, which is why nothing happened when a player queued.
    //
    // Safe to wire now: the matchmaker is reached ONLY through
    // LFGMgr::FindQueueMatches, which is called only from LFGMgr::Update, and
    // nothing ever checks the WUPDATE_LFGMGR timer. So this enters the player
    // into the queue and stops there. It does not wake MergeGroups, whose
    // needed-role arithmetic is still wrong -- LFGMgr.cpp gates role needs on
    // DifficultyID == 0 and no TypeID==1 row in LfgDungeons.dbc carries that,
    // so every entry reports needing nobody and any two would be matched.
    //
    // SMSG_LFG_JOIN_RESULT is now built to the 18414 layout and admitted, so a
    // refused join reaches the player. See MopLfgPackets::BuildJoinResult for the
    // three captures it is pinned to.
    Player* plr = GetPlayer();
    if (!plr)
    {
        return;
    }

    Group* pGroup = plr->GetGroup();
    if (!LFGStatePolicy::CanMutateGroupQueue(
            pGroup != nullptr, pGroup && pGroup->IsLeader(plr->GetObjectGuid())))
    {
        return;
    }

    std::set<uint32> requested(dungeons.begin(), dungeons.end());
    sLFGMgr.JoinLFG(roles, requested, comment, plr);
}

void WorldSession::HandleLfgLeaveOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_LFG_LEAVE");

    // The body is a ticket echo -- type, flags, time, queue id and a packed
    // GUID. None of it is authority: the server cancels the CALLER's own queue
    // entry, so the ticket only says what the client believes it is leaving.
    // It is parsed rather than skipped so a malformed body is refused instead
    // of silently cancelling something.
    MopLfgLeavePackets::Request request;
    if (!MopLfgLeavePackets::ParseRequest(recv_data, request))
    {
        return;
    }

    Player* plr = GetPlayer();
    if (!plr)
    {
        return;
    }

    // A grouped queue is keyed by the group GUID and can only be mutated by its leader.
    // A non-leader must not be sent down a solo path either: there is no player-keyed
    // entry to cancel, and reporting success would leave the actual party queue alive.
    Group* pGroup = plr->GetGroup();
    if (!LFGStatePolicy::CanMutateGroupQueue(
            pGroup != nullptr, pGroup && pGroup->IsLeader(plr->GetObjectGuid())))
    {
        return;
    }

    sLFGMgr.LeaveLFG(plr, pGroup != nullptr);
}

void WorldSession::HandleLfgSetRolesOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_LFG_SET_ROLES");

    // This is the reply half of the LFG role check, and it had no handler and no
    // registration at all -- the client's answer was dropped at the dispatcher without
    // so much as a log line, so a party entered LFG_STATE_ROLECHECK and stayed there.
    //
    // Note the Lua SetLFGRoles() does NOT send this; it only mutates local state. The
    // packet is emitted by CompleteLFGRoleCheck, i.e. when the player confirms.
    MopLfgSetRolesPackets::Request request;
    if (!MopLfgSetRolesPackets::ParseRequest(recv_data, request))
    {
        sLog.outError("Malformed CMSG_LFG_SET_ROLES body from %s: expected 5 bytes.",
                      GetPlayerName());
        return;
    }

    Player* plr = GetPlayer();
    if (!plr)
    {
        return;
    }

    // A role check only exists for a party. A solo queuer states their roles in
    // CMSG_LFG_JOIN and never reaches this path.
    Group* pGroup = plr->GetGroup();
    if (!pGroup)
    {
        return;
    }

    DEBUG_LOG("CMSG_LFG_SET_ROLES: %s roles 0x%02X.", GetPlayerName(), request.roles);

    // Truncated to the byte the role plumbing uses. The wire field is 32 bits, but only
    // the low four (leader/tank/healer/damage) are ever set; anything above them is
    // rejected by PerformRoleCheck's mask test rather than being silently accepted.
    sLFGMgr.PerformRoleCheck(plr, pGroup, uint8(request.roles & 0xFF));
}

void WorldSession::HandleLfgProposalResponseOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_LFG_PROPOSAL_RESPONSE");

    // Without this a proposal could be built and sent but never answered -- the accept
    // and decline buttons both did nothing, because the reply was dropped at the
    // dispatcher with no handler and no registration.
    MopLfgProposalResponsePackets::Request request;
    if (!MopLfgProposalResponsePackets::ParseRequest(recv_data, request))
    {
        sLog.outError("Malformed CMSG_LFG_PROPOSAL_RESPONSE body from %s.", GetPlayerName());
        return;
    }

    Player* plr = GetPlayer();
    if (!plr)
    {
        return;
    }

    DEBUG_LOG("CMSG_LFG_PROPOSAL_RESPONSE: %s %s proposal %u.",
              GetPlayerName(), request.accepted ? "accepted" : "declined", request.proposalId);

    // Answer on behalf of the CALLER, keyed on our own proposal id. The GUIDs and the
    // queue triplet in the body are echoes of what we sent and carry no authority; a
    // client that returns a different guidA must not be able to answer for someone else.
    sLFGMgr.ProposalUpdate(request.proposalId, plr->GetObjectGuid(), request.accepted);
}

void WorldSession::HandleLfgGetStatusOpcode(WorldPacket& /*recv_data*/)
{
    DEBUG_LOG("CMSG_LFG_GET_STATUS");

    LFGPlayerStatus status = sLFGMgr.GetPlayerStatus(GetPlayer()->GetObjectGuid());
    if (status.state == LFG_STATE_NONE)
        return;

    // Exactly ONE packet, with the dungeon list PRESENT.
    //
    // This used to send a second copy with dungeonList.clear(). No such body exists in
    // retail traffic: 0 of 5291 observed SMSG_LFG_UPDATE_STATUS carry an empty dungeon
    // list. It was the old 3.3.5 UPDATE_PARTY/UPDATE_PLAYER pair, and 5.4.8 has a
    // single opcode. Retail's reply to the zone-in probe is one reason-15 body that
    // still lists the dungeons (capture-000720 seq 1286, reproduced at capture-000044
    // seq 6354, capture-000656 seq 113708 and capture-000872 seq 14299).
    //
    // The LFG_STATE_NONE early return above is also correct and must stay: 1598 of 2144
    // GET_STATUS probes draw no reply at all, and none of the 504 post-completion or
    // post-leave probes do.
    status.updateType = LFG_UPDATE_STATUS;
    SendLfgUpdate(GetPlayer()->GetGroup() != nullptr, status);
}

/**
 * @brief A player's answer to an in-progress vote kick.
 *
 * The body is ONE BIT and nothing else, derived from the 18414 writer: the packet
 * class at vtable 0xD63364, whose header virtual sub_661F56 writes opcode 6078
 * (0x17BE) and whose body writer sub_688B4B is exactly
 *
 *     WriteBit(this->agree); FlushBits();
 *
 * One byte on the wire, 0x80 for agree and 0x00 for deny, because ReadBit is
 * MSB-first. There is no GUID, no length and no second field, so the client tells
 * us only HOW the player voted. WHICH boot it belongs to has to come entirely from
 * server state: the sending session identifies the voter, and the voter's group
 * identifies the boot.
 *
 * @param recv_data The received opcode packet.
 */
void WorldSession::HandleLfgBootPlayerVoteOpcode(WorldPacket& recv_data)
{
    if (recv_data.size() - recv_data.rpos() != 1)
    {
        sLog.outError("WORLD: malformed CMSG_LFG_BOOT_PLAYER_VOTE from %s", GetPlayerName());
        return;
    }

    bool const agree = recv_data.ReadBit();

    if (!GetPlayer())
    {
        return;
    }

    DEBUG_LOG("CMSG_LFG_BOOT_PLAYER_VOTE: %s voted %s",
              GetPlayer()->GetGuidStr().c_str(), agree ? "agree" : "deny");

    sLFGMgr.CastVote(GetPlayer(), agree);
}

void WorldSession::HandleLfgTeleportOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_LFG_TELEPORT");

    // The body is ONE BIT, MSB-first, not a uint8. All 47 corpus events are a single
    // byte carrying only 0x80 or 0x00, and the destination map of the SMSG_TRANSFER_PENDING
    // that follows classifies them: 0x80 precedes a move to an outdoor map (0, 530, 571,
    // 870, 974) and 0x00 precedes a move to an instance (70, 547, 556, 558, 574, 575,
    // 599, 600, 960, 1004, 1098, 1136). So 0x80 is OUT and 0x00 is back IN -- 0x00 is not
    // a leave. capture-000059 seqs 1038789..1040642 are all 0x00 with prevMap 960 and
    // destMap 960, i.e. re-summons into the same instance.
    //
    // A reader switching on 0 and 1 would match neither value.
    if (recv_data.size() - recv_data.rpos() != 1)
    {
        sLog.outError("WORLD: malformed CMSG_LFG_TELEPORT from %s", GetPlayerName());
        return;
    }

    bool const out = recv_data.ReadBit();

    Player* plr = GetPlayer();
    if (!plr)
    {
        return;
    }

    sLFGMgr.TeleportPlayer(plr, out);
}

void WorldSession::HandleLfgLockInfoRequestOpcode(WorldPacket& recv_data)
{
    bool forPlayer = false;
    if (!MopLfgPackets::ParseLockInfoRequest(recv_data, forPlayer))
    {
        sLog.outError("WORLD: malformed CMSG_LFG_LOCK_INFO_REQUEST from %s",
            GetPlayerName());
        return;
    }

    if (forPlayer)
        SendLfgPlayerLockInfo();
    else
        SendLfgPartyLockInfo();
}

void WorldSession::SendLfgPlayerLockInfo()
{
    Player* plr = GetPlayer();
    if (!plr)
    {
        return;
    }

    // The eligibility data the client needs to grey out content it cannot enter.
    //
    // FindRandomDungeonsNotForPlayer already computes exactly this: a map keyed by
    // LfgDungeonsEntry::Entry() -- which IS the wire's dungeonEntry field -- with an
    // LFGForbiddenTypes value, and those codes are the client's LFG_INSTANCE_INVALID_CODES
    // verbatim (2 LEVEL_TOO_LOW, 3 LEVEL_TOO_HIGH, 1025 MISSING_ITEM, 1031 NOT_IN_SEASON
    // and so on). So no translation is required in either direction.
    dungeonForbidden const locked = sLFGMgr.FindRandomDungeonsNotForPlayer(plr);

    std::vector<MopLfgPackets::PlayerLockInfo> locks;
    locks.reserve(locked.size());

    for (dungeonForbidden::const_iterator it = locked.begin(); it != locked.end(); ++it)
    {
        MopLfgPackets::PlayerLockInfo entry;
        entry.dungeonEntry = it->first;
        entry.lockStatus = it->second;
        // subReason1/2 stay zero. They carry the required and current item level for the
        // gear-score reasons; all 206 records of the reference capture have them zero.
        locks.push_back(entry);
    }

    // 5-byte header plus 16 bytes per lock. The reference reply was 6068 bytes for 206
    // locks and 35 random records; ours is locks-only, so 5 + 16 * n.
    WorldPacket data(SMSG_LFG_PLAYER_INFO, 5 + locks.size() * 16);
    MopLfgPackets::BuildPlayerInfo(data, locks);

    DEBUG_LOG("SMSG_LFG_PLAYER_INFO: %s, %u locked dungeon(s), %u bytes.",
              GetPlayerName(), uint32(locks.size()), uint32(data.size()));

    SendPacket(&data);
}

void WorldSession::SendLfgPartyLockInfo()
{
    // No compatible 18414 party lock model exists in this legacy manager.
    WorldPacket data(SMSG_LFG_PARTY_INFO, 3);
    MopLfgPackets::BuildEmptyPartyInfo(data);
    SendPacket(&data);
}

void WorldSession::HandleSetLfgCommentOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("CMSG_SET_LFG_COMMENT");

    std::string comment;
    recv_data >> comment;
    DEBUG_LOG("LFG comment \"%s\"", comment.c_str());
}

void WorldSession::SendLfgJoinResult(LfgJoinResult result, uint8 detail, partyForbidden const& lockedDungeons)
{
    MopLfgPackets::JoinResult update;
    update.result = uint8(result);
    update.detail = detail;

    // Retail zeroes the GUID and the whole ticket on a refusal -- that is what makes the
    // 18-byte form -- and carries both on a success. All 11 observed refusals are the
    // zeroed shape, so a refusal must not invent a ticket.
    if (result == ERR_LFG_OK)
    {
        ObjectGuid const playerGuid = GetPlayer()->GetObjectGuid();
        ObjectGuid queueGuid = playerGuid;

        LFGMgr::RetainedTicket retained;
        LFGMgr::RetainedTicket const* retainedIdentity = nullptr;
        if (sLFGMgr.GetRetainedTicket(playerGuid, retained))
        {
            queueGuid = ObjectGuid(retained.requesterGuid);
            retainedIdentity = &retained;
        }

        LFGStatusPacketData queueData;
        sLFGMgr.GetStatusPacketData(queueGuid, playerGuid, queueData);

        LFGStatePolicy::TicketIdentity const identity =
            LFGStatePolicy::ResolveTicketIdentity(
                retainedIdentity, playerGuid.GetRawValue(), queueData.ticketId,
                queueData.joinedTime ? queueData.joinedTime : uint32(time(NULL)));

        update.requesterGuid = identity.requesterGuid;
        update.joinTime = identity.time;
        update.clientQueueId = identity.id;
        update.ticketType = 3;
    }

    for (partyForbidden::const_iterator it = lockedDungeons.begin(); it != lockedDungeons.end(); ++it)
    {
        MopLfgPackets::JoinResultPlayer player;
        player.guid = it->first.GetRawValue();

        for (dungeonForbidden::const_iterator itr = it->second.begin(); itr != it->second.end(); ++itr)
        {
            MopLfgPackets::PlayerLockInfo lock;
            lock.dungeonEntry = itr->first;
            lock.lockStatus = itr->second;
            player.locks.push_back(lock);
        }

        update.players.push_back(player);
    }

    WorldPacket data(SMSG_LFG_JOIN_RESULT, 24);
    MopLfgPackets::BuildJoinResult(data, update);

    SendPacket(&data);
}

void WorldSession::SendLfgUpdate(bool fallbackIsGroup, LFGPlayerStatus status)
{
    bool joined = false;
    bool isQueued = false;

    switch (status.updateType)
    {
    case LFG_UPDATE_JOIN:
    case LFG_UPDATE_ADDED_TO_QUEUE:
        joined = true;
        isQueued = true;
        break;
    case LFG_UPDATE_PROPOSAL_BEGIN:
        joined = true;
        break;
    case LFG_UPDATE_JOIN_QUEUE_INITIAL:
        joined = true;
        break;
    case LFG_UPDATE_STATUS:
        isQueued = (status.state == LFG_STATE_QUEUED);
        // `joined` must go FALSE once the player is inside. It used to be
        // `state != LFG_STATE_NONE`, and LFG_STATE_IN_DUNGEON is non-zero, so we
        // reported joined=1 from inside the dungeon where retail sends 0
        // (capture-000720 seq 1286, byte 1 = 0x80). UIParent.lua:3902 GetLFGMode then
        // returns "suspended" instead of falling through to "lfgparty" -- the client
        // believes the player is still queued rather than in the run.
        joined = (status.state != LFG_STATE_NONE
                  && status.state != LFG_STATE_IN_DUNGEON
                  && status.state != LFG_STATE_FINISHED_DUNGEON);
        break;
    default:
        break;
    }

    ObjectGuid const playerGuid = GetPlayer()->GetObjectGuid();

    // The requester is part of the RideTicket key, so current group membership is
    // only a fallback for a path that has not established an authoritative ticket.
    ObjectGuid queueGuid = playerGuid;
    if (fallbackIsGroup && GetPlayer()->GetGroup())
        queueGuid = GetPlayer()->GetGroup()->GetObjectGuid();

    LFGMgr::RetainedTicket retained;
    if (sLFGMgr.GetRetainedTicket(playerGuid, retained))
        queueGuid = ObjectGuid(retained.requesterGuid);

    LFGStatusPacketData queueData;
    sLFGMgr.GetStatusPacketData(queueGuid, playerGuid, queueData);

    MopLfgPackets::StatusUpdate update;
    update.comment = status.comment;
    // Retail leaves these 0,0,0 in all 5291 observed bodies without exception; the
    // role shortage is advertised in SMSG_LFG_QUEUE_STATUS instead.
    update.needs = {{ 0, 0, 0 }};
    // Always 1. Across 5291 retail bodies byte 1 takes only 0x00, 0x80 and 0xC0 --
    // the 0x40 our solo queue used to emit (bit9 set, bit8 clear) occurs zero times,
    // and bit8 is set even for a solo queue with no group at all. The name "isParty"
    // does not explain that; the wire value is not in doubt.
    update.isParty = true;
    update.joined = joined;
    // notifyUi tracks joined -- equal in 5288 of 5291 bodies, and 0 for every terminal
    // reason (8, 9, 11, 15, 25). It was defaulted true and never assigned.
    update.notifyUi = joined;
    update.queued = isQueued;
    update.requestedRoles = queueData.roles;
    update.updateReason = uint8(status.updateType);
    // One ticket for the life of a queue, whatever happens to the entry underneath.
    //
    // The client keys its status records on the whole 20-byte RideTicket, so a body that
    // carries a different ticket does not update the record -- it creates a second one and
    // leaves the first stranded, queued and unclearable. GetStatusPacketData can legitimately
    // miss (the entry was erased by a merge, or the caller is announcing before it is
    // stored) and used to hand back a default-constructed struct, shipping ticketId = 0.
    // Retail sends 0 in none of 5291 observed bodies.
    //
    // Remember the first COMPLETE identity this player's bodies go out under, then
    // use that requester, id and time for every later body -- including ones whose
    // live lookup now resolves elsewhere after a merge or regroup.
    sLFGMgr.RetainTicket(playerGuid, queueGuid, queueData.ticketId, queueData.joinedTime);

    if (sLFGMgr.GetRetainedTicket(playerGuid, retained))
    {
        queueGuid = ObjectGuid(retained.requesterGuid);
        update.ticketId = retained.id;
        update.ticketTime = retained.time;
    }
    else
    {
        update.ticketId = queueData.ticketId;
        update.ticketTime = queueData.joinedTime;
        if (!update.ticketId)
        {
            sLog.outError("WORLD: SMSG_LFG_UPDATE_STATUS for %s has no ticket (reason %u); "
                          "the client cannot file this body against its queue record.",
                          GetPlayerName(), uint32(status.updateType));
        }
    }

    update.requesterGuid = queueGuid.GetRawValue();
    // Not "did the player leave" and not "is the player inside": this bit says the
    // retained requester is a group rather than this player. It must move with
    // requesterGuid because both describe the same client record.
    update.lfgJoined = (queueGuid != playerGuid);

    // NOTHING is forgotten here. The ticket belongs to the queue entry and is replaced by
    // LFGMgr::BeginTicket when the next entry is built -- see the note there for why
    // dropping it on a leave body left the client holding records nothing could address.

    if (!status.dungeonList.empty())
        update.dungeonCategory = sLFGMgr.GetDungeonCategory(*status.dungeonList.begin());

    for (std::set<uint32>::const_iterator it = status.dungeonList.begin(); it != status.dungeonList.end(); ++it)
        update.dungeonEntries.push_back(sLFGMgr.GetDungeonEntry(*it));

    WorldPacket data(SMSG_LFG_UPDATE_STATUS, 40 + status.comment.size() + update.dungeonEntries.size() * sizeof(uint32));
    if (MopLfgPackets::BuildUpdateStatus(data, update))
        SendPacket(&data);
    else
        sLog.outError("WORLD: LFG status fields exceed SMSG_LFG_UPDATE_STATUS wire limits");
}

void WorldSession::SendLfgQueueStatus(LFGQueueStatus const& status)
{
    MopLfgPackets::QueueStatusUpdate update;
    update.queueGuid = status.queueGuid;
    update.queuedTime = status.timeSpentInQueue;
    update.waitTimeAvg = status.avgWaitTime;
    update.waitTimeTank = status.tankAvgWaitTime;
    update.tanks = status.neededTanks;
    update.waitTimeHealer = status.healerAvgWaitTime;
    update.healers = status.neededHeals;
    update.waitTimeDps = status.dpsAvgWaitTime;
    update.dps = status.neededDps;
    update.joinTime = status.joinTime;
    // Retail's clientQueueId IS the status packet's ticketId -- capture-000044 carries
    // 0x9BFF in SMSG_LFG_JOIN_RESULT seq 1547, SMSG_LFG_QUEUE_STATUS seq 1577 and the
    // status bodies alike. One identifier, three packets.
    update.clientQueueId = status.ticketId;
    update.waitTime = status.playerAvgWaitTime;
    update.dungeonEntry = sLFGMgr.GetDungeonEntry(status.dungeonID);

    WorldPacket data(SMSG_LFG_QUEUE_STATUS, 52);
    MopLfgPackets::BuildQueueStatus(data, update);

    SendPacket(&data);
}

void WorldSession::SendLfgRoleCheckUpdate(LFGRoleCheck const& roleCheck)
{
    // Rebuilt for 18414. See MopLfgPackets::BuildRoleCheckUpdate for the layout and the
    // two captures it was verified against; the previous body was the 3.3.5 shape and
    // shared no field order with this client, which is why the role check prompt never
    // appeared however correct the server-side state was.
    MopLfgPackets::RoleCheckUpdate update;
    update.state = uint8(roleCheck.state);

    std::set<uint32> dungeons;
    if (roleCheck.randomDungeonID)
    {
        dungeons.insert(roleCheck.randomDungeonID);
    }
    else
    {
        dungeons = roleCheck.dungeonList;
    }

    for (std::set<uint32>::const_iterator it = dungeons.begin(); it != dungeons.end(); ++it)
    {
        update.dungeonEntries.push_back(sLFGMgr.GetDungeonEntry(*it));
    }

    // The leader MUST be first: the client renders entry 0 as the initiator, and both
    // captures show the leader's roles carrying the LEADER bit while later members are
    // still zero.
    ObjectGuid const leaderGuid = ObjectGuid(roleCheck.leaderGuidRaw);

    roleMap::const_iterator leaderItr = roleCheck.currentRoles.find(leaderGuid);
    if (leaderItr != roleCheck.currentRoles.end())
    {
        // Unchecked find() here previously: a role check whose leader had already left
        // dereferenced end().
        MopLfgPackets::RoleCheckMember member;
        member.guid = leaderGuid.GetRawValue();
        member.roles = leaderItr->second;

        Player* pLeader = sObjectAccessor.FindPlayer(leaderGuid);
        member.level = pLeader ? uint8(pLeader->getLevel()) : uint8(0);

        update.members.push_back(member);
    }

    for (roleMap::const_iterator rItr = roleCheck.currentRoles.begin();
         rItr != roleCheck.currentRoles.end(); ++rItr)
    {
        if (rItr->first == leaderGuid)
        {
            continue;
        }

        MopLfgPackets::RoleCheckMember member;
        member.guid = rItr->first.GetRawValue();
        member.roles = rItr->second;

        Player* pPlayer = sObjectAccessor.FindPlayer(rItr->first);
        member.level = pPlayer ? uint8(pPlayer->getLevel()) : uint8(0);

        update.members.push_back(member);
    }

    WorldPacket data(SMSG_LFG_ROLE_CHECK_UPDATE, 16 + update.members.size() * 16);
    MopLfgPackets::BuildRoleCheckUpdate(data, update);

    SendPacket(&data);
}

void WorldSession::SendLfgRoleChosen(uint64 rawGuid, uint8 roles)
{
    // Derived from the 18414 reader sub_6E921A (handler 0x985605, whose own log line
    // is "ROLE_CHOSEN - GUID: %016llX, Accepted: %s, Roles Desired: %x") and confirmed
    // by decoding eight corpus packets across seven captures, all of which reconstruct
    // byte for byte.
    //
    // The previous body -- uint64, uint8, uint32 -- was flat, and this opcode is not.
    // It never mattered because SMSG_ROLE_CHOSEN was not admitted through the send
    // gate, so the wrong bytes were discarded before reaching anyone.
    //
    // Nine mask bits, and the SIXTH is not a guid bit: it is `accepted`. Then three
    // guid bytes, the roles dword, then the remaining five guid bytes.
    ObjectGuid const guid(rawGuid);
    bool const accepted = roles > 0;

    WorldPacket data(SMSG_ROLE_CHOSEN, 2 + 8 + 4);

    data.WriteGuidMask<6, 2, 1, 7, 0>(guid);
    data.WriteBit(accepted);
    data.WriteGuidMask<3, 5, 4>(guid);
    data.FlushBits();

    data.WriteGuidBytes<0, 3, 6>(guid);
    data << uint32(roles);
    data.WriteGuidBytes<5, 1, 4, 2, 7>(guid);

    SendPacket(&data);
}

void WorldSession::SendLfgProposalUpdate(LFGProposal const& proposal)
{
    Player* pPlayer = GetPlayer();
    if (!pPlayer)
    {
        return;
    }

    ObjectGuid const plrGuid = pPlayer->GetObjectGuid();

    // find() without checking end() dereferenced a past-the-end iterator here. It is
    // reachable, not theoretical: SendDungeonProposal skips offline players when filling
    // `groups` and `answers` but still lists them in `currentRoles`, so a player who
    // queues, logs out and logs back in arrives with no entry of their own.
    playerGroupMap::const_iterator myGroup = proposal.groups.find(plrGuid);
    if (myGroup == proposal.groups.end())
    {
        return;
    }

    ObjectGuid const plrGroupGuid = myGroup->second;

    // Rebuilt for 18414. See MopLfgPackets::BuildProposalUpdate for the layout and the
    // two captures it was verified against.
    MopLfgPackets::ProposalUpdate update;
    update.dungeonEntry = sLFGMgr.GetDungeonEntry(proposal.dungeonID);
    update.proposalId = proposal.id;
    update.state = uint8(proposal.state);
    update.encounters = proposal.encounters;
    update.joinTime = uint32(proposal.joinedQueue);

    // "silent" suppresses opening a fresh window: the client updates one it already has.
    // Only correct when this is not a new proposal AND the recipient is already in the
    // group the proposal will reuse.
    update.silent = !proposal.isNew && plrGroupGuid &&
                    plrGroupGuid.GetRawValue() == proposal.groupRawGuid;

    // The recipient's own group if they have one, else themselves -- this identifies who
    // the update is about, not the proposed group.
    update.requesterGuid = plrGroupGuid ? plrGroupGuid.GetRawValue() : plrGuid.GetRawValue();

    for (playerGroupMap::const_iterator it = proposal.groups.begin();
         it != proposal.groups.end(); ++it)
    {
        ObjectGuid const memberGuid = it->first;

        roleMap::const_iterator roleItr = proposal.currentRoles.find(memberGuid);
        proposalAnswerMap::const_iterator answerItr = proposal.answers.find(memberGuid);
        if (roleItr == proposal.currentRoles.end() || answerItr == proposal.answers.end())
        {
            continue;
        }

        MopLfgPackets::ProposalPlayer entry;
        entry.roles = roleItr->second;
        entry.isSelf = (memberGuid == plrGuid);
        entry.answered = (answerItr->second != LFG_ANSWER_PENDING);
        entry.agreed = (answerItr->second == LFG_ANSWER_AGREE);
        entry.inProposedGroup = it->second && !proposal.isNew &&
                                it->second.GetRawValue() == proposal.groupRawGuid;
        entry.sameGroupAsSelf = it->second && it->second == plrGroupGuid;

        update.players.push_back(entry);
    }

    WorldPacket data(SMSG_LFG_PROPOSAL_UPDATE, 40 + update.players.size() * 5);
    MopLfgPackets::BuildProposalUpdate(data, update);

    SendPacket(&data);
}

void WorldSession::SendLfgOfferContinue(uint32 dungeonEntry)
{
    // "A player has left your group. Would you like to find another player to finish %s?"
    //
    // The whole body is ONE uint32: the packed dungeon entry, (TypeID << 24) | id, which is
    // exactly what LfgDungeonsEntry::Entry() returns. All 31 build-18414 bodies in the
    // corpus are 4 bytes and decode that way -- capture-000044 seq 278015 = 0x0100008C
    // (type 1, dungeon 140), capture-000059 seq 1946578 = 0x010001D4, capture-000133 seq
    // 560202 = 0x0100014A, capture-000187 seq 109049 = 0x01000020. Every one is type 1.
    //
    // The client raises LFG_OFFER_CONTINUE from this and names the dungeon in the popup.
    // Answering yes sends an ordinary CMSG_LFG_JOIN for that dungeon -- there is no
    // separate backfill opcode. capture-000326 shows the whole episode: offer, then the
    // normal join burst 43s later when the player accepted.
    //
    // We never sent this at all, so the prompt players saw was the client's own
    // LFGBackfillCover driven by party state rather than by us.
    WorldPacket data(SMSG_LFG_OFFER_CONTINUE, 4);
    data << uint32(dungeonEntry);
    SendPacket(&data);
}

void WorldSession::SendLfgTeleportError(uint8 error)
{
    DEBUG_LOG("SMSG_LFG_TELEPORT_DENIED: reason %u", uint32(error));

    // FOUR BITS, not a byte. The 18414 body is WriteBits(reason & 0xF, 4) followed by
    // FlushBits, which is why every captured body is exactly one byte.
    //
    // That also explains the value that previously blocked admission. The old comment
    // here reasoned that the captured 0x10 lay outside our enum and so our codes must
    // be wrong. The size was right and the reading was wrong: bits are MSB-first, so a
    // reason of 1 sits in the HIGH nibble and lands as 0x10. The corpus also carries
    // 0x90, which is reason 9. Both are ordinary codes.
    //
    // Only the low nibble is transmitted, so a value above 15 would silently truncate
    // into a different reason -- hence the enum is now constrained to 0-15 and this
    // masks defensively rather than trusting callers.
    WorldPacket data(SMSG_LFG_TELEPORT_DENIED, 1);
    data.WriteBits(error & 0xF, 4);
    data.FlushBits();
    SendPacket(&data);
}

void WorldSession::SendLfgRewards(LFGRewards const& rewards)
{
    DEBUG_LOG("SMSG_LFG_PLAYER_REWARD: money %u xp %u item %u x%u",
              rewards.moneyReward, rewards.expReward, rewards.itemID, rewards.itemAmount);

    // The inherited body shared no field order with 18414 and carried a uint8 flag the
    // client never reads. It never mattered, because this opcode was not admitted
    // through the send gate, so the wrong bytes were discarded before reaching anyone.
    //
    // Field MEANINGS are binary-derived, from the consumer at 0x989771 and the Lua
    // accessor sub_986CDD behind GetLFGCompletionRewardItem. Its own log lines name
    // them: "LFG_PLAYER_REWARD - Queued Slot: %u, Actual Slot: %u, Base Money: %d,
    // Base XP: %d" and "Receiving Item %u, Display %u, Quantity: %u".
    //
    // Field ORDER is corpus-derived, not reader-derived: 0x989771 is reached through a
    // runtime-computed pointer and has no xrefs to walk back from, so the deserialiser
    // could not be read. Thirteen corpus payloads decode with zero leftover under the
    // order below.
    //
    // Layout: money, queued slot, xp, actual slot, then a bit block of a 20-bit reward
    // count followed by one is-currency bit per reward, then 16 bytes per reward.
    //
    // ActualSlot is the concrete dungeon; the client masks it (& 0xFFFFF) to look up the
    // row it names and textures the alert frame from. QueuedSlot is what was queued for,
    // which for a random run is the category row, so the two legitimately differ.
    ItemPrototype const* proto = rewards.itemID ? ObjectMgr::GetItemPrototype(rewards.itemID) : NULL;
    bool const hasItem = proto != NULL && rewards.itemAmount != 0;
    uint32 const rewardCount = hasItem ? 1 : 0;

    WorldPacket data(SMSG_LFG_PLAYER_REWARD, 19 + 16 * rewardCount);
    data << uint32(rewards.moneyReward);
    data << uint32(rewards.randomDungeonEntry ? rewards.randomDungeonEntry : rewards.groupDungeonEntry);
    data << uint32(rewards.expReward);
    data << uint32(rewards.groupDungeonEntry);

    data.WriteBits(rewardCount, 20);
    for (uint32 i = 0; i < rewardCount; ++i)
    {
        // Item, not currency. The currency branch divides quantity by 100 for
        // high-precision currencies, so mislabelling one would misreport the amount.
        data.WriteBit(0);
    }
    data.FlushBits();

    if (hasItem)
    {
        data << uint32(rewards.itemID);
        data << uint32(0);                  // stored at struct+0xC and never read back
        data << uint32(proto->DisplayInfoID); // drives the reward frame's icon
        data << uint32(rewards.itemAmount);
    }

    SendPacket(&data);
}

void WorldSession::SendLfgBootUpdate(LFGBoot const& boot)
{
    DEBUG_LOG("SMSG_LFG_BOOT_PLAYER (5.4.8)");

    ObjectGuid plrGuid = GetPlayer()->GetObjectGuid();

    // The recipient is not guaranteed to have a vote recorded. The player being
    // voted on is deliberately skipped when the result is broadcast, and a member
    // who joined after the vote started never had an entry, so find() can and does
    // return end(). Dereferencing it was an unchecked crash on the boot path.
    proposalAnswerMap::const_iterator plrIt = boot.answers.find(plrGuid);
    LFGProposalAnswer plrAnswer = plrIt != boot.answers.end() ? plrIt->second : LFG_ANSWER_PENDING;

    uint32 voteCount = 0, yayCount = 0;
    for (proposalAnswerMap::const_iterator it = boot.answers.begin(); it != boot.answers.end(); ++it)
    {
        if (it->second != LFG_ANSWER_PENDING)
        {
            ++voteCount;
            if (it->second == LFG_ANSWER_AGREE)
            {
                ++yayCount;
            }
        }
    }

    time_t const expires = boot.startTime + LFG_TIME_BOOT;
    time_t const now = time(NULL);

    MopLfgPackets::BootUpdate update;
    update.victimGuid = boot.playerVotedOn.GetRawValue();
    update.reason = boot.reason;
    update.inProgress = boot.inProgress;
    update.didVote = plrAnswer != LFG_ANSWER_PENDING;
    update.votePassed = yayCount >= REQUIRED_VOTES_FOR_BOOT;
    update.agree = plrAnswer == LFG_ANSWER_AGREE;
    update.votesNeeded = REQUIRED_VOTES_FOR_BOOT;
    update.timeLeft = expires > now ? uint32(expires - now) : 0;
    update.agreeCount = yayCount;
    update.voteCount = voteCount;

    WorldPacket data(SMSG_LFG_BOOT_PLAYER, 30 + boot.reason.length());
    if (MopLfgPackets::BuildBootPlayer(data, update))
        SendPacket(&data);
    else
        sLog.outError("WORLD: LFG boot reason is too long for SMSG_LFG_BOOT_PLAYER");
}
