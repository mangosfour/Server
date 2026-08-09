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

#ifndef __MANGOS_LFGMGR_H
#define __MANGOS_LFGMGR_H

#include "Common.h"
#include "LFGStatePolicy.h"
#include "ObjectGuid.h"
#include "Policies/Singleton.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include <array>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

class Object;
class ObjectGuid;
class Player;
class Group;
class WorldPacket;

namespace MopLfgPackets
{
    struct LfrSearchRequest
    {
        uint32 lfgId = 0;
        uint8 typeId = 0;
    };

    struct BootUpdate
    {
        uint64 victimGuid = 0;
        std::string reason;
        bool inProgress = false;
        bool didVote = false;
        bool votePassed = false;
        bool agree = false;
        uint32 votesNeeded = 0;
        uint32 timeLeft = 0;
        uint32 agreeCount = 0;
        uint32 voteCount = 0;
    };

    struct StatusUpdate
    {
        uint64 requesterGuid = 0;
        std::vector<uint64> suspendedPlayerGuids;
        std::vector<uint32> dungeonEntries;
        std::string comment;
        std::array<uint8, 3> needs{};
        bool isParty = false;
        bool joined = false;
        bool notifyUi = true;
        bool lfgJoined = false;
        bool queued = false;
        uint8 updateReason = 0;
        uint32 requestedRoles = 0;
        uint32 ticketId = 0;
        uint32 ticketTime = 0;
        uint8 dungeonCategory = 0;
        uint32 ticketType = 3;
    };

    struct QueueStatusUpdate
    {
        uint64 queueGuid = 0;
        uint32 flags = 3;
        uint32 queuedTime = 0;
        int32 waitTimeAvg = 0;
        int32 waitTimeTank = 0;
        uint8 tanks = 0;
        int32 waitTimeHealer = 0;
        uint8 healers = 0;
        int32 waitTimeDps = 0;
        uint8 dps = 0;
        uint32 joinTime = 0;
        uint32 clientQueueId = 0;
        int32 waitTime = 0;
        uint32 dungeonEntry = 0;
    };

    /// One entry of a role check, in the order the client expects them: leader first.
    struct RoleCheckMember
    {
        uint64 guid = 0;
        uint32 roles = 0;
        uint8 level = 0;
    };

    struct RoleCheckUpdate
    {
        std::vector<RoleCheckMember> members;
        std::vector<uint32> dungeonEntries;
        uint8 partyIndex = 0;
        uint8 state = 0;
    };

    /// Wire value of LFG_ROLECHECK_INITIALITING.
    ///
    /// Spelled out here because LFGRoleCheckState is declared further down this header,
    /// after these inline builders. A static_assert next to the enum keeps the two from
    /// drifting apart.
    uint8 const ROLE_CHECK_STATE_INITIATING = 2;

    /// One participant of a dungeon proposal.
    struct ProposalPlayer
    {
        uint32 roles = 0;
        bool inProposedGroup = false;   // already in the group the proposal will reuse
        bool isSelf = false;            // is this the recipient
        bool answered = false;
        bool agreed = false;
        bool sameGroupAsSelf = false;
    };

    struct ProposalUpdate
    {
        std::vector<ProposalPlayer> players;
        uint64 requesterGuid = 0;       // recipient's original group, else the player
        uint64 instanceGuid = 0;        // see BuildProposalUpdate
        uint32 dungeonEntry = 0;
        uint32 clientQueueId = 0;
        uint32 proposalId = 0;
        uint32 joinTime = 0;
        uint32 encounters = 0;
        uint32 flags = 3;
        uint8 state = 0;
        bool silent = false;            // update an open window instead of opening one
    };

    bool BuildBootPlayer(WorldPacket& out, BootUpdate const& update);
    void BuildRoleCheckUpdate(WorldPacket& out, RoleCheckUpdate const& update);
    void BuildProposalUpdate(WorldPacket& out, ProposalUpdate const& update);
    bool BuildUpdateStatus(WorldPacket& out, StatusUpdate const& update);
    void BuildQueueStatus(WorldPacket& out, QueueStatusUpdate const& update);
    bool ParseLfrSearchRequest(WorldPacket& in, LfrSearchRequest& request);
    void BuildEmptyLfrSearchResponse(WorldPacket& out, LfrSearchRequest const& request);
    bool ParseLockInfoRequest(WorldPacket& in, bool& forPlayer);
    /// One entry of the lock array at the tail of SMSG_LFG_PLAYER_INFO.
    struct PlayerLockInfo
    {
        /// (TypeID << 24) | dungeonId -- the same value LfgDungeonsEntry::Entry() produces.
        uint32 dungeonEntry = 0;
        /// LFGForbiddenTypes, which are the client's LFG_INSTANCE_INVALID_CODES verbatim.
        uint32 lockStatus = 0;
        /// Only meaningful for the gear-score reasons, where the client formats them as
        /// "Requires: %2$d. Currently %3$d." Zero for every other reason, and zero in all
        /// 206 records of the reference capture.
        uint32 subReason1 = 0;
        uint32 subReason2 = 0;
    };

    void BuildEmptyPlayerInfo(WorldPacket& out);
    void BuildPlayerInfo(WorldPacket& out, std::vector<PlayerLockInfo> const& locks);
    void BuildEmptyPartyInfo(WorldPacket& out);

    /// One party member's lock list inside SMSG_LFG_JOIN_RESULT. Reuses PlayerLockInfo
    /// because the 16-byte lock record is the same one SMSG_LFG_PLAYER_INFO carries --
    /// only the field ORDER on the wire differs between the two packets.
    struct JoinResultPlayer
    {
        uint64 guid = 0;
        std::vector<PlayerLockInfo> locks;
    };

    struct JoinResult
    {
        std::vector<JoinResultPlayer> players;  // empty for every refusal observed
        uint64 requesterGuid = 0;               // zero on a refusal
        uint32 joinTime = 0;                    // queue ticket, shared with SMSG_LFG_QUEUE_STATUS
        uint32 clientQueueId = 0;
        uint32 ticketType = 0;                  // 3 on success, 0 on every observed refusal
        uint8 result = 0;                       // LfgJoinResult
        uint8 detail = 0;                       // LFGRoleCheckState; only read when result == 0x1C
    };

    void BuildJoinResult(WorldPacket& out, JoinResult const& update);
}

namespace MopLfgPacketDetail
{
    inline uint8 GuidByte(uint64 guid, size_t index)
    {
        return uint8(guid >> (index * 8));
    }

    inline void WriteGuidMask(WorldPacket& out, uint64 guid,
        std::initializer_list<size_t> order)
    {
        for (size_t index : order)
            out.WriteBit(GuidByte(guid, index) != 0);
    }

    inline void WriteGuidBytes(WorldPacket& out, uint64 guid,
        std::initializer_list<size_t> order)
    {
        for (size_t index : order)
            out.WriteByteSeq(GuidByte(guid, index));
    }
}

inline void MopLfgPackets::BuildRoleCheckUpdate(WorldPacket& out,
    RoleCheckUpdate const& update)
{
    // SMSG_LFG_ROLE_CHECK_UPDATE (0x12BB).
    //
    // The body that stood here was the 3.3.5 shape -- a uint32 state, flat counts and
    // raw uint64 GUIDs -- and shared no field order with 18414. Verified byte-exact
    // against two real captures of different shape, decoding to zero leftover bytes:
    //
    //   capture-000075 seq 891708, 35 B: partyIndex 0, state 2, 2 members, 1 dungeon
    //   capture-000059 seq 719547, 68 B: partyIndex 1, state 2, 5 members, 1 dungeon
    //
    // Corpus catalogueGenerationId 2BE10C89...88752.
    //
    // Note partyIndex is NOT always zero -- the second capture carries 1 -- so it is a
    // real field rather than padding, even though GetLFGRoleUpdate does not surface it
    // to Lua (the client stores it at dword_1209678 and reads it elsewhere).
    //
    // The "random dungeon" GUID is always empty in observed traffic; its mask bits are
    // written all-zero and WriteByteSeq emits nothing for a zero byte, so it costs 8
    // mask bits and no bytes. It is kept explicit because the bit positions are
    // interleaved with the dungeon count and cannot be collapsed away.
    uint64 const randomDungeonGuid = 0;

    out << uint8(update.partyIndex);
    out << uint8(update.state);

    out.WriteBits(uint32(update.members.size()), 21);

    for (std::vector<RoleCheckMember>::const_iterator it = update.members.begin();
         it != update.members.end(); ++it)
    {
        out.WriteBit(it->roles > 0);        // has this member answered yet
        MopLfgPacketDetail::WriteGuidMask(out, it->guid, { 3, 0, 5, 2, 7, 1, 4, 6 });
    }

    MopLfgPacketDetail::WriteGuidMask(out, randomDungeonGuid, { 3, 5 });
    out.WriteBits(uint32(update.dungeonEntries.size()), 22);
    MopLfgPacketDetail::WriteGuidMask(out, randomDungeonGuid, { 0, 7, 6, 1, 4, 2 });
    out.WriteBit(update.state == ROLE_CHECK_STATE_INITIATING);

    out.FlushBits();

    MopLfgPacketDetail::WriteGuidBytes(out, randomDungeonGuid, { 0 });

    for (std::vector<RoleCheckMember>::const_iterator it = update.members.begin();
         it != update.members.end(); ++it)
    {
        out << uint8(it->level);
        MopLfgPacketDetail::WriteGuidBytes(out, it->guid, { 3, 6 });
        out << uint32(it->roles);
        MopLfgPacketDetail::WriteGuidBytes(out, it->guid, { 2, 4, 0, 1, 5, 7 });
    }

    MopLfgPacketDetail::WriteGuidBytes(out, randomDungeonGuid, { 1, 7, 6, 4, 3, 2, 5 });

    for (std::vector<uint32>::const_iterator it = update.dungeonEntries.begin();
         it != update.dungeonEntries.end(); ++it)
    {
        out << uint32(*it);
    }
}

inline void MopLfgPackets::BuildProposalUpdate(WorldPacket& out,
    ProposalUpdate const& update)
{
    // SMSG_LFG_PROPOSAL_UPDATE (0x1E3B).
    //
    // The body that stood here was the 3.3.5 shape -- flat uint32/uint8 fields and a
    // per-player run of single bytes -- and shared no field order with 18414. Verified
    // byte-exact against two real captures chosen to differ as much as possible:
    //
    //   capture-000044 seq 1948,    64 B:  5 players, 1 tank / 1 healer / 3 dps
    //   capture-000059 seq 2063424, 156 B: 25 players, 2 tank / 6 healer / 17 dps
    //
    // Both decode to zero leftover. The second is a raid finder proposal, and its
    // composition matches the 2/6/17 that LfgDungeons.dbc carries for LFR rows -- an
    // independent check on the decode from a completely different evidence source.
    //
    // Corpus catalogueGenerationId 2BE10C89...88752.
    //
    // Two corrections to the reference layout this was checked against:
    //
    //  - It builds the second GUID as `dungeonEntry | (0x1F45 << 48)`. Real traffic
    //    carries neither: the top five bytes are constant 1F 44 00 00 11 in both
    //    captures while the low three vary, i.e. a genuine instance-side GUID with a
    //    counter, unrelated to the dungeon entry. We do not model that object, so we
    //    send zero -- a legal encoding, since all eight mask bits then read false and
    //    WriteByteSeq emits nothing. If a live client turns out to need it to match the
    //    proposal, synthesise it from proposalId rather than guessing a constant.
    //
    //  - Roles are passed through verbatim. Observed values include 0x32 and 0x09, so
    //    bits above DAMAGE are real and must not be masked off.
    out.WriteBit(MopLfgPacketDetail::GuidByte(update.instanceGuid, 6) != 0);
    out.WriteBit(MopLfgPacketDetail::GuidByte(update.instanceGuid, 0) != 0);
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 1, 7, 5 });
    out.WriteBit(MopLfgPacketDetail::GuidByte(update.instanceGuid, 5) != 0);
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 4 });
    out.WriteBit(update.silent);
    out.WriteBit(MopLfgPacketDetail::GuidByte(update.instanceGuid, 2) != 0);
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 6 });
    MopLfgPacketDetail::WriteGuidMask(out, update.instanceGuid, { 3, 7 });
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 3 });

    out.WriteBits(uint32(update.players.size()), 21);

    for (std::vector<ProposalPlayer>::const_iterator it = update.players.begin();
         it != update.players.end(); ++it)
    {
        out.WriteBit(it->inProposedGroup);
        out.WriteBit(it->isSelf);
        out.WriteBit(it->answered);
        out.WriteBit(it->agreed);
        out.WriteBit(it->sameGroupAsSelf);
    }

    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 2 });
    MopLfgPacketDetail::WriteGuidMask(out, update.instanceGuid, { 4 });
    out.WriteBit(false);                                    // unknown; zero in all observed traffic
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 0 });
    MopLfgPacketDetail::WriteGuidMask(out, update.instanceGuid, { 1 });

    out.FlushBits();

    MopLfgPacketDetail::WriteGuidBytes(out, update.instanceGuid, { 1 });
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 4 });
    MopLfgPacketDetail::WriteGuidBytes(out, update.instanceGuid, { 4 });
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 7, 2, 0 });

    out << uint32(update.dungeonEntry);
    out << uint8(update.state);
    out << uint32(update.clientQueueId);

    MopLfgPacketDetail::WriteGuidBytes(out, update.instanceGuid, { 6 });
    out << uint32(update.proposalId);
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 5, 3 });
    out << uint32(update.joinTime);
    MopLfgPacketDetail::WriteGuidBytes(out, update.instanceGuid, { 5 });
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 6 });

    for (std::vector<ProposalPlayer>::const_iterator it = update.players.begin();
         it != update.players.end(); ++it)
    {
        out << uint32(it->roles);
    }

    out << uint32(update.encounters);

    MopLfgPacketDetail::WriteGuidBytes(out, update.instanceGuid, { 7 });
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 1 });
    MopLfgPacketDetail::WriteGuidBytes(out, update.instanceGuid, { 0, 2 });

    out << uint32(update.flags);

    MopLfgPacketDetail::WriteGuidBytes(out, update.instanceGuid, { 3 });
}

inline bool MopLfgPackets::ParseLfrSearchRequest(WorldPacket& in,
    LfrSearchRequest& request)
{
    if (in.size() - in.rpos() != 4)
        return false;

    uint8 const* body = in.contents() + in.rpos();
    uint32 const key = uint32(body[0]) |
        (uint32(body[1]) << 8) |
        (uint32(body[2]) << 16) |
        (uint32(body[3]) << 24);
    uint8 const typeId = uint8(key >> 24);
    if ((key & 0x00F00000u) != 0 || typeId >= 7)
        return false;

    request.lfgId = key & 0x000FFFFFu;
    request.typeId = typeId;
    in.read_skip<uint32>();
    return in.rpos() == in.size();
}

inline void MopLfgPackets::BuildEmptyLfrSearchResponse(WorldPacket& out,
    LfrSearchRequest const& request)
{
    // Direct 18414 reader layout. The top-level GUID mask is split around the
    // two result collections; every field below is zero and flushes to 9 bytes.
    out.WriteBits(0, 24); // removed result count
    out.WriteBit(false);  // top GUID[6]
    out.WriteBit(false);  // top GUID[2]
    out.WriteBit(false);  // top GUID[0]
    out.WriteBit(false);  // replacement mode: false clears current result caches
    out.WriteBits(0, 17); // player count
    out.WriteBit(false);  // top GUID[4]
    out.WriteBit(false);  // top GUID[1]
    out.WriteBits(0, 20); // group count
    out.WriteBit(false);  // top GUID[5]
    out.WriteBit(false);  // top GUID[7]
    out.WriteBit(false);  // top GUID[3]
    out.FlushBits();

    out << uint32(0);
    out << uint32(0);
    out << request.lfgId;
    out << uint32(request.typeId);
    out << uint32(0);
    out << uint32(0);
    out << uint32(0);
}

inline bool MopLfgPackets::BuildBootPlayer(WorldPacket& out,
    BootUpdate const& update)
{
    if (update.reason.size() >= (size_t(1) << 8))
        return false;

    out.WriteBit(update.reason.empty());
    MopLfgPacketDetail::WriteGuidMask(out, update.victimGuid, { 3 });
    out.WriteBit(update.didVote);
    out.WriteBit(update.votePassed);
    out.WriteBit(update.agree);
    MopLfgPacketDetail::WriteGuidMask(out, update.victimGuid, { 6 });
    if (!update.reason.empty())
        out.WriteBits(update.reason.size(), 8);
    out.WriteBit(update.inProgress);
    MopLfgPacketDetail::WriteGuidMask(out, update.victimGuid, { 1, 7, 5, 2, 0, 4 });
    out.FlushBits();

    MopLfgPacketDetail::WriteGuidBytes(out, update.victimGuid, { 2, 4, 3, 6 });
    out << update.votesNeeded;
    out << update.timeLeft;
    if (!update.reason.empty())
        out.append(update.reason.data(), update.reason.size());
    MopLfgPacketDetail::WriteGuidBytes(out, update.victimGuid, { 5, 0 });
    out << update.agreeCount;
    MopLfgPacketDetail::WriteGuidBytes(out, update.victimGuid, { 7 });
    out << update.voteCount;
    MopLfgPacketDetail::WriteGuidBytes(out, update.victimGuid, { 1 });
    return true;
}

inline bool MopLfgPackets::BuildUpdateStatus(WorldPacket& out,
    StatusUpdate const& update)
{
    if (update.comment.size() >= (size_t(1) << 8) ||
        update.dungeonEntries.size() >= (size_t(1) << 22) ||
        update.suspendedPlayerGuids.size() >= (size_t(1) << 24))
    {
        return false;
    }

    out.WriteBits(update.comment.size(), 8);
    out.WriteBit(update.isParty);
    out.WriteBit(update.joined);
    out.WriteBits(update.dungeonEntries.size(), 22);
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 2, 3, 1 });
    out.WriteBit(update.notifyUi);
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 7, 6, 0 });
    out.WriteBit(update.lfgJoined);
    out.WriteBit(update.queued);
    out.WriteBits(update.suspendedPlayerGuids.size(), 24);
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 5 });
    for (uint64 guid : update.suspendedPlayerGuids)
        MopLfgPacketDetail::WriteGuidMask(out, guid, { 7, 0, 4, 2, 5, 3, 1, 6 });
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 4 });
    out.FlushBits();

    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 3 });
    for (uint8 need : update.needs)
        out << need;
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 4 });
    for (uint64 guid : update.suspendedPlayerGuids)
        MopLfgPacketDetail::WriteGuidBytes(out, guid, { 7, 0, 1, 6, 4, 5, 2, 3 });
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 6 });
    out << update.updateReason;
    out << update.requestedRoles;
    out << update.ticketId;
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 5 });
    if (!update.comment.empty())
        out.append(update.comment.data(), update.comment.size());
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 2 });
    for (uint32 entry : update.dungeonEntries)
        out << entry;
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 0, 1 });
    out << update.ticketTime;
    out << update.dungeonCategory;
    out << update.ticketType;
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 7 });
    return true;
}

inline void MopLfgPackets::BuildQueueStatus(WorldPacket& out,
    QueueStatusUpdate const& update)
{
    // Direct inverse of the 18414 queue-status reader reached by selector 34.
    // The queue GUID is the player GUID for solo queues and group GUID otherwise.
    MopLfgPacketDetail::WriteGuidMask(out, update.queueGuid, { 4, 3, 5, 1, 2, 0, 6, 7 });
    out.FlushBits();

    out << update.flags;
    MopLfgPacketDetail::WriteGuidBytes(out, update.queueGuid, { 0 });
    out << update.queuedTime;
    MopLfgPacketDetail::WriteGuidBytes(out, update.queueGuid, { 4 });
    out << update.waitTimeAvg;
    out << update.waitTimeTank;
    out << update.tanks;
    out << update.waitTimeHealer;
    out << update.healers;
    out << update.waitTimeDps;
    out << update.dps;
    out << update.joinTime;
    out << update.clientQueueId;
    MopLfgPacketDetail::WriteGuidBytes(out, update.queueGuid, { 1 });
    out << update.waitTime;
    MopLfgPacketDetail::WriteGuidBytes(out, update.queueGuid, { 7, 2 });
    out << update.dungeonEntry;
    MopLfgPacketDetail::WriteGuidBytes(out, update.queueGuid, { 5, 3, 6 });
}

inline void MopLfgPackets::BuildJoinResult(WorldPacket& out,
    JoinResult const& update)
{
    // SMSG_LFG_JOIN_RESULT (0x18E3).
    //
    // Direct inverse of the 18414 reader sub_760C65, reached from dispatcher case 687.
    // The body that stood here was the 3.3.5 shape -- uint32 result, uint32 state, then
    // raw uint64 GUIDs -- which shares no field WIDTH with this client, let alone field
    // order. That, plus the opcode never having been admitted, is why a refused join was
    // silent in both directions.
    //
    // Verified byte-exact against all three observed sizes, decoding to zero leftover
    // bytes and zero non-zero pad bits (catalogueGenerationId 2BE10C89...88752):
    //
    //   capture-000059 seq 490545, 18 B: refusal, result 0x1C detail 6, guid 0, ticket 0
    //   capture-000044 seq 1547,   23 B: success, guid 0x0400000006296291, type 3
    //   capture-000075 seq 891753, 24 B: success, guid 0x1F5400001249B4F0, type 3
    //
    // The governing identity when no locks are present is
    //   len == 18 + popcount(byte0) + popcount(byte3)
    // because the GUID mask is SPLIT either side of the 22-bit lock count.
    //
    // capture-000044 cross-checks against SMSG_LFG_QUEUE_STATUS seq 1577 in the same
    // capture: joinTime 0x54146107 and queueId 0x9BFF are identical in both, so the
    // ticket really is one shared identifier rather than a per-packet value.
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 7, 6, 3, 0 });
    out.WriteBits(update.players.size(), 22);
    for (JoinResultPlayer const& player : update.players)
    {
        MopLfgPacketDetail::WriteGuidMask(out, player.guid, { 3 });
        out.WriteBits(player.locks.size(), 20);
        MopLfgPacketDetail::WriteGuidMask(out, player.guid, { 6, 1, 4, 7, 2, 0, 5 });
    }
    MopLfgPacketDetail::WriteGuidMask(out, update.requesterGuid, { 5, 1, 4, 2 });
    out.FlushBits();

    out << update.result;
    for (JoinResultPlayer const& player : update.players)
    {
        MopLfgPacketDetail::WriteGuidBytes(out, player.guid, { 4 });
        for (PlayerLockInfo const& lock : player.locks)
        {
            // Reverse of the SMSG_LFG_PLAYER_INFO order: the dungeon entry is written
            // LAST here, after both sub-reasons and the lock status.
            out << lock.subReason2;
            out << lock.subReason1;
            out << lock.lockStatus;
            out << lock.dungeonEntry;
        }
        MopLfgPacketDetail::WriteGuidBytes(out, player.guid, { 1, 0, 5, 7, 3, 6, 2 });
    }
    out << update.detail;
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 2 });
    out << update.joinTime;
    out << update.clientQueueId;
    out << update.ticketType;
    MopLfgPacketDetail::WriteGuidBytes(out, update.requesterGuid, { 6, 4, 1, 0, 5, 7, 3 });
}

inline bool MopLfgPackets::ParseLockInfoRequest(WorldPacket& in,
    bool& forPlayer)
{
    if (in.size() - in.rpos() != 2)
        return false;

    size_t const position = in.rpos();
    uint8 const* body = in.contents() + position;
    if (body[0] != 0x7F || (body[1] & 0x7F) != 0)
        return false;

    in.read_skip<uint8>();
    forPlayer = in.ReadBit();
    return in.rpos() == in.size();
}

inline void MopLfgPackets::BuildPlayerInfo(WorldPacket& out,
    std::vector<PlayerLockInfo> const& locks)
{
    // SMSG_LFG_PLAYER_INFO with a populated lock list.
    //
    // Sent ONLY in reply to CMSG_LFG_LOCK_INFO_REQUEST -- it is not pushed at login. In
    // capture-000006 the two pair seven-for-seven, and the client asks at world-enter
    // (CMSG_LFG_GET_STATUS then CMSG_LFG_LOCK_INFO_REQUEST at adjacent sequence numbers).
    //
    // Layout verified byte-exact against capture-000006 seq 1953, a 6068-byte reply to a
    // max-level character:
    //
    //   bits   WriteBits(lockCount, 20)      -> 206
    //          WriteBit(hasPlayerGuid)       -> 0
    //          WriteBits(randomDungeonCount, 17) -> 35
    //          FlushBits                     -> 38 bits, 5 bytes
    //   ...random dungeon reward records, variable length...
    //   tail   lockCount x 16 bytes, flat and unpacked:
    //              uint32 dungeonEntry   (TypeID << 24) | id
    //              uint32 lockStatus
    //              uint32 subReason1
    //              uint32 subReason2
    //
    // The locks sit at the TAIL, after the random records. With zero randoms the two are
    // adjacent, which is what makes a locks-only reply coherent: the client installs the
    // lock list and raises LFG_LOCK_INFO_RECEIVED whether or not any random rows follow,
    // so none of the reward plumbing is needed to make the eligibility filter work.
    //
    // Why this matters: LFGList_DefaultFilterFunction shows a dungeon when
    // `not LFGLockList[dungeonID]`, and LFGLockList is built from this array. Sending it
    // empty told the client nothing is locked, so every dungeon in the game appeared in
    // the finder and players could queue for content they cannot enter.
    out.WriteBits(uint32(locks.size()), 20);
    out.WriteBit(false);                // has player GUID -- 0 in the reference capture
    out.WriteBits(0, 17);               // random dungeon count; see above
    out.FlushBits();

    for (std::vector<PlayerLockInfo>::const_iterator it = locks.begin(); it != locks.end(); ++it)
    {
        out << uint32(it->dungeonEntry);
        out << uint32(it->lockStatus);
        out << uint32(it->subReason1);
        out << uint32(it->subReason2);
    }
}

inline void MopLfgPackets::BuildEmptyPlayerInfo(WorldPacket& out)
{
    out.WriteBits(0, 20); // locked dungeon count
    out.WriteBit(false);  // has player GUID
    out.WriteBits(0, 17); // random/seasonal dungeon count
    out.FlushBits();
}

inline void MopLfgPackets::BuildEmptyPartyInfo(WorldPacket& out)
{
    out.WriteBits(0, 22); // party member count
    out.FlushBits();
}


struct LFGBoot;
class Map;
struct LFGGroupStatus;
struct LFGPlayers;
struct LFGPlayerStatus;
struct LFGProposal;
struct LFGRoleCheck;
struct LFGWait;

// Begin Section: Enumerations

enum LFGFlags
{
    LFG_FLAG_UNK1        = 0x1,
    LFG_FLAG_UNK2        = 0x2,
    LFG_FLAG_SEASONAL    = 0x4,
    LFG_FLAG_UNK3        = 0x8
};

/// Possible statuses to send after a request to join the dungeon finder
/// Result codes for SMSG_LFG_JOIN_RESULT, build 18414.
///
/// Re-valued from the 3.3.5 numbering this was inherited with. The client picks the
/// displayed string by LINEAR SCAN of a 19-entry {u32 code, u32 stringId} table at
/// .data:00F66A30, bounded by `cmp ecx, 13h` at .text:0098E80C. A code that is not in
/// that table takes the `jmp short loc_98E820` at .text:0098E811, which skips the
/// DisplayError call outright -- the player is shown NOTHING. Every value below was
/// resolved through the descriptor array at .data:00F5C278 (stride 0x14, name pointer
/// at +0x00), so these are table reads, not an ordering guess.
///
/// The shift is NOT a constant: +0x1B for the old 0x01..0x05, then +0x1A from
/// MISMATCHED_SLOTS on, because MoP dropped NO_SLOTS_PARTY. A blanket offset would
/// silently mis-value two thirds of the enum.
enum LfgJoinResult
{
    ERR_LFG_OK                                  = 0x00, // success; not in the table, client shows nothing
    ERR_LFG_ROLE_CHECK_FAILED                   = 0x1C, // detail byte refines this one -- see LfgJoinResultDetail
    ERR_LFG_GROUP_FULL                          = 0x1D,
    ERR_LFG_NO_LFG_OBJECT                       = 0x1F,
    ERR_LFG_NO_SLOTS_PLAYER                     = 0x20, // the only code that also carries the per-player lock array
    ERR_LFG_MISMATCHED_SLOTS                    = 0x21,
    ERR_LFG_PARTY_PLAYERS_FROM_DIFFERENT_REALMS = 0x22,
    ERR_LFG_MEMBERS_NOT_PRESENT                 = 0x23,
    ERR_LFG_GET_INFO_TIMEOUT                    = 0x24,
    ERR_LFG_INVALID_SLOT                        = 0x25,
    ERR_LFG_DESERTER_PLAYER                     = 0x26,
    ERR_LFG_DESERTER_PARTY                      = 0x27,
    ERR_LFG_RANDOM_COOLDOWN_PLAYER              = 0x28,
    ERR_LFG_RANDOM_COOLDOWN_PARTY               = 0x29,
    ERR_LFG_TOO_MANY_MEMBERS                    = 0x2A,
    ERR_LFG_CANT_USE_DUNGEONS                   = 0x2B,
    ERR_LFG_ROLE_CHECK_FAILED2                  = 0x2C, // genuine second code; renders the same string as 0x1C
    ERR_LFG_TOO_FEW_MEMBERS                     = 0x32, // MoP-new
    ERR_LFG_REASON_TOO_MANY_LFG                 = 0x33, // MoP-new
    ERR_LFG_MISMATCHED_SLOTS_LOCAL_XREALM       = 0x35, // MoP-new

    // ERR_LFG_NO_SLOTS_PARTY is deliberately absent. Its string still exists in the
    // client (index 0x2EE) but NO result code maps to it, so there is no way to send
    // it. Callers must use ERR_LFG_NO_SLOTS_PLAYER for a party too -- it is the code
    // that carries the lock array, so the player is told which dungeons were locked
    // instead of being shown nothing.
};

/// Second body byte, only consulted when the result is ERR_LFG_ROLE_CHECK_FAILED
/// (.text:0098E7DE `cmp dl, 1Ch`). Any other value falls through to the plain string.
enum LfgJoinResultDetail
{
    LFG_JOIN_DETAIL_NONE       = 0,
    LFG_JOIN_DETAIL_TIMEOUT    = 3, // -> ERR_LFG_ROLE_CHECK_FAILED_TIMEOUT    (string 0x2E9)
    LFG_JOIN_DETAIL_NOT_VIABLE = 4, // -> ERR_LFG_ROLE_CHECK_FAILED_NOT_VIABLE (string 0x2EA)
};

enum LfgUpdateType
{
    LFG_UPDATE_DEFAULT              = 0,
    LFG_UPDATE_LEADER_LEAVE         = 1,
    LFG_UPDATE_ROLECHECK_ABORTED    = 4,
    LFG_UPDATE_JOIN                 = 6,
    LFG_UPDATE_ROLECHECK_FAILED     = 7,
    LFG_UPDATE_LEAVE                = 8,
    LFG_UPDATE_PROPOSAL_FAILED      = 9,
    LFG_UPDATE_PROPOSAL_DECLINED    = 10,
    LFG_UPDATE_GROUP_FOUND          = 11,
    LFG_UPDATE_ADDED_TO_QUEUE       = 13,
    LFG_UPDATE_PROPOSAL_BEGIN       = 14,
    LFG_UPDATE_STATUS               = 15,
    LFG_UPDATE_GROUP_MEMBER_OFFLINE = 16,
    LFG_UPDATE_GROUP_DISBAND        = 17,

    /// Retail's opening reason for a fresh queue: 257 of 276 observed joins lead with
    /// 24 and NONE lead with 6. LFG_UPDATE_JOIN (6) is the re-queue-from-inside-a-
    /// dungeon reason, which is why it was the wrong thing to open with.
    LFG_UPDATE_JOIN_QUEUE_INITIAL   = 24,
    /// Sent after SMSG_LFG_PLAYER_REWARD when the run completes. All 283 observed
    /// reason-25 bodies carry the same flag tuple.
    LFG_UPDATE_DUNGEON_FINISHED     = 25,
};

enum LfgType
{
    LFG_TYPE_NONE                 = 0,
    LFG_TYPE_DUNGEON              = 1,
    LFG_TYPE_RAID                 = 2,
    LFG_TYPE_QUEST                = 3,
    LFG_TYPE_ZONE                 = 4,
    LFG_TYPE_HEROIC_DUNGEON       = 5,
    LFG_TYPE_RANDOM_DUNGEON       = 6
};

/// Reasons a player cannot enter a dungeon
enum LFGForbiddenTypes
{
    LFG_FORBIDDEN_EXPANSION             = 1,
    LFG_FORBIDDEN_LOW_LEVEL             = 2,
    LFG_FORBIDDEN_HIGH_LEVEL            = 3,
    LFG_FORBIDDEN_LOW_GEAR_SCORE        = 4,
    LFG_FORBIDDEN_HIGH_GEAR_SCORE       = 5,
    LFG_FORBIDDEN_RAID                  = 6,
    LFG_FORBIDDEN_ATTUNEMENT_LOW_LEVEL  = 1001,
    LFG_FORBIDDEN_ATTUNEMENT_HIGH_LEVEL = 1002,
    LFG_FORBIDDEN_QUEST_INCOMPLETE      = 1022,
    LFG_FORBIDDEN_MISSING_ITEM          = 1025,
    LFG_FORBIDDEN_NOT_IN_SEASON         = 1031,
    LFG_FORBIDDEN_MISSING_ACHIEVEMENT   = 1034
};

/// Spells that affect the mechanisms of the dungeon finder
enum LFGSpells
{
    LFG_DESERTER_SPELL = 71041,
    LFG_COOLDOWN_SPELL = 71328,
};

enum LFGTimes
{
    // SECONDS, not milliseconds: waitForRoleTime is built from time(NULL),
    // so 45*IN_MILLISECONDS made a role check expire after 12.5 HOURS.
    LFG_TIME_ROLECHECK                           = 45,
    LFG_TIME_BOOT                                = 30,   // retail: 30 s in all 14 observed boot sessions
    LFG_TIME_PROPOSAL                            = 45,
};

/// Proposal answers
enum LFGProposalAnswer
{
    LFG_ANSWER_PENDING                           = -1,
    LFG_ANSWER_DENY                              = 0,
    LFG_ANSWER_AGREE                             = 1
};

/// Player states in the lfg system
enum LFGState
{
    LFG_STATE_NONE,
    LFG_STATE_ROLECHECK,
    LFG_STATE_QUEUED,
    LFG_STATE_PROPOSAL,
    LFG_STATE_BOOT,
    LFG_STATE_IN_DUNGEON,
    LFG_STATE_FINISHED_DUNGEON,
    LFG_STATE_RAIDBROWSER
};

/// Proposal states
enum LFGProposalState
{
    LFG_PROPOSAL_INITIATING                      = 0,
    LFG_PROPOSAL_FAILED                          = 1,
    LFG_PROPOSAL_SUCCESS                         = 2
};

/// Role check states
enum LFGRoleCheckState
{
    LFG_ROLECHECK_DEFAULT                        = 0,      // Internal use = Not initialized.
    LFG_ROLECHECK_FINISHED                       = 1,      // Role check finished
    LFG_ROLECHECK_INITIALITING                   = 2,      // Role check begins
    LFG_ROLECHECK_MISSING_ROLE                   = 3,      // Someone hasn't selected a role after 2 mins
    LFG_ROLECHECK_WRONG_ROLES                    = 4,      // Can't form a group with the role selection
    LFG_ROLECHECK_ABORTED                        = 5,      // Someone left the group
    LFG_ROLECHECK_NO_ROLE                        = 6       // Someone didn't select a role
};

static_assert(uint8(LFG_ROLECHECK_INITIALITING) == MopLfgPackets::ROLE_CHECK_STATE_INITIATING,
    "SMSG_LFG_ROLE_CHECK_UPDATE writes a bit for state == INITIALITING; the value it "
    "compares against must track the enum. Both captures the writer is tested on carry "
    "state 2 with that bit set.");

/// Role types
enum LFGRoles
{
    PLAYER_ROLE_NONE                             = 0x00,
    PLAYER_ROLE_LEADER                           = 0x01,
    PLAYER_ROLE_TANK                             = 0x02,
    PLAYER_ROLE_HEALER                           = 0x04,
    PLAYER_ROLE_DAMAGE                           = 0x08
};

/// Dungeon finder debug modes, driven by `.debug dungeon`.
///
/// Every relaxation these enable is gated on the queue entry actually containing a game
/// master. Relaxing the matchmaker globally would change how ordinary players match each
/// other while the operator is testing, which is exactly what makes a debug switch
/// untrustworthy.
enum LFGDebugMode
{
    LFG_DEBUG_OFF                                = 0,      // normal matchmaking
    LFG_DEBUG_SOLO                               = 1,      // a GM's entry completes alone
    LFG_DEBUG_GROUP                              = 2       // a GM's entry also absorbs whoever else is waiting
};

/// Role amounts
enum LFGRoleCount
{
    NORMAL_TANK_OR_HEALER_COUNT                  = 1,      // Tanks / Heals
    NORMAL_DAMAGE_COUNT                          = 3,      // DPS
    NORMAL_TOTAL_ROLE_COUNT                      = 5       // Amount of players total per normal dungeon
};

/// Teleport errors
/// Reason codes for SMSG_LFG_TELEPORT_DENIED.
///
/// Derived from the 18414 client, not from a fork. The body is a FOUR BIT field
/// (WriteBits(reason & 0xF, 4) then FlushBits), which is why every captured body
/// is one byte and why the single observed value looked unmappable: MSB-first, a
/// reason of 1 occupies the high nibble and lands as 0x10, and the corpus also
/// carries 0x90 for reason 9. Both are ordinary codes, not an unknown space.
///
/// Only the low nibble reaches the wire, so every value here must be 0-15.
enum LFGTeleportError
{
    LFG_TELEPORTERROR_OK                         = 0,
    LFG_TELEPORTERROR_FALLING                    = 7,
    LFG_TELEPORTERROR_PLAYER_DEAD                = 9,
    LFG_TELEPORTERROR_FATIGUE                    = 12,
    LFG_TELEPORTERROR_INVALID_LOCATION           = 15,

    /// The three refusals with no dedicated client message.
    ///
    /// 5 and 10 both route to ERR_CLIENT_LOCKED_OUT ("You can't do that right
    /// now"), which is vague but true and, crucially, VISIBLE. 6 and 13 are
    /// silent in the client, so sending either would put the player back exactly
    /// where they were before this opcode was admitted: a click that does
    /// nothing and explains nothing.
    ///
    /// IN_COMBAT deliberately shares that generic code rather than using 30. The
    /// client does own ERR_PARTY_LFG_TELEPORT_IN_COMBAT, but 30 was recovered
    /// from the PARTY error dispatcher, and this opcode's four-bit field cannot
    /// carry 30 at all -- it would truncate to 14. A vague visible message beats
    /// a wrong one.
    LFG_TELEPORTERROR_IN_VEHICLE                 = 5,
    LFG_TELEPORTERROR_CHARMING                   = 5,
    LFG_TELEPORTERROR_IN_COMBAT                  = 5
};

enum DungeonTypes
{
    DUNGEON_CLASSIC      = 0,
    DUNGEON_TBC          = 1,
    DUNGEON_TBC_HEROIC   = 2,
    DUNGEON_WOTLK        = 3,
    DUNGEON_WOTLK_HEROIC = 4,
    DUNGEON_UNKNOWN
};

// End Section: Enumerations

// Begin Section: Constants & Definitions

/// Heroic dungeon rewards in WoTLK after already doing a dungeon
const uint32 WOTLK_SPECIAL_HEROIC_ITEM = 47241;
const uint32 WOTLK_SPECIAL_HEROIC_AMNT = 2;

/// Default average queue time (in case we don't have data to base calculations on)
const int32 QUEUE_DEFAULT_TIME = 15*MINUTE;                              // 15 minutes [system is measured in seconds]

/// Amount of votes needed to kick a player out of a group
const int32 REQUIRED_VOTES_FOR_BOOT = 3;

typedef std::set<uint32> dailyEntries;                                   // for players who did one of X type instance per day
typedef std::set<ObjectGuid> queueSet;                                   // List of players / groups in the queue
typedef std::set<ObjectGuid> groupSet;                                   // List of groups doing a dungeon via the finder

typedef std::unordered_map<uint32, uint32> dungeonEntries;                    // ID, Entry
typedef std::unordered_map<uint32, uint32> dungeonForbidden;                  // Entry, LFGForbiddenTypes
typedef std::unordered_map<uint32, LFGProposal> proposalMap;                  // Proposal ID, info on a proposal
typedef std::unordered_map<uint32, LFGWait> waitTimeMap;                      // DungeonID, wait info
typedef std::unordered_map<ObjectGuid, dungeonForbidden> partyForbidden;      // ObjectGuid of player, map of locked dungeons
typedef std::unordered_map<ObjectGuid, uint8> roleMap;                        // ObjectGuid of player, role(s) selected
typedef std::unordered_map<ObjectGuid, LFGRoleCheck> roleCheckMap;            // ObjectGuid of group, role information
typedef std::unordered_map<ObjectGuid, LFGPlayerStatus> playerStatusMap;      // ObjectGuid of player, info on specific players only
typedef std::unordered_map<ObjectGuid, LFGPlayers> playerData;                // ObjectGuid of plr/group, info on specific player or group. TODO: rename to queueData
typedef std::unordered_map<ObjectGuid, LFGProposalAnswer> proposalAnswerMap;  // ObjectGuid of player, answer to proposal
typedef std::unordered_map<ObjectGuid, ObjectGuid> playerGroupMap;            // ObjectGuid of player, ObjectGuid of group
typedef std::unordered_map<ObjectGuid, LFGGroupStatus> groupStatusMap;        // ObjectGuid of group, group status structure
typedef std::unordered_map<ObjectGuid, LFGBoot> bootStatusMap;                // ObjectGuid of group, boot vote status

// End Section: Constants & Definitions

// Begin Section: Structures

/// Item rewards taken from DungeonFinderItems in ObjectMgr, parsed by dbc values
struct ItemRewards
{
    uint32 itemId;
    uint32 itemAmount;

    ItemRewards() : itemId(0), itemAmount(0) {}
    ItemRewards(uint32 ItemId, uint32 ItemAmount) : itemId(ItemId), itemAmount(ItemAmount) {}
};

/// Information the dungeon finder needs about each player (or group)
struct LFGPlayers //TODO: rename to LFGQueueData
{
    LFGState currentState;                  // where the player is at with the dungeon finder
    std::set<uint32> dungeonList;           // The dungeons this player or group are queued for (ID, not entry)
    roleMap currentRoles;                   // tank, dps, healer, etc..
    std::string comments;
    bool isGroup;

    /// The concrete dungeons a RANDOM selection expanded to, kept so a proposal can name
    /// one. Empty for a normal queue.
    ///
    /// dungeonList holds what the player asked for, which for a random queue is the single
    /// category row -- that is what the client is shown and what the reward lookup keys on,
    /// so it must not be replaced. But a category row is not a place: all 12 TypeID 6 rows
    /// in LfgDungeons.dbc carry MapID 0 or 0xFFFFFFFF, so proposing one teleports the group
    /// nowhere. The expansion is therefore kept alongside rather than collapsed away.
    std::set<uint32> candidateDungeons;

    /// The random category the entry requested, kept independently from the
    /// concrete candidates. Zero for an ordinary specific-dungeon selection.
    uint32 randomDungeonID = 0;

    // Zeroed: the default constructor left these indeterminate, and needed* decides both
    // whether an entry is complete and what the queue advertises to the client.
    time_t joinedTime = 0;
    /// The queue ticket. Retail never sends 0 in any of the 5291 observed status
    /// bodies; it is stable for the life of a queue entry and the client ECHOES IT
    /// BACK verbatim in CMSG_LFG_PROPOSAL_RESPONSE and CMSG_LFG_LEAVE, so with 0 the
    /// client's own replies cannot be matched to the entry that produced them.
    uint32 ticketId = 0;
    uint8 neededTanks = 0;
    uint8 neededHealers = 0;
    uint8 neededDps = 0;

    LFGPlayers() : currentState(LFG_STATE_NONE), currentRoles(0), isGroup(false) {}
    LFGPlayers(LFGState state, std::set<uint32> dungeonSelection, roleMap CurrentRoles, std::string comment, bool IsGroup, time_t JoinedTime,
        uint8 NeededTanks, uint8 NeededHealers, uint8 NeededDps) : currentState(state), dungeonList(dungeonSelection),
        currentRoles(CurrentRoles), comments(comment), isGroup(IsGroup), joinedTime(JoinedTime), neededTanks(NeededTanks),
        neededHealers(NeededHealers), neededDps(NeededDps) {}
};

struct LFGRoleCheck
{
    LFGRoleCheckState state;      // current status of the role check
    roleMap currentRoles;         // map of players to roles
    std::set<uint32> dungeonList; // The dungeons this player or group are queued for
    uint32 randomDungeonID;       // The random dungeon ID
    uint64 leaderGuidRaw;         // ObjectGuid(raw) of leader
    time_t waitForRoleTime;       // How long we'll wait for the players to confirm their roles
};

struct LFGWait
{
    int32 time;                   // current wait time for x (in seconds, so (time_t x / IN_MILLISECONDS)
    int32 previousTime;           // how long it took for the last person to go from queue to instance
    uint32 playerCount;           // amount of players in x queue for calculations [not sure if needed when finished implementing system]
    bool doAverage;               // tells the lfgmgr during a world update whether or not to recalculate waiting time

    LFGWait() : time(-1), previousTime(-1), playerCount(0), doAverage(false) {}
    LFGWait(int32 currentTime, int32 lastTime, uint32 currentPlayerCount, bool shouldRecalculate)
        : time(currentTime), previousTime(lastTime), playerCount(currentPlayerCount), doAverage(shouldRecalculate) {}
};

/// For SMSG_LFG_QUEUE_STATUS
struct LFGQueueStatus
{
    uint64 queueGuid;             // player GUID for solo queues, group GUID otherwise
    uint32 dungeonID;             // queue info for x dungeon
    int32  playerAvgWaitTime;     // average wait time for the current player
    int32  avgWaitTime;           // average wait time for the dungeon
    int32  tankAvgWaitTime;       // average wait time for the tank(s)
    int32  healerAvgWaitTime;     // average wait time for the healer(s)
    int32  dpsAvgWaitTime;        // average wait time for the dps'
    uint8  neededTanks;           // amount of tanks needed
    uint8  neededHeals;           // amount of healers needed
    uint8  neededDps;             // amount of dps needed
    uint32 timeSpentInQueue;      // time already spent in the queue
    uint32 joinTime;              // server epoch time when the queue entry was created
    uint32 ticketId;              // retail's clientQueueId equals the status packet's ticketId
};

/// For CMSG_LFG_GET_STATUS, SMSG_LFG_UPDATE_PARTY, and SMSG_LFG_UPDATE_PLAYER
struct LFGPlayerStatus
{
    LFGState state;
    LfgUpdateType updateType;
    std::set<uint32> dungeonList;
    std::string comment;

    LFGPlayerStatus() : state(LFG_STATE_NONE), updateType(LFG_UPDATE_DEFAULT) { }
    LFGPlayerStatus(LFGState State, LfgUpdateType UpdateType, std::set<uint32> DungeonList, std::string Comment)
        : state(State), updateType(UpdateType), dungeonList(DungeonList), comment(Comment) { }
};

/// Queue metadata required by the 5.4.8 SMSG_LFG_UPDATE_STATUS packet.
struct LFGStatusPacketData
{
    uint32 roles = 0;
    uint32 joinedTime = 0;
    uint32 ticketId = 0;
    uint8 neededTanks = 0;
    uint8 neededHealers = 0;
    uint8 neededDps = 0;
};

/// Information on a group currently in a dungeon
struct LFGGroupStatus //todo: check for this in joinlfg function, not lfgplayers struct
{
    LFGState state;        // State of the group
    uint32 dungeonID;      // ID of the dungeon the group should be in (the RESOLVED one)
    /// Has this run made enough progress that leaving is no longer desertion?
    ///
    /// MoP's rule protected the OPENING of a run: leave or get vote-kicked before the
    /// group had engaged/killed a boss and you took Dungeon Deserter for 30 minutes;
    /// after the first boss you could ordinarily leave clean. Blizzard deliberately kept
    /// the exact predicate hidden and it had edge cases (boss combat, a wipe, or simply
    /// enough time inside could satisfy it, and Heroic Scarlet Monastery was reported in
    /// 2013 to keep awarding Deserter after its first boss when others did not), so this
    /// tracks the one signal that is unambiguous and that we can actually observe: a
    /// credited dungeon encounter.
    bool madeProgress = false;

    /// The random category the group queued under, or 0 for a direct queue. Kept because
    /// SMSG_GROUP_LIST carries BOTH: slot A is the resolved dungeon and slot B the random
    /// row. Retail never puts a type-6 entry in slot A.
    uint32 randomDungeonID = 0;
    roleMap playerRoles;   // Container holding each player's objectguid and their roles
    ObjectGuid leaderGuid; // The group leader's object guid

    LFGGroupStatus() { }
    LFGGroupStatus(LFGState State, uint32 DungeonID, roleMap PlayerRoles, ObjectGuid LeaderGuid)
        : state(State), dungeonID(DungeonID), playerRoles(PlayerRoles), leaderGuid(LeaderGuid) { }
};

/// For SMSG_LFG_PROPOSAL_UPDATE
struct LFGProposal
{
    // Every scalar is initialised. groupRawGuid and groupLeaderGuid in particular are
    // the only two SendDungeonProposal does not always assign -- it sets them solely on
    // the premade path -- yet it READS groupRawGuid to decide whether to set it, and
    // CreateDungeonGroup branches on it to choose between reusing an existing group and
    // making a new one. Left indeterminate, an all-solo proposal picked its branch from
    // whatever was on the stack.
    uint32 id = 0;                  // proposal id
    uint32 dungeonID = 0;           // dungeon id as QUEUED -- for a random queue this is the
                                    // category row, which is what the client is shown and what
                                    // the reward lookup keys on

    /// The dungeon the group is actually put into. Equals dungeonID for a normal queue.
    ///
    /// For a random queue it is a concrete member of the expansion, because the category row
    /// has no map to teleport to. Split from dungeonID rather than replacing it so the
    /// proposal packet and the reward path keep naming the random entry the player chose.
    uint32 concreteDungeonID = 0;

    // The m_playerData key this proposal was built from. The queue entry is kept alive
    // for the lifetime of the proposal so a failure can put the survivors back, which is
    // what the client tells the player happens: ERR_LFG_PROPOSAL_FAILED reads "Someone
    // has declined the invite. You have been returned to the front of the queue."
    ObjectGuid queueGuid;
    time_t createdTime = 0;         // for the timeout reaper
    LFGProposalState state = LFG_PROPOSAL_INITIATING;  // proposal state
    uint32 encounters = 0;          // encounters done
    uint64 groupRawGuid = 0;        // group raw guid value
    uint64 groupLeaderGuid = 0;     // group leader's guid
    bool isNew = true;              // is new or old group
    roleMap currentRoles;           // group player's roles
    proposalAnswerMap answers;      // answers to a proposal
    playerGroupMap groups;          // data on which groups players belong/belonged to
    time_t joinedQueue = 0;         // time from when the players joined the queue
};

// For SMSG_LFG_PLAYER_REWARD
struct LFGRewards
{
    uint32 randomDungeonEntry;  // Entry of the random dungeon done (0 if not random)
    uint32 groupDungeonEntry;   // Entry of the dungeon done by your group
    bool hasDoneDaily;          // First dungeon of the day?
    uint32 moneyReward;         // Amount of money rewarded
    uint32 expReward;           // Amount of experience rewarded
    uint32 itemID;              // ID of item reward
    uint32 itemAmount;          // How many of x item is rewarded

    LFGRewards() { }
    LFGRewards(uint32 RandomDungeonEntry, uint32 GroupDungeonEntry, bool HasDoneDaily,
        uint32 MoneyReward, uint32 ExpReward, uint32 ItemID, uint32 ItemAmount) :
        randomDungeonEntry(RandomDungeonEntry), groupDungeonEntry(GroupDungeonEntry),
        hasDoneDaily(HasDoneDaily), moneyReward(MoneyReward), expReward(ExpReward),
        itemID(ItemID), itemAmount(ItemAmount) { }
};

// For SMSG_LFG_BOOT_PLAYER
struct LFGBoot
{
    bool inProgress;           // Is the boot vote still occurring?
    ObjectGuid playerVotedOn;  // ObjectGuid of the player being voted on
    std::string reason;        // Reason stated for the vote
    proposalAnswerMap answers; // Player's votes
    time_t startTime;          // When the vote started

    LFGBoot() { }
    LFGBoot(bool InProgress, ObjectGuid PlayerVotedOn, std::string Reason, proposalAnswerMap Answers, time_t StartTime)
        : inProgress(InProgress), playerVotedOn(PlayerVotedOn), reason(Reason), answers(Answers), startTime(StartTime) { }
};

// End Section: Structures

class LFGMgr
{
public:
    LFGMgr();
    ~LFGMgr();

    /// Update queue information and such
    void Update();

    /**
     * @brief Attempt to join the dungeon finder queue, as long as the player(s)
     *        fit the criteria.
     *
     * @param roles Roles selected in lfg window
     * @param dungeons List of dungeon(s) selected
     * @param comments Comments made by the player
     * @param plr Pointer to the player sending the packet
     */
    void JoinLFG(uint32 roles, std::set<uint32> dungeons, std::string comments, Player* plr);

    /**
     * @brief Leave the lfg/dungeon finder system.
     *
     * @param plr The pointer to the player sending the request
     * @param isGroup Whether or not they are the leader of a group / in a group
     */
    void LeaveLFG(Player* plr, bool isGroup);

    /**
     * @brief Go through a number of checks to see if the player/group can join
     *        the LFG queue
     *
     * @param plr The pointer to the player
     */
    /// \param queueIsRandom the request contains a random category. The Dungeon
    ///        Cooldown (71328) gates only random queues, so a specific-dungeon request
    ///        must pass while it is active.
    LfgJoinResult GetJoinResult(Player* plr, bool queueIsRandom);

    /**
     * @brief Fetch the playerstatus struct of a player on request, if existant
     *
     * @param guid the player's objectguid
     */
    LFGPlayerStatus GetPlayerStatus(ObjectGuid guid);

    /// Fetch the subset of queue metadata exposed by SMSG_LFG_UPDATE_STATUS.
    bool GetStatusPacketData(ObjectGuid queueGuid, ObjectGuid playerGuid, LFGStatusPacketData& data) const;

    /**
     * @brief Set the player's comment string
     *
     * @param guid The player's objectguid
     * @param comment Their comments
     */
    void SetPlayerComment(ObjectGuid guid, std::string comment);

    /**
     * @brief Set the player's LFG state
     *
     * @param guid The player's objectguid
     * @param state the LFGState value
     */
    void SetPlayerState(ObjectGuid guid, LFGState state);

    /// Current `.debug dungeon` mode; LFG_DEBUG_OFF unless an administrator enabled it.
    LFGDebugMode GetDebugMode() const { return m_debugMode; }
    void SetDebugMode(LFGDebugMode mode) { m_debugMode = mode; }

    /**
     * @brief Set the player's LFG update type
     *
     * @param guid The player's objectguid
     * @param updateType The LfgUpdateType value
     */
    void SetPlayerUpdateType(ObjectGuid guid, LfgUpdateType updateType);

    /**
     * @brief Used to fetch the item rewards of a dungeon from the database
     *
     * @param dungeonId the dungeon ID used in the DBCs
     * @param type the type of dungeon
     */
    ItemRewards GetDungeonItemRewards(uint32 dungeonId, DungeonTypes type);

    /**
     * @brief Used to determine the type of dungeon for ease of use.
     *
     * @param dungeonId the dungeon ID used in the DBCs
     */
    DungeonTypes GetDungeonType(uint32 dungeonId);

    /**
     * @brief Used to record the first time a player has entered x type of dungeon in the day.
     *
     * @param guidLow the player's guidLow
     * @param dungeon the specific type/expansion of dungeon
     */
    void RegisterPlayerDaily(uint32 guidLow, DungeonTypes dungeon);

    /**
     * @brief Used to find whether or not the player has done x type of dungeon today.
     *
     * @param guidLow the player's guidLow
     * @param dungeon the specific type/expansion of dungeon
     */
    bool HasPlayerDoneDaily(uint32 guidLow, DungeonTypes dungeon);

    /// Reset accounts of players completing a/any dungeon for the day for new rewards
    void ResetDailyRecords();

    /**
     * @brief Find out whether or not a special dungeon is available for that season
     *
     * @param dungeonId the ID of the dungeon in question
     */
    bool IsSeasonActive(uint32 dungeonId);

    /**
     * @brief Find the random dungeons applicable for a player
     *
     * @param level The level of said player
     * @param expansion The player's expansion
     */
    dungeonEntries FindRandomDungeonsForPlayer(uint32 level, uint8 expansion);

    /**
     * @brief Find the random dungeons not applicable for a player
     *
     * @param plr The player to test against
     */
    dungeonForbidden FindRandomDungeonsNotForPlayer(Player* plr);

    /// Given the ID of a dungeon, spit out its entry
    uint32 GetDungeonEntry(uint32 ID);

    /// The resolved dungeon entry for a group that is in (or heading into) an LFG
    /// dungeon, or 0 if it is not an LFG group. SMSG_GROUP_LIST carries this in its
    /// LFG block; retail never sends the block with a zero entry.
    uint32 GetGroupDungeonEntry(ObjectGuid groupGuid);

    /// The random-category entry a group queued under, or 0. SMSG_GROUP_LIST slot B.
    uint32 GetGroupRandomDungeonEntry(ObjectGuid groupGuid);

    /// LFG state of a group, for the SMSG_GROUP_LIST state byte.
    LFGState GetGroupLfgState(ObjectGuid groupGuid);

    /// Drop a disbanded group's LFG status. Must run when the Group is torn down, not
    /// when its dungeon finishes -- see the note in HandleBossKilled.
    /// Drop a group's LFG state AND reset its members' player states.
    void ReleaseGroupLfgStatus(Group* pGroup);

    /// A dungeon encounter was credited on this map. Marks every LFG group with players
    /// present as having made progress, so leaving no longer earns Dungeon Deserter, and
    /// on the LAST encounter runs the completion/reward path.
    void OnDungeonEncounterCredited(Map* map, bool lastEncounter);

    /// Called when a player leaves an LFG group. Applies Dungeon Deserter if the run had
    /// not yet made progress.
    void OnPlayerLeftDungeonGroup(Player* pPlayer);

    /// Always-runs cleanup for a player leaving an LFG group: clears their LFG state
    /// and withdraws any vote they had cast.
    void OnPlayerLeftLfgGroup(Player* pPlayer, Group* pGroup);

    /// Is this player standing inside the dungeon of a live LFG run?
    bool IsPlayerInLfgDungeon(Player* pPlayer);

    /// Applies the 15-minute requeue cooldown that retail starts when a player enters.
    void ApplyDungeonCooldown(Player* pPlayer);

    /// Record where this player must be returned to when they leave the dungeon.
    /// Called ONCE, when the queue is joined -- never on entry. See the definition.
    void RecordEntryPoint(Player* pPlayer);

    /// Rebuild a dungeon group's LFG status after a restart, from persisted state alone.
    /// Called once per group_instance bind while groups load. See the definition.
    void RestoreDungeonGroup(Group* pGroup, uint32 mapId, uint32 difficulty, uint32 encountersMask);

    /// Return the 5.4.8 LFG status category byte for a dungeon.
    uint8 GetDungeonCategory(uint32 ID);

    /// Teleports a player out of a dungeon (called by CMSG_LFG_TELEPORT)
    void TeleportPlayer(Player* pPlayer, bool out);

    /// Queue Functions Below

    /**
     * Find the player's or group's information and update the system with
     *     the amount of each role they need to find.
     *
     * @param guid The guid assigned to the structure
     * @param information The LFGPlayers structure containing their information
     */
    void UpdateNeededRoles(ObjectGuid guid, LFGPlayers* information);

    /**
     * @brief Fire a proposal for this entry if every role it needs is filled, and
     *        dequeue it so it cannot be matched or proposed again.
     * @return true if a proposal was sent (the entry no longer exists).
     */
    bool TryFormGroup(ObjectGuid guid);

    /**
     * @brief Cancel a proposal, remove the players responsible, and return everyone else
     *        to the queue.
     *
     * @param proposalId the proposal to cancel
     * @param culprits   players removed from the dungeon finder entirely (the decliner
     *                   and, if they were in a premade, that premade). Empty on timeout,
     *                   where nobody is singled out.
     */
    void CancelProposal(uint32 proposalId, std::set<ObjectGuid> const& culprits);

    /// Cancel proposals nobody answered within LFG_TIME_PROPOSAL.
    void RemoveOldProposals();

    /// The decline half of ProposalUpdate: work out who is responsible and cancel.
    void DeclineProposal(ObjectGuid plrGuid, LFGProposal* proposal);

    /**
     * @brief The key of the queue entry that LISTS this player.
     *
     * After a merge an absorbed player has no entry under their own guid -- MergeGroups
     * folds them into the absorbing entry and erases theirs -- so anything keyed on the
     * player's own guid silently misses them.
     *
     * @return the entry key, or an empty guid if the player is not queued anywhere.
     */
    ObjectGuid FindQueueEntryContaining(ObjectGuid plrGuid) const;

    /// Is there a proposal still awaiting this player's answer? Authoritative, unlike
    /// the LFG_STATE_PROPOSAL status flag, which several paths can leave stale.
    bool HasLiveProposalFor(ObjectGuid plrGuid) const;

    /// Cancel every live proposal listing this player, counting them as the culprit.
    /// Used when they leave the finder while a proposal is still open.
    void CancelProposalsFor(ObjectGuid plrGuid);

    /**
     * @brief Take a single player out of whichever queue entry holds them, recomputing
     *        that entry's needed roles, and drop the entry if it is left empty.
     */
    void RemovePlayerFromQueue(ObjectGuid plrGuid);

    /// Does this queue entry contain at least one game master? Scopes `.debug dungeon`.
    bool EntryHasGameMaster(LFGPlayers const* entry) const;

    /**
     * @brief Add the player or group to the Dungeon Finder queue
     *
     * @param guid the player/group's ObjectGuid
     */
    void AddToQueue(ObjectGuid guid);

    /**
     * @brief Remove the player or group from the Dungeon Finder queue
     *
     * @param guid the player/group's ObjectGuid
     */
    void RemoveFromQueue(ObjectGuid guid);

    /// Search the queue for compatible matches
    void FindQueueMatches();

    /**
     * @brief Search the queue for matches based off of one's guid
     *
     * @param guid The player or group's guid
     */
    void FindSpecificQueueMatches(ObjectGuid guid);

    /// Send a periodic status update for queued players
    void SendQueueStatus();
    void SendQueueStatusFor(ObjectGuid queueGuid, time_t timeNow);

    /// Non-zero, stable per queue entry, monotonic. See LFGPlayers::ticketId.
    uint32 AllocateTicketId() { return ++m_nextTicketId; }

    /// The complete ticket identity a player's status bodies have been going out under.
    ///
    /// The client files each status body under a 20-byte RideTicket and looks records up
    /// by comparing it whole, so every body about one queue MUST carry the same requester,
    /// id and time. If a later body changes any field the client creates a second record
    /// instead of updating the first, and the first can never be addressed again.
    /// Re-deriving identity from live state cannot guarantee that: the entry may have been
    /// erased (a merge folds an absorbed queuer's entry away) or not yet stored.
    typedef LFGStatePolicy::TicketIdentity RetainedTicket;
    typedef std::unordered_map<ObjectGuid, RetainedTicket> retainedTicketMap;

    /// FIRST WINS. Once a player's bodies have gone out under a ticket, every later body
    /// about that queue must carry the same one or the client files a second record.
    ///
    /// Overwriting would reintroduce the very failure this exists to stop: MergeGroups
    /// erases an absorbed solo queuer's entry, after which GetStatusPacketData falls back
    /// to whichever entry now LISTS them -- the absorbing one -- and hands back a stranger's
    /// ticket. Adopting it would strand that player's own join record for good.
    ///
    /// Cleared by ForgetTicket when the queue genuinely ends, so the next join starts fresh.
    void RetainTicket(ObjectGuid plrGuid, ObjectGuid requesterGuid, uint32 id, uint32 time)
    {
        if (!id)
        {
            return;
        }

        m_retainedTickets[plrGuid].Retain(requesterGuid.GetRawValue(), id, time);
    }

    bool GetRetainedTicket(ObjectGuid plrGuid, RetainedTicket& out) const
    {
        retainedTicketMap::const_iterator it = m_retainedTickets.find(plrGuid);
        if (it == m_retainedTickets.end() || !it->second.id)
        {
            return false;
        }
        out = it->second;
        return true;
    }

    void ForgetTicket(ObjectGuid plrGuid) { m_retainedTickets.erase(plrGuid); }

    /// Start a NEW queue for this player: overwrite whatever was retained.
    ///
    /// Ticket lifetime is tied to the QUEUE ENTRY, not to any packet. Dropping it when a
    /// leave body goes out looked right and is not: the accept path also sends
    /// LFG_UPDATE_LEAVE (the player left the queue, not the session), so the ticket went
    /// away while the client's record was still live, and every later body -- including
    /// the ones answering the player's own Leave Queue clicks -- had nothing to quote and
    /// went out with ticketId = 0. The client cannot file those against any record, so the
    /// eye stayed lit however many times it was clicked. Observed live at 11:31:09 with
    /// five such refusals in a row.
    ///
    /// Overwriting here is safe precisely because it happens when a new entry is built:
    /// the old queue is over, and the new one owns the player's records from now on.
    void BeginTicket(ObjectGuid plrGuid, ObjectGuid requesterGuid, uint32 id, uint32 time)
    {
        m_retainedTickets[plrGuid].Begin(requesterGuid.GetRawValue(), id, time);
    }

    /// Role-Related Functions

    /**
     * @brief Set and/or confirm roles for a group.
     *
     * @param pPlayer The pointer to the player issuing the request
     * @param pGroup The pointer to that player's group
     * @param roles The group leader's role(s)
     */
    void PerformRoleCheck(Player* pPlayer, Group* pGroup, uint8 roles);

    /// Make sure role selections are okay
    bool ValidateGroupRoles(roleMap groupMap, std::set<uint32> const& dungeonList);

    /**
     * @brief Can every player fill exactly one of the roles they ticked, within the
     *        role counts the dungeon's own DBC row asks for?
     *
     * Handles multi-role selections: the client offers four independent checkboxes,
     * so a mask carrying tank|damage is a player willing to be either.
     */
    bool RolesAreValidForDungeons(roleMap const& roles, std::set<uint32> const& dungeonList);

    /// Proposal-Related Functions

    void ProposalUpdate(uint32 proposalID, ObjectGuid plrGuid, bool accepted);

    /// Handles reward hooks -- called by achievement manager
    void HandleBossKilled(Player* pPlayer);

    /// Group kick hook
    void AttemptToKickPlayer(Group* pGroup, ObjectGuid guid, ObjectGuid kicker, std::string reason);

    /// Expire boot votes that ran past LFG_TIME_BOOT. An expired vote always fails.
    void RemoveOldBoots();

    // Called when a player votes yes or no on a boot vote
    void CastVote(Player* pPlayer, bool vote);

protected:
    bool IsSeasonal(uint32 dbcFlags) { return ((dbcFlags & LFG_FLAG_SEASONAL) != 0) ? true : false; }

    /// Check if player/party is already in the system, return that data
    LFGPlayers* GetPlayerOrPartyData(ObjectGuid guid);

    /// Get a proposal structure given its id
    LFGProposal* GetProposalData(uint32 proposalID);

    /// Get information on a group currently in a dungeon
    LFGGroupStatus* GetGroupStatus(ObjectGuid guid);

    /// Add the player to their respective waiting map for their dungeon
    void AddToWaitMap(uint8 role, std::set<uint32> dungeons);

    /// Checks if any players have the leader flag for their roles
    bool HasLeaderFlag(roleMap const& roles);

    /// Compares two groups/players to see if their role combinations are compatible
    bool RoleMapsAreCompatible(LFGPlayers* groupOne, LFGPlayers* groupTwo, std::set<uint32> const& compatibleDungeons);

    /// Checks whether or not two combinations of players/groups are on the same team (alliance/horde)
    bool MatchesAreOfSameTeam(LFGPlayers* groupOne, LFGPlayers* groupTwo);

    /// Are the players in a proposal already grouped up?
    /// Is this a finder group whose run is still live?
    ///
    /// GROUPTYPE_LFD is never cleared anywhere, so membership alone also matches a
    /// finished run whose party stayed together -- hence the state test as well.
    bool IsLiveLfgRun(Group* pGroup);

    /// The group a proposal must be built INTO, or an empty guid to build a fresh one.
    ///
    /// Replaces IsProposalSameGroup, which asked the wrong question: it required EVERY
    /// member to share one group, so a backfill (a live group plus one solo queuer) always
    /// answered false and a brand new group was formed -- and with it a brand new instance.
    /// It was also right to refuse a plain world premade, which this preserves: only a LIVE
    /// FINDER RUN is continued, never an ordinary party.
    ///
    /// outLiveRuns > 1 means the matchmaker merged two live runs, which must not happen:
    /// every fork treats that as hard-incompatible. Callers refuse rather than pick one.
    ObjectGuid ResolveContinuingGroup(roleMap const& members, uint32& outLiveRuns);

    /// Update a proposal after a player refused to join
    void ProposalDeclined(ObjectGuid guid, LFGProposal* proposal);

    /// Updates a wait map with the amount of time it took the last player to join
    void UpdateWaitMap(LFGRoles role, uint32 dungeonID, time_t waitTime);

    /// Sends a group to the dungeon assigned to them
    /// Teleport into the dungeon. Passing onlyPlayer restricts the move to that
    /// member while still resolving the destination from the whole group; NULL
    /// moves every eligible member, which is what a proposal accept needs.
    void TeleportToDungeon(uint32 dungeonID, Group* pGroup, Player* onlyPlayer = NULL);

    /// Grant a completion reward item, mailing whatever does not fit in the bags.
    void GiveDungeonRewardItem(Player* pPlayer, uint32 itemId, uint32 amount);

    /**
     * @brief Merges two players/groups/etc into one for dungeon assignment.
     *
     * @param guidOne The guid assigned to the first group in m_playerData
     * @param guidTwo The guid assigned to the second group in m_playerData
     * @param compatibleDungeons The dungeons that both players or groups agreed to doing
     */
    void MergeGroups(ObjectGuid guidOne, ObjectGuid guidTwo, std::set<uint32> compatibleDungeons);

    /// Send a proposal to each member of a group. Returns false without mutating
    /// queue/player/proposal state when no valid proposal can be constructed.
    bool SendDungeonProposal(ObjectGuid queueGuid, LFGPlayers* lfgGroup);

    /// Tell a group member that someone else just confirmed their role
    void SendRoleChosen(ObjectGuid plrGuid, ObjectGuid confirmedGuid, uint8 roles);

    /// Send SMSG_LFG_ROLE_CHECK_UPDATE to a specific player
    void SendRoleCheckUpdate(ObjectGuid plrGuid, LFGRoleCheck const& roleCheck);

    /// Send SMSG_LFG_UPDATE_PARTY or SMSG_LFG_UPDATE_PLAYER
    void SendLfgUpdate(ObjectGuid plrGuid, LFGPlayerStatus status, bool fallbackIsGroup);

    /// Send SMSG_LFG_JOIN_RESULT
    void SendLfgJoinResult(ObjectGuid plrGuid, LfgJoinResult result, uint8 detail, partyForbidden const& lockedDungeons);

    /// Get rid of expired role checks
    void RemoveOldRoleChecks();

private:
    struct DungeonGroupPlan;

    /// Resolve every fallible proposal-completion decision before success packets.
    bool PrepareDungeonGroup(LFGProposal* proposal, DungeonGroupPlan& plan,
                             std::set<ObjectGuid>& culprits);

    /// Commit a preflighted group plan; no client-visible refusal remains here.
    void CreateDungeonGroup(LFGProposal* proposal, DungeonGroupPlan const& plan);

    /// Complete a boot vote through one state-restoration and record-removal path.
    /// removeVictim is true for a passed vote or when the target leaves voluntarily;
    /// notify is false only while the whole group is being disbanded.
    void FinishBootVote(ObjectGuid groupGuid, Group* pGroup, LFGBoot boot,
                        bool removeVictim, bool notify);

    /// Daily occurences of a player doing X type dungeon
    dailyEntries m_dailyAny;
    dailyEntries m_dailyTBCHeroic;
    dailyEntries m_dailyLKNormal;
    dailyEntries m_dailyLKHeroic;

    /// General info related to joining / leaving the dungeon finder
    playerData m_playerData;
    queueSet   m_queueSet;
    uint32     m_nextTicketId;
    retainedTicketMap m_retainedTickets;

    /// Dungeon Finder Status for players
    playerStatusMap m_playerStatusMap;

    groupSet m_groupSet;
    groupStatusMap m_groupStatusMap;

    /// Role check information
    roleCheckMap m_roleCheckMap;

    /// Boot vote information
    bootStatusMap m_bootStatusMap;

    /// Wait times for the queue
    waitTimeMap m_tankWaitTime;
    waitTimeMap m_healerWaitTime;
    waitTimeMap m_dpsWaitTime;
    waitTimeMap m_avgWaitTime;

    /// Proposal information
    uint32 m_proposalId;
    LFGDebugMode m_debugMode = LFG_DEBUG_OFF;
    proposalMap m_proposalMap;
};

#define sLFGMgr MaNGOS::Singleton<LFGMgr>::Instance()

#endif
