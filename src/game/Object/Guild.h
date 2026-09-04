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

#ifndef MANGOSSERVER_GUILD_H
#define MANGOSSERVER_GUILD_H

#define WITHDRAW_MONEY_UNLIMITED    UI64LIT(0xFFFFFFFFFFFFFFFF)
#define WITHDRAW_SLOT_UNLIMITED     0xFFFFFFFF

#include "Common.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "SharedDefines.h"
#include "Opcodes.h"
#include "Util.h"
#include "WorldPacket.h"

#include <string>
#include <vector>

class WorldPacket;

namespace MopGuildBankPackets
{
    struct GuildBankList;
}

namespace MopGuildPackets
{
    struct EmblemDesign
    {
        uint64 vendorGuid = 0;
        uint32 emblemStyle = 0;
        uint32 emblemColor = 0;
        uint32 borderStyle = 0;
        uint32 borderColor = 0;
        uint32 backgroundColor = 0;
    };

    inline uint8 GuidByte(uint64 guid, uint8 index)
    {
        return uint8(guid >> (index * 8));
    }

    inline uint64 ReadGuid(WorldPacket& in, uint8 const (&maskOrder)[8],
        uint8 const (&byteOrder)[8])
    {
        uint8 guidBytes[8] = {};
        for (uint8 index : maskOrder)
            guidBytes[index] = in.ReadBit();
        for (uint8 index : byteOrder)
            in.ReadByteSeq(guidBytes[index]);

        uint64 guid = 0;
        for (uint8 index = 0; index < 8; ++index)
            guid |= uint64(guidBytes[index]) << (index * 8);
        return guid;
    }

    inline uint64 ReadTabardVendorActivate(WorldPacket& in)
    {
        uint8 const maskOrder[] = { 2, 1, 4, 6, 3, 5, 0, 7 };
        uint8 const byteOrder[] = { 7, 6, 2, 5, 0, 1, 4, 3 };
        return ReadGuid(in, maskOrder, byteOrder);
    }

    inline void BuildTabardVendorActivate(WorldPacket& out, uint64 vendorGuid)
    {
        uint8 const maskOrder[] = { 1, 5, 0, 7, 4, 6, 3, 2 };
        uint8 const byteOrder[] = { 5, 4, 2, 3, 6, 0, 1, 7 };

        out.Initialize(SMSG_TABARD_VENDOR_ACTIVATE, 9);
        for (uint8 index : maskOrder)
            out.WriteBit(GuidByte(vendorGuid, index) != 0);
        out.FlushBits();
        for (uint8 index : byteOrder)
            out.WriteByteSeq(GuidByte(vendorGuid, index));
    }

    /// One bit or byte slot in a packet that interleaves two guids.
    struct GuidBitRef
    {
        uint8 which;                                        // 0 = first guid, 1 = second
        uint8 index;                                        // guid byte 0..7
    };

    /**
     * CMSG_GUILD_QUERY_RANKS body, orders taken from the client's own send
     * serializer sub_C860F3 in Wow.exe 5.4.8.18414.
     */
    inline uint64 ReadGuildQueryRanks(WorldPacket& in)
    {
        uint8 const maskOrder[] = { 0, 2, 5, 4, 3, 7, 6, 1 };
        uint8 const byteOrder[] = { 6, 0, 1, 7, 3, 2, 5, 4 };
        return ReadGuid(in, maskOrder, byteOrder);
    }

    /**
     * CMSG_GUILD_ROSTER body, orders taken from the client's own send serializer
     * sub_C85E7C. It holds the two guids at object offsets +16..23 and +24..31 and
     * interleaves their mask bits and bytes.
     */
    inline void ReadGuildRoster(WorldPacket& in, uint64& guidA, uint64& guidB)
    {
        static GuidBitRef const maskOrder[16] =
        {
            { 0, 4 }, { 0, 3 }, { 0, 2 }, { 1, 7 }, { 0, 0 }, { 0, 6 }, { 0, 5 }, { 1, 0 },
            { 1, 2 }, { 1, 6 }, { 0, 7 }, { 1, 1 }, { 0, 1 }, { 1, 4 }, { 1, 5 }, { 1, 3 }
        };
        static GuidBitRef const byteOrder[16] =
        {
            { 0, 7 }, { 1, 3 }, { 1, 0 }, { 1, 1 }, { 0, 4 }, { 0, 3 }, { 0, 0 }, { 0, 1 },
            { 1, 6 }, { 1, 7 }, { 0, 5 }, { 1, 5 }, { 0, 6 }, { 0, 2 }, { 1, 4 }, { 1, 2 }
        };

        uint8 bytes[2][8] = {};
        for (GuidBitRef const& ref : maskOrder)
            bytes[ref.which][ref.index] = in.ReadBit();
        for (GuidBitRef const& ref : byteOrder)
            in.ReadByteSeq(bytes[ref.which][ref.index]);

        guidA = 0;
        guidB = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            guidA |= uint64(bytes[0][index]) << (index * 8);
            guidB |= uint64(bytes[1][index]) << (index * 8);
        }
    }

    /**
     * SMSG_GUILD_PERMISSIONS body. Field order, the 21-bit tab count and the
     * slots-before-rights tab pairing are all fixed by retail capture; see the
     * comment on WorldSession::HandleGuildPermissions.
     */
    inline void BuildGuildPermissions(WorldPacket& out, uint32 rankId, uint32 moneyPerDay,
        uint32 purchasedTabs, uint32 rankRights,
        uint32 const (&remainingSlots)[GUILD_BANK_MAX_TABS],
        uint32 const (&tabRights)[GUILD_BANK_MAX_TABS])
    {
        out.Initialize(SMSG_GUILD_PERMISSIONS, 4 * 4 + 3 + GUILD_BANK_MAX_TABS * 2 * 4);
        out << uint32(rankId);
        out << uint32(moneyPerDay);
        out << uint32(purchasedTabs);
        out << uint32(rankRights);
        out.WriteBits(GUILD_BANK_MAX_TABS, 21);
        out.FlushBits();
        for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
        {
            out << uint32(remainingSlots[tab]);
            out << uint32(tabRights[tab]);
        }
    }

    /// One rank as SMSG_GUILD_QUERY_RESPONSE carries it: a display index, the rank's
    /// own id, and its name. The two numbers are genuinely different -- retail shows
    /// (0,0) (3,5) (2,6) (1,9) in one capture -- so neither can be derived.
    struct QueryRank
    {
        uint32 index = 0;
        uint32 rankId = 0;
        std::string name;
    };

    /**
     * SMSG_GUILD_QUERY_RESPONSE body.
     *
     * Proven byte-for-byte against capture-000004 seq 39473 (133 bytes): a guid bit,
     * a has-data bit, a 21-bit rank count, four guid bits, one 7-bit name length per
     * rank, four more guid bits, a 7-bit guild-name length, seven more guid bits, a
     * flush, then the byte block -- and finally the guid's present bytes a SECOND
     * time in a different order. That duplication is not a transcription slip; the
     * capture carries both copies and they are identical.
     *
     * The inherited body was a different packet entirely: a raw ObjectGuid, then
     * null-terminated strings, then always ten ranks. Nothing about it matched.
     */
    inline bool BuildGuildQueryResponse(WorldPacket& out, uint64 guid,
        std::string const& guildName, std::vector<QueryRank> const& ranks,
        uint32 emblemStyle, uint32 emblemColor, uint32 borderStyle, uint32 borderColor,
        uint32 backgroundColor, uint32 realm)
    {
        if (ranks.size() >= (size_t(1) << 21) || guildName.size() >= (size_t(1) << 7))
            return false;
        for (QueryRank const& rank : ranks)
            if (rank.name.size() >= (size_t(1) << 7))
                return false;

        out.Initialize(SMSG_GUILD_QUERY_RESPONSE, 40 + guildName.size() + ranks.size() * 24);

        out.WriteBit(GuidByte(guid, 5) != 0);
        out.WriteBit(true);                                 // has data
        out.WriteBits(uint32(ranks.size()), 21);
        out.WriteBit(GuidByte(guid, 5) != 0);
        out.WriteBit(GuidByte(guid, 1) != 0);
        out.WriteBit(GuidByte(guid, 4) != 0);
        out.WriteBit(GuidByte(guid, 7) != 0);
        for (QueryRank const& rank : ranks)
            out.WriteBits(uint32(rank.name.length()), 7);
        out.WriteBit(GuidByte(guid, 3) != 0);
        out.WriteBit(GuidByte(guid, 2) != 0);
        out.WriteBit(GuidByte(guid, 0) != 0);
        out.WriteBit(GuidByte(guid, 6) != 0);
        out.WriteBits(uint32(guildName.length()), 7);
        for (uint8 index : { 3, 7, 2, 1, 0, 4, 6 })
            out.WriteBit(GuidByte(guid, index) != 0);
        out.FlushBits();

        out << uint32(borderStyle);
        out << uint32(emblemStyle);
        out.WriteByteSeq(GuidByte(guid, 2));
        out.WriteByteSeq(GuidByte(guid, 7));
        out << uint32(emblemColor);
        out << uint32(realm);
        for (QueryRank const& rank : ranks)
        {
            out << uint32(rank.index);
            out << uint32(rank.rankId);
            out.append(rank.name.c_str(), rank.name.size());
        }
        out.append(guildName.c_str(), guildName.size());
        // Background before border, not the other way round. Retail puts 44 and
        // 45 in the slot straight after the guild name (capture-000146 seq 1601
        // and 3811) and GuildColorBorder.dbc has only 17 rows, so that slot
        // cannot be a border colour. Transposed, every tabard drawn from a guild
        // query shows the two colours swapped, and a background id of 17 or more
        // indexes off the end of the border table and draws no tabard at all.
        out << uint32(backgroundColor);
        out.WriteByteSeq(GuidByte(guid, 5));
        out.WriteByteSeq(GuidByte(guid, 4));
        out << uint32(borderColor);
        out.WriteByteSeq(GuidByte(guid, 1));
        out.WriteByteSeq(GuidByte(guid, 6));
        out.WriteByteSeq(GuidByte(guid, 0));
        out.WriteByteSeq(GuidByte(guid, 3));

        // The guid's present bytes again, in a different order. Retail carries both.
        for (uint8 index : { 2, 6, 4, 0, 7, 3, 5, 1 })
            out.WriteByteSeq(GuidByte(guid, index));
        return true;
    }

    /// One rank as SMSG_GUILD_QUERY_RANKS_RESULT carries it.
    struct RankEntry
    {
        uint32 index = 0;
        uint32 bankMoneyPerDay = 0;
        uint32 tabSlots[GUILD_BANK_MAX_TABS] = {};
        uint32 tabRights[GUILD_BANK_MAX_TABS] = {};
        std::string name;
        uint32 rankId = 0;
        uint32 rights = 0;
    };

    /**
     * SMSG_GUILD_QUERY_RANKS_RESULT body.
     *
     * Proven byte-for-byte against capture-000019 seq 185 (447 bytes): a 17-bit
     * rank count, then one 7-bit name length per rank, then a flush, then each
     * rank as index, money-per-day, eight (slots, rights) tab pairs, the name,
     * the rank id and the rights mask.
     *
     * The inherited body wrote an 18-bit count and ordered the per-rank fields
     * index, tabs, money, rights, name, id. That happens to total the same 80
     * bytes plus name, which is why only a byte comparison catches it.
     */
    inline void BuildGuildRanks(WorldPacket& out, std::vector<RankEntry> const& ranks)
    {
        out.Initialize(SMSG_GUILD_QUERY_RANKS_RESULT, 3 + ranks.size() * 92);
        out.WriteBits(uint32(ranks.size()), 17);
        for (RankEntry const& rank : ranks)
            out.WriteBits(uint32(rank.name.length()), 7);
        out.FlushBits();

        for (RankEntry const& rank : ranks)
        {
            out << uint32(rank.index);
            out << uint32(rank.bankMoneyPerDay);
            for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
            {
                out << uint32(rank.tabSlots[tab]);
                out << uint32(rank.tabRights[tab]);
            }
            out.WriteStringData(rank.name);
            out << uint32(rank.rankId);
            out << uint32(rank.rights);
        }
    }

    /**
     * CMSG_GUILD_REQUEST_PARTY_STATE body: the querying player's guild guid.
     *
     * Orders taken from the client's own writer, sub_691C9D, which is slot +4 of
     * the message vtable off_D65520; slot +8 is the thunk that pushes 0x10C3, and
     * slot +12 is the campaign's sub_C84A3D signature. The writer reads the guid
     * from +0x10 of the object, which is where sub_69585B stores it.
     */
    inline uint64 ReadGuildRequestPartyState(WorldPacket& in)
    {
        uint8 const maskOrder[] = { 1, 5, 7, 2, 6, 3, 0, 4 };
        uint8 const byteOrder[] = { 2, 5, 4, 6, 1, 0, 7, 3 };
        return ReadGuid(in, maskOrder, byteOrder);
    }

    /**
     * SMSG_GUILD_PARTY_STATE_RESPONSE body: a fixed 13 bytes.
     *
     * Every one of the 62866 replies in the 18414 corpus is exactly this length,
     * against 62972 requests. Two decoded payloads fix the order:
     * capture-000072 seq 5140 is 0C 00 00 00 | 00 00 00 00 | 01 00 00 00 | 00 and
     * capture-000015 seq 3122 is 02 00 00 00 | 00 00 80 3F | 02 00 00 00 | 80.
     *
     * The first is required=12, present=1, so the trailing flag is false; read the
     * other way round it would be 12 present against 1 required, which would have
     * to set the flag. Two aggregates say the same thing independently: the first
     * dword is never 1 in any capture and the third is 1 in 22797 of them, and the
     * first is never 4 because the client substitutes 4 itself for five-man
     * parties (Minimap.lua, GuildInstanceDifficulty_OnEnter).
     *
     * Field identity is the client's: sub_967C66 stores +0x10 to the flag global,
     * +0x1C to present, +0x14 to required and +0x18 to the multiplier, and
     * InGuildParty (sub_9658A6) returns them as
     * (inGuildParty, numGuildPresent, numGuildRequired, xpMultiplier).
     *
     * The trailing byte is 0x80 for true, so it is one MSB-first bit and a flush,
     * not a uint8 -- which produces identical bytes either way.
     */
    inline void BuildGuildPartyState(WorldPacket& out, uint32 numRequired,
        float xpMultiplier, uint32 numPresent, bool inGuildParty)
    {
        out.Initialize(SMSG_GUILD_PARTY_STATE_RESPONSE, 13);
        out << uint32(numRequired);
        out << float(xpMultiplier);
        out << uint32(numPresent);
        out.WriteBit(inGuildParty);
        out.FlushBits();
    }

    /// Challenge types the 18414 client tracks. Index 0 is unused: the client
    /// counts a type as existing only where the max count is above zero
    /// (GetNumGuildChallenges, sub_96591B), and retail leaves slot 0 at zero.
    #define GUILD_CHALLENGE_TYPES 6

    /**
     * SMSG_GUILD_CHALLENGE_UPDATED body: a fixed 120 bytes, five groups of six
     * uint32. Every one of the 163 replies in the 18414 corpus is that length.
     *
     * The five NAMES are the client's own: the challenge tooltip in
     * Blizzard_GuildInfo.xml reads
     * `local index, current, max, xp, gold, maxGold = GetGuildChallengeInfo(...)`,
     * and GetGuildChallengeInfo (sub_965952) returns dword_11EFE88, _E70, _E28,
     * _E58, _E40 in that order. maxGold is the reward once the guild is at max
     * level, xp the reward below it.
     *
     * Which name sits at which WIRE position is inferred, not proven, because the
     * parser could not be located. The inference: capture-000009 seq 30430 and
     * capture-000025 seq 9450 differ only in the fifth group, so that is the
     * per-guild one, and it is elementwise <= the first, so the first is the max
     * it is measured against. Of the remaining three the largest is xp, and of
     * the other two the larger is maxGold, being exactly twice gold throughout.
     * That gives max, gold, maxGold, xp, current, and the table those two
     * captures carry:
     *   type            max  gold  maxGold  xp
     *   1 Dungeon         7   125     250     300000
     *   2 Raid            1   500    1000    3000000
     *   3 Rated BG        3   250     500    1500000
     *   4 Scenario       15   125     250      50000
     *   5 Challenge Mode  3   250     500    1000000
     * Slot 0 is unused and zero throughout. Corpus-wide, only one value has been
     * checked across all 163 replies -- 300000 at byte offset 76 -- so the rest
     * of the table is static across those two captures, not proven static across
     * the corpus.
     *
     * Do not infer the client's internal order from this. sub_9668AA copies
     * current from the fourth record block and max from the first, so the
     * record is not filled in wire order and the parser's arrangement is
     * unknown -- it does not affect the bytes on the wire.
     */
    inline void BuildGuildChallengeUpdate(WorldPacket& out,
        uint32 const (&maxCount)[GUILD_CHALLENGE_TYPES],
        uint32 const (&gold)[GUILD_CHALLENGE_TYPES],
        uint32 const (&maxGold)[GUILD_CHALLENGE_TYPES],
        uint32 const (&xp)[GUILD_CHALLENGE_TYPES],
        uint32 const (&currentCount)[GUILD_CHALLENGE_TYPES])
    {
        out.Initialize(SMSG_GUILD_CHALLENGE_UPDATED, 120);
        for (uint32 value : maxCount)     { out << uint32(value); }
        for (uint32 value : gold)         { out << uint32(value); }
        for (uint32 value : maxGold)      { out << uint32(value); }
        for (uint32 value : xp)           { out << uint32(value); }
        for (uint32 value : currentCount) { out << uint32(value); }
    }

    /// One member as SMSG_GUILD_ROSTER carries it.
    struct RosterMember
    {
        uint64 guid = 0;
        uint8 cls = 0;
        uint8 level = 0;
        uint8 flags = 0;
        uint8 gender = 0;
        uint32 zoneId = 0;
        uint32 rankId = 0;
        uint32 totalReputation = 0;
        uint32 remainingWeekReputation = 0;
        uint64 totalActivity = 0;
        uint64 weekActivity = 0;
        uint32 achievementPoints = 0;
        uint32 virtualRealm = 0;
        float lastLogoutDays = 0.0f;
        // Two professions, each id/value/rank. Retail populates these; we have no
        // guild profession tracking yet and send zeroes, but the field must exist
        // or the member record is the wrong length.
        uint32 professions[6] = {};
        std::string name;
        std::string publicNote;
        std::string officerNote;
    };

    /**
     * CMSG_GUILD_QUERY body: the player's guid and the guild's, bit-packed and
     * interleaved. Orders come from the client's own send serializer sub_665EE4 in
     * Wow.exe 5.4.8.18414, reached through wrapper sub_66107F which stamps opcode
     * 6838. It holds the two guids at object offsets +16..23 (A) and +24..31 (B).
     *
     * A is the player and B is the guild, which the retail bodies make plain:
     *
     *   capture-000004 seq 692    A 0x040000000513CCA1   B 0x1FF4000001C5F4EA
     *   capture-000004 seq 37898  A 0x0400000000C7C4FC   B 0x1FF400000220C61B
     *
     * B's high half is the same guild high-guid that CMSG_GUILD_QUERY_RANKS decodes
     * to, derived independently, so the two opcodes corroborate each other. Note that
     * value is retail's; this server assigns guild guids from HIGHGUID_GUILD and the
     * client echoes back whatever we sent it, so the lookup stays self-consistent.
     */
    inline void ReadGuildQuery(WorldPacket& in, uint64& playerGuid, uint64& guildGuid)
    {
        static GuidBitRef const maskOrder[16] =
        {
            { 0, 7 }, { 0, 3 }, { 0, 4 }, { 1, 3 }, { 1, 4 }, { 0, 2 }, { 0, 6 }, { 1, 2 },
            { 1, 5 }, { 0, 1 }, { 0, 5 }, { 1, 7 }, { 0, 0 }, { 1, 1 }, { 1, 6 }, { 1, 0 }
        };
        static GuidBitRef const byteOrder[16] =
        {
            { 0, 7 }, { 1, 2 }, { 1, 4 }, { 1, 7 }, { 0, 6 }, { 0, 0 }, { 1, 6 }, { 1, 0 },
            { 1, 3 }, { 0, 2 }, { 1, 5 }, { 0, 3 }, { 1, 1 }, { 0, 4 }, { 0, 1 }, { 0, 5 }
        };

        uint8 bytes[2][8] = {};
        for (GuidBitRef const& ref : maskOrder)
            bytes[ref.which][ref.index] = in.ReadBit();
        for (GuidBitRef const& ref : byteOrder)
            in.ReadByteSeq(bytes[ref.which][ref.index]);

        playerGuid = 0;
        guildGuid = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            playerGuid |= uint64(bytes[0][index]) << (index * 8);
            guildGuid |= uint64(bytes[1][index]) << (index * 8);
        }
    }

    /**
     * SMSG_GUILD_ROSTER body.
     *
     * Proven by decoding capture-000019 seq 923 (235 bytes) field by field and
     * consuming it exactly: a 17-bit member count then a 10-bit MOTD length, a
     * 32-bit block per member, an 11-bit guild-info length, then the member byte
     * data and finally the guild-wide tail.
     *
     * The inherited body had the two header fields in the opposite order and at
     * the wrong widths (11-bit MOTD then 18-bit count), a 12-bit info length, a
     * 7-bit name length, a different member bit order, a different member byte
     * order and a different tail order. The capture settles all of it: the first
     * 17 bits read 2 and the packet carries exactly two names, and the next 10
     * bits read 24 against a 24-character MOTD.
     */
    inline bool BuildGuildRoster(WorldPacket& out, std::vector<RosterMember> const& members,
        std::string const& motd, std::string const& info, uint32 accountsNumber,
        uint32 createdDatePacked, uint32 weeklyReputationCap)
    {
        // Every length below goes out in a bit field narrower than the string it
        // describes. Writing a truncated length and then appending the whole string
        // desynchronises the client's reader for the rest of the packet, so reject
        // rather than emit, the same way the other bounded guild builders do.
        if (members.size() >= (size_t(1) << 17) ||
            motd.size() >= (size_t(1) << 10) ||
            info.size() >= (size_t(1) << 11))
        {
            return false;
        }
        for (RosterMember const& member : members)
        {
            if (member.name.size() >= (size_t(1) << 6) ||
                member.publicNote.size() >= (size_t(1) << 8) ||
                member.officerNote.size() >= (size_t(1) << 8))
            {
                return false;
            }
        }

        ByteBuffer memberData;

        out.Initialize(SMSG_GUILD_ROSTER, 24 + members.size() * 100 + motd.size() + info.size());
        out.WriteBits(uint32(members.size()), 17);
        out.WriteBits(uint32(motd.length()), 10);

        for (RosterMember const& member : members)
        {
            uint64 const guid = member.guid;

            out.WriteBits(uint32(member.officerNote.length()), 8);
            out.WriteBit(GuidByte(guid, 5) != 0);
            out.WriteBit(false);                            // can scroll of resurrect
            out.WriteBits(uint32(member.publicNote.length()), 8);
            out.WriteBit(GuidByte(guid, 7) != 0);
            out.WriteBit(GuidByte(guid, 0) != 0);
            out.WriteBit(GuidByte(guid, 6) != 0);
            out.WriteBits(uint32(member.name.length()), 6);
            out.WriteBit(false);                            // has authenticator
            out.WriteBit(GuidByte(guid, 3) != 0);
            out.WriteBit(GuidByte(guid, 4) != 0);
            out.WriteBit(GuidByte(guid, 1) != 0);
            out.WriteBit(GuidByte(guid, 2) != 0);

            memberData << uint8(member.cls);
            memberData << uint32(member.totalReputation);
            memberData.append(member.name.c_str(), member.name.size());
            memberData.WriteByteSeq(GuidByte(guid, 0));
            for (uint8 field = 0; field < 6; ++field)
            {
                memberData << uint32(member.professions[field]);
            }
            memberData << uint8(member.level);
            memberData << uint8(member.flags);
            memberData << uint32(member.zoneId);
            memberData << uint32(member.remainingWeekReputation);
            memberData.WriteByteSeq(GuidByte(guid, 3));
            memberData << uint64(member.totalActivity);
            memberData.append(member.officerNote.c_str(), member.officerNote.size());
            memberData << float(member.lastLogoutDays);
            memberData << uint8(member.gender);
            memberData << uint32(member.rankId);
            memberData << uint32(member.virtualRealm);
            memberData.WriteByteSeq(GuidByte(guid, 5));
            memberData.WriteByteSeq(GuidByte(guid, 7));
            memberData.append(member.publicNote.c_str(), member.publicNote.size());
            memberData.WriteByteSeq(GuidByte(guid, 4));
            memberData << uint64(member.weekActivity);
            memberData << uint32(member.achievementPoints);
            memberData.WriteByteSeq(GuidByte(guid, 6));
            memberData.WriteByteSeq(GuidByte(guid, 1));
            memberData.WriteByteSeq(GuidByte(guid, 2));
        }

        out.WriteBits(uint32(info.length()), 11);
        out.FlushBits();
        if (memberData.size())
        {
            out.append(memberData);
        }

        out << uint32(accountsNumber);
        out << uint32(createdDatePacked);
        out.append(info.c_str(), info.size());
        out << uint32(weeklyReputationCap);
        out.append(motd.c_str(), motd.size());
        out << uint32(0);
        return true;
    }

    inline EmblemDesign ReadSaveGuildEmblem(WorldPacket& in)
    {
        EmblemDesign design;
        in >> design.borderStyle;
        in >> design.backgroundColor;
        in >> design.borderColor;
        in >> design.emblemColor;
        in >> design.emblemStyle;

        uint8 const maskOrder[] = { 0, 7, 4, 6, 5, 1, 2, 3 };
        uint8 const byteOrder[] = { 6, 2, 7, 5, 0, 4, 1, 3 };
        design.vendorGuid = ReadGuid(in, maskOrder, byteOrder);
        return design;
    }

    inline void BuildSaveGuildEmblemResult(WorldPacket& out, uint32 result)
    {
        out.Initialize(SMSG_SAVE_GUILD_EMBLEM, 4);
        out << result;
    }

    /**
     * SMSG_GUILD_INVITE body, 18414.
     *
     * Rebuilt from the client's own reader sub_69E959 (parser sub_6A0BF8, reached
     * from the inbound dispatcher sub_68EC4C) and verified byte-exact against
     * capture-000499 seq 777 -- 65 of 65 bytes consumed. The inherited body was
     * pre-MoP: six raw uint32 before any bits, and 8/8/7-bit name lengths where
     * the client reads 7/6/7 (sub_6650D3 returns seven bits, sub_691684 six).
     *
     * A is the new guild guid, B the old one. Both name blocks are raw bytes with
     * no terminator; the client NUL-terminates its own buffer after the read.
     *
     * Eight of the nine uint32 are identified. Positions 2, 8 and 5 are
     * borderStyle, emblemStyle and the old guild's realm. Do NOT read anything
     * more into the capture correlation that first suggested them: the guild
     * query response it was correlated against had its own two tabard colours
     * transposed, so that route gave the wrong answer for the colours and was
     * replaced by the consumer route below. Only 7 and 9 are still ambiguous:
     *
     *   1, 3 and 6 are borderColor, emblemColor and backgroundColor, settled by
     *     the consumer route rather than by correlating captures. sub_9683C3
     *     hands 6, 1, 3 and 8 to the tabard resolver sub_831870, which pairs each
     *     input with one output -- arg5 from arg1, arg6 from arg2, arg7 from
     *     arg3 -- and those outputs are pushed to Lua as tabardData[1..9], which
     *     SetGuildTabardTextures reads as background, border then emblem RGB.
     *     The captures agree and rule out the earlier reading: 6 is 44..49,
     *     which only GuildColorBackground's 51 rows can hold, while 1 and 3 stay
     *     inside the 17 rows of the border and emblem colour tables.
     *   7 and 9 are {new-guild realm, inviter realm}. A guild invite is
     *     same-realm, so both carry the same value here and the ambiguity is not
     *     observable on the wire.
     *   4 is the guild level, from the same consumer: it becomes event argument
     *     three, which GuildInviteFrame_OnEvent puts in GuildInviteFrameLevelNumber.
     *     Four captures read 10, 25, 25 and 17, never above the level cap.
     *
     * The consumer is reached through a pointer the client computes rather than
     * stores (sub_6A0F7E calls dword_109735C minus a computed offset), which is
     * why no SMSG handler address appears anywhere in the image as a pointer or
     * a call target. That obfuscates the route; it does not prevent reading it,
     * and sub_9683C3 is ordinary static code once found.
     */
    inline bool BuildGuildInvite(WorldPacket& out,
        uint64 newGuildGuid, uint64 oldGuildGuid,
        std::string const& inviterName, std::string const& newGuildName,
        std::string const& oldGuildName, uint32 guildLevel,
        uint32 emblemStyle, uint32 emblemColor, uint32 borderStyle,
        uint32 borderColor, uint32 backgroundColor,
        uint32 newGuildRealm, uint32 oldGuildRealm, uint32 inviterRealm)
    {
        if (inviterName.size() >= (size_t(1) << 6) ||
            newGuildName.size() >= (size_t(1) << 7) ||
            oldGuildName.size() >= (size_t(1) << 7))
        {
            return false;
        }

        out.Initialize(SMSG_GUILD_INVITE, 48 + inviterName.size() +
            newGuildName.size() + oldGuildName.size());

        out.WriteBit(GuidByte(newGuildGuid, 4) != 0);
        out.WriteBits(uint32(newGuildName.length()), 7);
        out.WriteBit(GuidByte(oldGuildGuid, 4) != 0);
        out.WriteBit(GuidByte(newGuildGuid, 6) != 0);
        out.WriteBit(GuidByte(oldGuildGuid, 2) != 0);
        out.WriteBit(GuidByte(oldGuildGuid, 1) != 0);
        out.WriteBit(GuidByte(oldGuildGuid, 5) != 0);
        out.WriteBit(GuidByte(oldGuildGuid, 7) != 0);
        out.WriteBit(GuidByte(newGuildGuid, 0) != 0);
        out.WriteBit(GuidByte(oldGuildGuid, 3) != 0);
        out.WriteBit(GuidByte(newGuildGuid, 5) != 0);
        out.WriteBit(GuidByte(oldGuildGuid, 6) != 0);
        out.WriteBits(uint32(inviterName.length()), 6);
        out.WriteBit(GuidByte(newGuildGuid, 1) != 0);
        out.WriteBit(GuidByte(newGuildGuid, 3) != 0);
        out.WriteBit(GuidByte(oldGuildGuid, 0) != 0);
        out.WriteBit(GuidByte(newGuildGuid, 2) != 0);
        out.WriteBits(uint32(oldGuildName.length()), 7);
        out.WriteBit(GuidByte(newGuildGuid, 7) != 0);
        out.FlushBits();

        out.WriteByteSeq(GuidByte(newGuildGuid, 1));
        out << uint32(borderColor);                         // 1
        out.WriteByteSeq(GuidByte(newGuildGuid, 4));
        out.append(inviterName.c_str(), inviterName.size());
        out << uint32(borderStyle);                         // 2
        out.WriteByteSeq(GuidByte(oldGuildGuid, 7));
        out.WriteByteSeq(GuidByte(newGuildGuid, 0));
        out.WriteByteSeq(GuidByte(newGuildGuid, 2));
        out << uint32(emblemColor);                         // 3, paired with 1
        out.WriteByteSeq(GuidByte(oldGuildGuid, 2));
        out.WriteByteSeq(GuidByte(oldGuildGuid, 5));
        out << uint32(guildLevel);                          // 4
        out << uint32(oldGuildRealm);                       // 5
        out.WriteByteSeq(GuidByte(newGuildGuid, 7));
        out.WriteByteSeq(GuidByte(newGuildGuid, 3));
        out.WriteByteSeq(GuidByte(oldGuildGuid, 4));
        out << uint32(backgroundColor);                     // 6
        out.append(newGuildName.c_str(), newGuildName.size());
        out << uint32(newGuildRealm);                       // 7, paired with 9
        out << uint32(emblemStyle);                         // 8
        out.WriteByteSeq(GuidByte(oldGuildGuid, 0));
        out.append(oldGuildName.c_str(), oldGuildName.size());
        out.WriteByteSeq(GuidByte(newGuildGuid, 5));
        out << uint32(inviterRealm);                        // 9, paired with 7
        out.WriteByteSeq(GuidByte(oldGuildGuid, 1));
        out.WriteByteSeq(GuidByte(newGuildGuid, 6));
        out.WriteByteSeq(GuidByte(oldGuildGuid, 3));
        out.WriteByteSeq(GuidByte(oldGuildGuid, 6));
        return true;
    }

    inline bool ReadGuildInvite(WorldPacket& in, std::string& name)
    {
        name.clear();
        if (in.size() - in.rpos() < 2)
            return false;

        // Live 18414 capture: 9-bit byte length followed by the raw player name.
        uint32 const nameLength = in.ReadBits(9);
        if (nameLength != in.size() - in.rpos())
            return false;

        name = in.ReadString(nameLength);
        return in.rpos() == in.size();
    }

    inline bool ReadGuildAchievementTracking(WorldPacket& in,
        std::vector<uint32>& achievementIds)
    {
        achievementIds.clear();
        if (in.size() - in.rpos() < 3)
            return false;

        // Wow.exe 5.4.8.18414 writes a 22-bit MSB-first count, flushes the
        // remaining two bits, then writes each achievement ID as uint32 LE.
        uint32 const count = in.ReadBits(22);
        if (count > 10)
            return false;

        size_t const remaining = in.size() - in.rpos();
        size_t const expected = size_t(count) * sizeof(uint32);
        if (remaining < expected)
            return false;
        if (remaining > expected)
            return false;

        achievementIds.reserve(count);
        for (uint32 i = 0; i < count; ++i)
        {
            uint32 achievementId;
            in >> achievementId; // little-endian uint32
            achievementIds.push_back(achievementId);
        }

        return in.rpos() == in.size();
    }

    inline bool BuildGuildMotd(WorldPacket& out, std::string const& motd)
    {
        if (motd.size() >= (size_t(1) << 10))
            return false;

        out.WriteBits(motd.size(), 10);
        out.FlushBits();
        out.append(motd.data(), motd.size());
        return true;
    }

    inline bool BuildGuildBankText(WorldPacket& out, uint32 tabId,
        std::string text)
    {
        // The 18414 reader copies its 14-bit text length into fixed storage.
        // Enforce the game's 500-character limit here as well as on edits so
        // oversized or malformed database content can never reach that copy.
        utf8truncate(text, 500);
        if (text.size() >= (size_t(1) << 14))
            return false;

        out.Initialize(SMSG_GUILD_BANK_TEXT, 6 + text.size());
        out.WriteBits(text.size(), 14);
        out.FlushBits();
        out << tabId;
        out.WriteStringData(text);
        return true;
    }

    inline bool BuildGuildCommandResult(WorldPacket& out, uint32 command,
        std::string const& name, uint32 result)
    {
        // The direct 18414 reader consumes command, result, an 8-bit byte count,
        // then the raw name. Reject values the on-wire length cannot represent.
        if (name.size() >= (size_t(1) << 8))
            return false;

        out.Initialize(SMSG_GUILD_COMMAND_RESULT, 9 + name.size());
        out << command;
        out << result;
        out.WriteBits(name.size(), 8);
        out.WriteStringData(name);
        return true;
    }

    template <size_t N>
    inline void WriteGuidMask(WorldPacket& out, uint64 guid,
        uint8 const (&order)[N])
    {
        for (uint8 index : order)
            out.WriteBit(GuidByte(guid, index) != 0);
    }

    template <size_t N>
    inline void WriteGuidBytes(WorldPacket& out, uint64 guid,
        uint8 const (&order)[N])
    {
        for (uint8 index : order)
            out.WriteByteSeq(GuidByte(guid, index));
    }

    inline bool FitsGuildEventName(std::string const& name)
    {
        return name.size() < (size_t(1) << 6);
    }

    inline bool BuildGuildMemberJoined(WorldPacket& out, uint64 memberGuid,
        std::string const& memberName, uint32 virtualRealm)
    {
        if (!FitsGuildEventName(memberName))
            return false;

        uint8 const firstMask[] = { 6, 1, 3 };
        uint8 const secondMask[] = { 7, 4, 2, 5, 0 };
        uint8 const firstBytes[] = { 2, 4, 1, 6, 5 };
        uint8 const secondBytes[] = { 3, 0 };
        uint8 const lastByte[] = { 7 };

        out.Initialize(SMSG_GUILD_EVENT_PLAYER_JOINED,
            2 + 8 + 4 + memberName.size());
        WriteGuidMask(out, memberGuid, firstMask);
        out.WriteBits(memberName.size(), 6);
        WriteGuidMask(out, memberGuid, secondMask);
        out.FlushBits();
        WriteGuidBytes(out, memberGuid, firstBytes);
        out << virtualRealm;
        WriteGuidBytes(out, memberGuid, secondBytes);
        out.append(memberName.data(), memberName.size());
        WriteGuidBytes(out, memberGuid, lastByte);
        return true;
    }

    inline bool BuildGuildPresenceChange(WorldPacket& out, uint64 playerGuid,
        std::string const& playerName, uint32 virtualRealm, bool loggedOn,
        bool mobile)
    {
        if (!FitsGuildEventName(playerName))
            return false;

        uint8 const firstMask[] = { 0, 6 };
        uint8 const secondMask[] = { 2, 5, 3 };
        uint8 const thirdMask[] = { 1, 7, 4 };
        uint8 const firstBytes[] = { 3, 2, 0 };
        uint8 const secondBytes[] = { 6 };
        uint8 const thirdBytes[] = { 4, 5, 7, 1 };

        out.Initialize(SMSG_GUILD_EVENT_PRESENCE_CHANGE,
            2 + 8 + 4 + playerName.size());
        WriteGuidMask(out, playerGuid, firstMask);
        out.WriteBit(mobile);
        WriteGuidMask(out, playerGuid, secondMask);
        out.WriteBits(playerName.size(), 6);
        WriteGuidMask(out, playerGuid, thirdMask);
        out.WriteBit(loggedOn);
        out.FlushBits();
        WriteGuidBytes(out, playerGuid, firstBytes);
        out << virtualRealm;
        WriteGuidBytes(out, playerGuid, secondBytes);
        out.append(playerName.data(), playerName.size());
        WriteGuidBytes(out, playerGuid, thirdBytes);
        return true;
    }

    inline void BuildGuildMemberRankUpdate(WorldPacket& out,
        uint64 issuerGuid, uint64 targetGuid, uint32 newRankId, bool promoted)
    {
        uint8 const targetMask1[] = { 5, 6 };
        uint8 const issuerMask1[] = { 0, 1 };
        uint8 const targetMask2[] = { 3 };
        uint8 const issuerMask2[] = { 4 };
        uint8 const targetMask3[] = { 2 };
        uint8 const issuerMask3[] = { 6, 3, 7 };
        uint8 const targetMask4[] = { 4, 0, 1 };
        uint8 const issuerMask4[] = { 2 };
        uint8 const targetMask5[] = { 7 };
        uint8 const issuerMask5[] = { 5 };

        out.Initialize(SMSG_GUILD_RANKS_UPDATE, 3 + 8 + 8 + 4);
        WriteGuidMask(out, targetGuid, targetMask1);
        WriteGuidMask(out, issuerGuid, issuerMask1);
        WriteGuidMask(out, targetGuid, targetMask2);
        WriteGuidMask(out, issuerGuid, issuerMask2);
        WriteGuidMask(out, targetGuid, targetMask3);
        WriteGuidMask(out, issuerGuid, issuerMask3);
        WriteGuidMask(out, targetGuid, targetMask4);
        WriteGuidMask(out, issuerGuid, issuerMask4);
        WriteGuidMask(out, targetGuid, targetMask5);
        out.WriteBit(promoted);
        WriteGuidMask(out, issuerGuid, issuerMask5);
        out.FlushBits();

        out.WriteByteSeq(GuidByte(targetGuid, 2));
        out.WriteByteSeq(GuidByte(issuerGuid, 1));
        out.WriteByteSeq(GuidByte(targetGuid, 6));
        out.WriteByteSeq(GuidByte(targetGuid, 1));
        out.WriteByteSeq(GuidByte(targetGuid, 5));
        out.WriteByteSeq(GuidByte(issuerGuid, 0));
        out << newRankId;
        out.WriteByteSeq(GuidByte(issuerGuid, 3));
        out.WriteByteSeq(GuidByte(issuerGuid, 7));
        out.WriteByteSeq(GuidByte(targetGuid, 7));
        out.WriteByteSeq(GuidByte(issuerGuid, 2));
        out.WriteByteSeq(GuidByte(targetGuid, 3));
        out.WriteByteSeq(GuidByte(targetGuid, 4));
        out.WriteByteSeq(GuidByte(issuerGuid, 6));
        out.WriteByteSeq(GuidByte(issuerGuid, 5));
        out.WriteByteSeq(GuidByte(targetGuid, 0));
        out.WriteByteSeq(GuidByte(issuerGuid, 4));
    }

    inline bool BuildGuildNewLeader(WorldPacket& out, uint64 oldLeaderGuid,
        std::string const& oldLeaderName, uint32 oldLeaderRealm,
        uint64 newLeaderGuid, std::string const& newLeaderName,
        uint32 newLeaderRealm, bool selfPromoted)
    {
        if (!FitsGuildEventName(oldLeaderName) ||
                !FitsGuildEventName(newLeaderName))
            return false;

        out.Initialize(SMSG_GUILD_EVENT_NEW_LEADER,
            4 + 16 + 8 + oldLeaderName.size() + newLeaderName.size());
        uint8 const newMask1[] = { 4, 2, 7 };
        uint8 const oldMask1[] = { 4 };
        uint8 const oldMask2[] = { 0 };
        uint8 const newMask2[] = { 6, 3 };
        uint8 const newMask3[] = { 1, 0 };
        uint8 const oldMask3[] = { 1, 7, 3, 6, 2 };
        uint8 const oldMask4[] = { 5 };
        uint8 const newMask4[] = { 5 };

        WriteGuidMask(out, newLeaderGuid, newMask1);
        WriteGuidMask(out, oldLeaderGuid, oldMask1);
        out.WriteBits(oldLeaderName.size(), 6);
        WriteGuidMask(out, oldLeaderGuid, oldMask2);
        WriteGuidMask(out, newLeaderGuid, newMask2);
        out.WriteBit(selfPromoted);
        WriteGuidMask(out, newLeaderGuid, newMask3);
        WriteGuidMask(out, oldLeaderGuid, oldMask3);
        out.WriteBits(newLeaderName.size(), 6);
        WriteGuidMask(out, oldLeaderGuid, oldMask4);
        WriteGuidMask(out, newLeaderGuid, newMask4);
        out.FlushBits();

        out.WriteByteSeq(GuidByte(newLeaderGuid, 5));
        out.WriteByteSeq(GuidByte(newLeaderGuid, 6));
        out.append(oldLeaderName.data(), oldLeaderName.size());
        out.append(newLeaderName.data(), newLeaderName.size());
        out.WriteByteSeq(GuidByte(newLeaderGuid, 3));
        out.WriteByteSeq(GuidByte(newLeaderGuid, 4));
        out << newLeaderRealm;
        out.WriteByteSeq(GuidByte(oldLeaderGuid, 6));
        out.WriteByteSeq(GuidByte(newLeaderGuid, 0));
        out.WriteByteSeq(GuidByte(oldLeaderGuid, 5));
        out.WriteByteSeq(GuidByte(newLeaderGuid, 2));
        out.WriteByteSeq(GuidByte(newLeaderGuid, 7));
        out.WriteByteSeq(GuidByte(oldLeaderGuid, 7));
        out.WriteByteSeq(GuidByte(oldLeaderGuid, 4));
        out << oldLeaderRealm;
        out.WriteByteSeq(GuidByte(newLeaderGuid, 1));
        out.WriteByteSeq(GuidByte(oldLeaderGuid, 2));
        out.WriteByteSeq(GuidByte(oldLeaderGuid, 1));
        out.WriteByteSeq(GuidByte(oldLeaderGuid, 3));
        out.WriteByteSeq(GuidByte(oldLeaderGuid, 0));
        return true;
    }

    inline void BuildGuildDisbanded(WorldPacket& out)
    {
        out.Initialize(SMSG_GUILD_EVENT_DISBANDED, 0);
    }

    inline bool BuildGuildPlayerLeft(WorldPacket& out, uint64 leaverGuid,
        std::string const& leaverName, uint32 leaverRealm,
        bool removedByMember, uint64 removerGuid,
        std::string const& removerName, uint32 removerRealm)
    {
        if (!FitsGuildEventName(leaverName) ||
                (removedByMember &&
                    (!removerGuid || !FitsGuildEventName(removerName))) ||
                (!removedByMember &&
                    (removerGuid || !removerName.empty() || removerRealm)))
            return false;

        bool const hasRemover = removedByMember;
        out.Initialize(SMSG_GUILD_EVENT_PLAYER_LEFT,
            4 + 8 + 4 + leaverName.size() +
            (hasRemover ? 8 + 4 + removerName.size() : 0));

        out.WriteBit(GuidByte(leaverGuid, 2) != 0);
        out.WriteBits(leaverName.size(), 6);
        uint8 const leaverMask1[] = { 6, 5 };
        WriteGuidMask(out, leaverGuid, leaverMask1);
        out.WriteBit(hasRemover);
        if (hasRemover)
        {
            out.WriteBit(false);                           // remover name present
            out.WriteBit(false);                           // removed, not self-leave
            out.WriteBits(removerName.size(), 6);
            uint8 const removerMask[] = { 1, 3, 4, 2, 5, 7, 6, 0 };
            WriteGuidMask(out, removerGuid, removerMask);
            out.WriteBit(false);                           // remover realm present
        }
        uint8 const leaverMask2[] = { 1, 0, 3, 4, 7 };
        WriteGuidMask(out, leaverGuid, leaverMask2);
        out.FlushBits();

        if (hasRemover)
        {
            uint8 const removerBytes[] = { 1, 3, 5, 2, 0, 4, 6, 7 };
            WriteGuidBytes(out, removerGuid, removerBytes);
            out.append(removerName.data(), removerName.size());
            out << removerRealm;
        }
        out.append(leaverName.data(), leaverName.size());
        out.WriteByteSeq(GuidByte(leaverGuid, 1));
        out << leaverRealm;
        uint8 const leaverBytes[] = { 0, 4, 2, 3, 6, 5, 7 };
        WriteGuidBytes(out, leaverGuid, leaverBytes);
        return true;
    }

    /**
     * @brief Parses CMSG_GUILD_SET_NOTE, from writer sub_C85C5A.
     *
     * Lifted out of the handler so it can be run over real captured bodies. The
     * shipped reader was wrong three ways at once -- bit order, the position of
     * the note length, and the byte order -- and a test that merely restated the
     * new derivation would have caught none of that. Driving THIS function with
     * captured bytes does.
     *
     * The note length is a full 8 bits and sits immediately after the first mask
     * bit; the public/officer flag sits inside the mask run; and the note itself
     * goes out in the MIDDLE of the byte run. A set flag means the PUBLIC note --
     * established from the client's two builders, GuildRosterSetPublicNote
     * passing 1 and GuildRosterSetOfficerNote passing 0, and corroborated by the
     * captures, whose notes read "DPS 571" and "Resto 570 IL".
     */
    /**
     * @brief One SMSG_GUILD_EVENT_LOG entry, from client reader sub_6A6843.
     *
     * Inline so the layout can be driven by a test without linking the database.
     * The elapsed time is a parameter rather than computed here for the same
     * reason -- time(NULL) cannot be pinned in a fixture.
     *
     * eventType goes to record +20 and newRank to +21. That is not the order
     * position suggests, and it was wrong here once: the consumer settles it,
     * since GetGuildEventInfo switches on the field from +20 to yield
     * invite/join/promote/demote/remove/quit and passes the one from +21 to the
     * rank-name lookup sub_966826.
     */
    inline void BuildGuildEventLogEntry(WorldPacket& out, ByteBuffer& buffer,
        uint8 eventType, uint64 guid1, uint64 guid2, uint8 newRank, uint32 elapsed)
    {
        uint8 const mask1[] = { 6, 1 };
        WriteGuidMask(out, guid1, mask1);
        uint8 const mask2[] = { 5, 1, 3, 0, 4 };
        WriteGuidMask(out, guid2, mask2);
        uint8 const mask3[] = { 4 };
        WriteGuidMask(out, guid1, mask3);
        uint8 const mask4[] = { 7 };
        WriteGuidMask(out, guid2, mask4);
        uint8 const mask5[] = { 0, 2, 7, 3, 5 };
        WriteGuidMask(out, guid1, mask5);
        uint8 const mask6[] = { 2, 6 };
        WriteGuidMask(out, guid2, mask6);

        uint8 const b1[] = { 5, 4 };
        for (uint8 index : b1)
            buffer.WriteByteSeq(GuidByte(guid1, index));
        uint8 const b2[] = { 6 };
        for (uint8 index : b2)
            buffer.WriteByteSeq(GuidByte(guid2, index));
        uint8 const b3[] = { 2 };
        for (uint8 index : b3)
            buffer.WriteByteSeq(GuidByte(guid1, index));
        uint8 const b4[] = { 4 };
        for (uint8 index : b4)
            buffer.WriteByteSeq(GuidByte(guid2, index));
        buffer << uint8(eventType);                        // +20
        uint8 const b5[] = { 0 };
        for (uint8 index : b5)
            buffer.WriteByteSeq(GuidByte(guid2, index));
        uint8 const b6[] = { 7, 3 };
        for (uint8 index : b6)
            buffer.WriteByteSeq(GuidByte(guid1, index));
        uint8 const b7[] = { 5, 2 };
        for (uint8 index : b7)
            buffer.WriteByteSeq(GuidByte(guid2, index));
        uint8 const b8[] = { 0 };
        for (uint8 index : b8)
            buffer.WriteByteSeq(GuidByte(guid1, index));
        buffer << uint32(elapsed);                         // +16
        uint8 const b9[] = { 1, 6 };
        for (uint8 index : b9)
            buffer.WriteByteSeq(GuidByte(guid1, index));
        uint8 const b10[] = { 7, 1 };
        for (uint8 index : b10)
            buffer.WriteByteSeq(GuidByte(guid2, index));
        buffer << uint8(newRank);                          // +21
        uint8 const b11[] = { 3 };
        for (uint8 index : b11)
            buffer.WriteByteSeq(GuidByte(guid2, index));
    }

    inline void ParseGuildSetNote(WorldPacket& in, ObjectGuid& targetGuid,
                                  bool& isPublic, std::string& note)
    {
        in.ReadGuidMask<1>(targetGuid);
        uint32 const noteLen = in.ReadBits(8);
        in.ReadGuidMask<4, 2>(targetGuid);
        isPublic = in.ReadBit();
        in.ReadGuidMask<3, 5, 0, 6, 7>(targetGuid);

        in.ReadGuidBytes<5, 1, 6>(targetGuid);
        note = in.ReadString(noteLen);
        in.ReadGuidBytes<0, 7, 4, 3, 2>(targetGuid);
    }
}

class Item;

#define GUILD_RANK_NONE         0xFF

enum GuildDefaultRanks
{
    // these ranks can be modified, but they can not be deleted
    GR_GUILDMASTER  = 0,
    GR_OFFICER      = 1,
    GR_VETERAN      = 2,
    GR_MEMBER       = 3,
    GR_INITIATE     = 4,
    // When promoting member server does: rank--;!
    // When demoting member server does: rank++;!
};

// The client clamps both the withdraw-gold limit and the per-tab item-withdraw
// count to this before sending (sub_964EE1 and sub_964F15 in the 18414 client),
// so a larger value cannot have come from the stock UI.
#define GUILD_WITHDRAW_MONEY_CLIENT_MAX  100000
#define GUILD_WITHDRAW_SLOTS_CLIENT_MAX  100000

enum GuildRankRights
{
    GR_RIGHT_EMPTY              = 0x00000040,
    GR_RIGHT_GCHATLISTEN        = 0x00000041,
    GR_RIGHT_GCHATSPEAK         = 0x00000042,
    GR_RIGHT_OFFCHATLISTEN      = 0x00000044,
    GR_RIGHT_OFFCHATSPEAK       = 0x00000048,
    GR_RIGHT_PROMOTE            = 0x000000C0,
    GR_RIGHT_DEMOTE             = 0x00000140,
    GR_RIGHT_INVITE             = 0x00000050,
    GR_RIGHT_REMOVE             = 0x00000060,
    GR_RIGHT_SETMOTD            = 0x00001040,
    GR_RIGHT_EPNOTE             = 0x00002040,
    GR_RIGHT_VIEWOFFNOTE        = 0x00004040,
    GR_RIGHT_EOFFNOTE           = 0x00008040,
    GR_RIGHT_MODIFY_GUILD_INFO  = 0x00010040,
    GR_RIGHT_WITHDRAW_GOLD_LOCK = 0x00020000,               // remove money withdraw capacity
    GR_RIGHT_WITHDRAW_REPAIR    = 0x00040000,               // withdraw for repair
    GR_RIGHT_WITHDRAW_GOLD      = 0x00080000,               // withdraw gold
    GR_RIGHT_CREATE_GUILD_EVENT = 0x00100000,               // wotlk
    GR_RIGHT_REQUIRES_AUTHENTICATOR = 0x00200000,
    GR_RIGHT_MODIFY_BANK_TABS       = 0x00400000,               // cata?
    GR_RIGHT_REMOVE_GUILD_EVENT     = 0x00800000,               // wotlk
    GR_RIGHT_ALL                    = 0x00DDF1FF,
};

enum Typecommand
{
    GUILD_CREATE_S  = 0x00,
    GUILD_INVITE_S  = 0x01,
    GUILD_QUIT_S    = 0x03,
    // 0x05?
    GUILD_FOUNDER_S = 0x0E,
    GUILD_UNK1      = 0x14,
    GUILD_UNK2      = 0x15,
};

enum CommandErrors
{
    ERR_PLAYER_NO_MORE_IN_GUILD     = 0x00, // no message/error
    ERR_GUILD_INTERNAL              = 0x01,
    ERR_ALREADY_IN_GUILD            = 0x02,
    ERR_ALREADY_IN_GUILD_S          = 0x03,
    ERR_INVITED_TO_GUILD            = 0x04,
    ERR_ALREADY_INVITED_TO_GUILD_S  = 0x05,
    ERR_GUILD_NAME_INVALID          = 0x06,
    ERR_GUILD_NAME_EXISTS_S         = 0x07,
    ERR_GUILD_LEADER_LEAVE          = 0x08, // for Typecommand 0x03
    ERR_GUILD_PERMISSIONS           = 0x08, // for another Typecommand
    ERR_GUILD_PLAYER_NOT_IN_GUILD   = 0x09,
    ERR_GUILD_PLAYER_NOT_IN_GUILD_S = 0x0A,
    ERR_GUILD_PLAYER_NOT_FOUND_S    = 0x0B,
    ERR_GUILD_NOT_ALLIED            = 0x0C,
    ERR_GUILD_RANK_TOO_HIGH_S       = 0x0D,
    ERR_GUILD_RANK_TOO_LOW_S        = 0x0E,
    ERR_GUILD_RANKS_LOCKED          = 0x11,
    ERR_GUILD_RANK_IN_USE           = 0x12,
    ERR_GUILD_IGNORING_YOU_S        = 0x13,
    ERR_GUILD_UNK1                  = 0x14,
    ERR_GUILD_WITHDRAW_LIMIT        = 0x19,
    ERR_GUILD_NOT_ENOUGH_MONEY      = 0x1A,
    ERR_GUILD_BANK_FULL             = 0x1C,
    ERR_GUILD_ITEM_NOT_FOUND            = 0x1D,
    ERR_GUILD_TOO_MUCH_MONEY            = 0x1F,
    ERR_GUILD_BANK_WRONG_TAB            = 0x20,
    ERR_RANK_REQUIRES_AUTHENTICATOR     = 0x22,
    ERR_GUILD_BANK_VOUCHER_FAILED       = 0x23,
    ERR_GUILD_TRIAL_ACCOUNT             = 0x24,
    ERR_GUILD_UNDELETABLE_DUE_TO_LEVEL  = 0x25,
    ERR_GUILD_MOVE_STARTING             = 0x26,
    ERR_GUILD_REP_TOO_LOW               = 0x27,
};

enum PetitionTurns
{
    PETITION_TURN_OK                    = 0,
    PETITION_TURN_ALREADY_IN_GUILD      = 2,
    PETITION_TURN_NEED_MORE_SIGNATURES  = 4,
    PETITION_TURN_GUILD_PERMISSIONS     = 11,
    PETITION_TURN_GUILD_NAME_INVALID    = 12,
};

enum PetitionSigns
{
    PETITION_SIGN_OK                = 0,
    PETITION_SIGN_ALREADY_SIGNED    = 1,
    PETITION_SIGN_ALREADY_IN_GUILD  = 2,
    PETITION_SIGN_CANT_SIGN_OWN     = 3,
    PETITION_SIGN_NOT_SAME_SERVER       = 5,
    PETITION_SIGN_PETITION_FULL         = 8,
    PETITION_SIGN_ALREADY_SIGNED_OTHER  = 10,
    PETITION_SIGN_RESTRICTED_ACCOUNT    = 11,
};

enum GuildBankRights
{
    GUILD_BANK_RIGHT_VIEW_TAB       = 0x01,
    GUILD_BANK_RIGHT_PUT_ITEM       = 0x02,
    GUILD_BANK_RIGHT_UPDATE_TEXT    = 0x04,

    GUILD_BANK_RIGHT_DEPOSIT_ITEM   = GUILD_BANK_RIGHT_VIEW_TAB | GUILD_BANK_RIGHT_PUT_ITEM,
    GUILD_BANK_RIGHT_FULL           = 0xFF,
};

enum GuildBankEventLogTypes
{
    GUILD_BANK_LOG_DEPOSIT_ITEM     = 1,
    GUILD_BANK_LOG_WITHDRAW_ITEM    = 2,
    GUILD_BANK_LOG_MOVE_ITEM        = 3,
    GUILD_BANK_LOG_DEPOSIT_MONEY    = 4,
    GUILD_BANK_LOG_WITHDRAW_MONEY   = 5,
    GUILD_BANK_LOG_REPAIR_MONEY     = 6,
    GUILD_BANK_LOG_MOVE_ITEM2       = 7,
    GUILD_BANK_LOG_UNK1             = 8,
    GUILD_BANK_LOG_BUY_SLOT             = 9,
    GUILD_BANK_LOG_CASH_FLOW_DEPOSIT    = 10,
};

enum GuildEventLogTypes
{
    GUILD_EVENT_LOG_INVITE_PLAYER     = 1,
    GUILD_EVENT_LOG_JOIN_GUILD        = 2,
    GUILD_EVENT_LOG_PROMOTE_PLAYER    = 3,
    GUILD_EVENT_LOG_DEMOTE_PLAYER     = 4,
    GUILD_EVENT_LOG_UNINVITE_PLAYER   = 5,
    GUILD_EVENT_LOG_LEAVE_GUILD       = 6,
};

enum GuildEmblem
{
    ERR_GUILDEMBLEM_SUCCESS               = 0,
    ERR_GUILDEMBLEM_INVALID_TABARD_COLORS = 1,
    ERR_GUILDEMBLEM_NOGUILD               = 2,
    ERR_GUILDEMBLEM_NOTGUILDMASTER        = 3,
    ERR_GUILDEMBLEM_NOTENOUGHMONEY        = 4,
    ERR_GUILDEMBLEM_INVALIDVENDOR         = 5
};

enum GuildMemberFlags
{
    GUILDMEMBER_STATUS_NONE      = 0x0000,
    GUILDMEMBER_STATUS_ONLINE    = 0x0001,
    GUILDMEMBER_STATUS_AFK       = 0x0002,
    GUILDMEMBER_STATUS_DND       = 0x0004,
    GUILDMEMBER_STATUS_MOBILE    = 0x0008,
};

inline uint64 GetGuildBankTabPrice(uint8 Index)
{
    switch (Index)
    {
        case 0: return 100;
        case 1: return 250;
        case 2: return 500;
        case 3: return 1000;
        case 4: return 2500;
        case 5: return 5000;
        default:
            return 0;
    }
}

struct GuildEventLogEntry
{
    uint8  EventType;
    uint32 PlayerGuid1;
    uint32 PlayerGuid2;
    uint8  NewRank;
    uint64 TimeStamp;

    void WriteData(WorldPacket& data, ByteBuffer& buffer);
};

struct GuildBankEventLogEntry
{
    uint8  EventType;
    uint32 PlayerGuid;
    uint32 ItemOrMoney;
    uint8  ItemStackCount;
    uint8  DestTabId;
    uint64 TimeStamp;

    bool isMoneyEvent() const
    {
        return EventType == GUILD_BANK_LOG_DEPOSIT_MONEY ||
               EventType == GUILD_BANK_LOG_WITHDRAW_MONEY ||
               EventType == GUILD_BANK_LOG_REPAIR_MONEY;
    }

    void WriteData(WorldPacket& data, ByteBuffer& buffer);
};

struct GuildBankTab
{
    GuildBankTab() { memset(Slots, 0, GUILD_BANK_MAX_SLOTS * sizeof(Item*)); }

    Item* Slots[GUILD_BANK_MAX_SLOTS];
    std::string Name;
    std::string Icon;
    std::string Text;
};

struct GuildItemPosCount
{
    GuildItemPosCount(uint8 _slot, uint32 _count) : Slot(_slot), Count(_count) {}

    bool isContainedIn(std::vector<GuildItemPosCount> const& vec) const;

    uint8 Slot;
    uint32 Count;
};
typedef std::vector<GuildItemPosCount> GuildItemPosCountVec;

struct MemberSlot
{
    void SetMemberStats(Player* player);
    void UpdateLogoutTime();
    void SetPNOTE(std::string pnote);
    void SetOFFNOTE(std::string offnote);
    void ChangeRank(uint32 newRank);

    ObjectGuid guid;
    uint32 accountId;
    std::string Name;
    uint32 RankId;
    uint8 Level;
    uint8 Class;
    uint32 ZoneId;
    uint64 LogoutTime;
    std::string Pnote;
    std::string OFFnote;
    uint32 BankResetTimeMoney;
    uint32 BankRemMoney;
    uint32 BankResetTimeTab[GUILD_BANK_MAX_TABS];
    uint32 BankRemSlotsTab[GUILD_BANK_MAX_TABS];
};

struct RankInfo
{
    RankInfo(const std::string& _name, uint32 _rights, uint32 _money) : Name(_name), Rights(_rights), BankMoneyPerDay(_money)
    {
        for (uint8 i = 0; i < GUILD_BANK_MAX_TABS; ++i)
        {
            TabRight[i] = 0;
            TabSlotPerDay[i] = 0;
        }
    }

    std::string Name;
    uint32 Rights;
    uint32 BankMoneyPerDay;
    uint32 TabRight[GUILD_BANK_MAX_TABS];
    uint32 TabSlotPerDay[GUILD_BANK_MAX_TABS];
};

class Guild
{
    public:
        Guild();
        ~Guild();

        bool Create(Player* leader, std::string gname);
        void CreateDefaultGuildRanks(int locale_idx);
        void Disband();

        void DeleteGuildBankItems(bool alsoInDB = false);
        typedef std::unordered_map<uint32, MemberSlot> MemberList;
        typedef std::vector<RankInfo> RankList;

        uint32 GetId() const { return m_Id; }
        uint32 GetLevel() const { return m_Level; }
        ObjectGuid GetObjectGuid() const { return ObjectGuid(HIGHGUID_GUILD, 0, m_Id); }
        ObjectGuid GetLeaderGuid() const { return m_LeaderGuid; }
        std::string const& GetName() const { return m_Name; }
        std::string const& GetMOTD() const { return MOTD; }
        std::string const& GetGINFO() const { return GINFO; }

        time_t GetCreatedDate() const { return m_CreatedDate; }

        uint32 GetEmblemStyle() const { return m_EmblemStyle; }
        uint32 GetEmblemColor() const { return m_EmblemColor; }
        uint32 GetBorderStyle() const { return m_BorderStyle; }
        uint32 GetBorderColor() const { return m_BorderColor; }
        uint32 GetBackgroundColor() const { return m_BackgroundColor; }

        void SetLeader(ObjectGuid guid);
        bool AddMember(ObjectGuid plGuid, uint32 plRank);
        bool DelMember(ObjectGuid guid, bool isDisbanding = false);
        bool ChangeMemberRank(ObjectGuid guid, uint8 newRank);
        // lowest rank is the count of ranks - 1 (the highest rank_id in table)
        uint32 GetLowestRank() const { return m_Ranks.size() - 1; }

        void SetMOTD(std::string motd);
        void SetGINFO(std::string ginfo);
        void SetEmblem(uint32 emblemStyle, uint32 emblemColor, uint32 borderStyle, uint32 borderColor, uint32 backgroundColor);

        uint32 GetMemberSize() const { return members.size(); }
        uint32 GetAccountsNumber();

        bool LoadGuildFromDB(QueryResult* guildDataResult);
        bool CheckGuildStructure();
        bool LoadRanksFromDB(QueryResult* guildRanksResult);
        bool LoadMembersFromDB(QueryResult* guildMembersResult);

        void BroadcastToGuild(WorldSession* session, const std::string& msg, uint32 language = LANG_UNIVERSAL);
        void BroadcastAddonToGuild(WorldSession* session, const std::string& msg, const std::string& prefix);
        void BroadcastToOfficers(WorldSession* session, const std::string& msg, uint32 language = LANG_UNIVERSAL);
        void BroadcastAddonToOfficers(WorldSession* session, const std::string& msg, const std::string& prefix);
        void BroadcastPacketToRank(WorldPacket* packet, uint32 rankId);
        void BroadcastRankDefinitions();
        void BroadcastPacket(WorldPacket* packet);
        // for calendar
        void MassInviteToEvent(WorldSession* session, uint32 minLevel, uint32 maxLevel, uint32 minRank);

        void BroadcastMotd(std::string const& motd);
        void BroadcastMemberJoined(ObjectGuid guid, std::string const& name);
        void BroadcastMemberPresence(ObjectGuid guid, std::string const& name, bool loggedOn);
        void BroadcastMemberRankUpdate(ObjectGuid issuerGuid, ObjectGuid targetGuid,
            uint32 newRankId, bool promoted);
        void BroadcastNewLeader(ObjectGuid oldLeaderGuid, std::string const& oldLeaderName,
            ObjectGuid newLeaderGuid, std::string const& newLeaderName, bool selfPromoted = false);
        void BroadcastMemberLeft(ObjectGuid guid, std::string const& name);
        void BroadcastMemberRemoved(ObjectGuid guid, std::string const& name,
            ObjectGuid removerGuid, std::string const& removerName);
        void BroadcastDisbanded();

        template<class Do>
        void BroadcastWorker(Do& _do, Player* except = NULL)
        {
            for (MemberList::iterator itr = members.begin(); itr != members.end(); ++itr)
                if (Player* player = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, itr->first)))
                    if (player != except)
                    {
                        _do(player);
                    }
        }

        void CreateRank(std::string name, uint32 rights);
        bool DelRank(uint32 rankId);
        bool SwitchRank(uint32 rankId, bool up);
        std::string GetRankName(uint32 rankId);
        uint32 GetRankRights(uint32 rankId);
        uint32 GetRanksSize() const { return m_Ranks.size(); }

        void SetRankName(uint32 rankId, std::string name);
        void SetRankRights(uint32 rankId, uint32 rights);
        // GR_RIGHT_EMPTY is a baseline bit OR'd into the low constants, not a
        // right in itself: the client's own rank-flag table (dword_F654C0 in the
        // 18414 client, 20 entries of stride 8, read by GuildControlSetRankFlag)
        // carries the distinguishing bit alone and never bit 6. So the
        // meaningful part of any constant is what remains once the baseline is
        // masked off, and the rank must carry all of it.
        //
        // The former test compared the AND against GR_RIGHT_EMPTY, so it was
        // false only when the result was exactly 0x40. That failed open twice
        // over: the seven constants that carry no baseline bit can only AND to 0
        // or to themselves, never to 0x40, so they were granted to every rank
        // unconditionally; and any stored mask lacking bit 6 ANDed to 0 against
        // the low constants, which also read as granted.
        //
        // What that actually let through: nothing, yet. All three rights that
        // both fail open and have a caller -- WITHDRAW_GOLD at
        // HandleGuildBankWithdrawMoney, MODIFY_BANK_TABS at
        // HandleGuildBankUpdateTab, WITHDRAW_REPAIR at PlayerDurability -- sit
        // behind opcodes that are still dormant, so no client could reach any of
        // them. The predicate was wrong regardless and is worth fixing before
        // those opcodes register, not after; do not read this as the hole having
        // been exploitable.
        bool HasRankRight(uint32 rankId, uint32 right)
        {
            if (rankId >= m_Ranks.size())
            {
                return false;
            }

            uint32 const required = right & ~uint32(GR_RIGHT_EMPTY);
            return required != 0 && (GetRankRights(rankId) & required) == required;
        }

        bool HasMembersWithRank(uint32 rankId) const
        {
            for (MemberList::const_iterator itr = members.begin(); itr != members.end(); ++itr)
                if (itr->second.RankId == rankId)
                {
                    return true;
                }

            return false;
        }

        int32 GetRank(ObjectGuid guid)
        {
            MemberSlot* slot = GetMemberSlot(guid);
            return slot ? slot->RankId : -1;
        }

        MemberSlot* GetMemberSlot(ObjectGuid guid)
        {
            MemberList::iterator itr = members.find(guid.GetCounter());
            return itr != members.end() ? &itr->second : NULL;
        }

        MemberSlot* GetMemberSlot(const std::string& name)
        {
            for (MemberList::iterator itr = members.begin(); itr != members.end(); ++itr)
                if (itr->second.Name == name)
                {
                    return &itr->second;
                }

            return NULL;
        }

        void Roster(WorldSession* session = NULL);          // NULL = broadcast
        void Query(WorldSession* session);
        void QueryRanks(WorldSession* session);

        // Guild EventLog
        void   LoadGuildEventLogFromDB();
        void   DisplayGuildEventLog(WorldSession* session);
        void   LogGuildEvent(uint8 EventType, ObjectGuid playerGuid1, ObjectGuid playerGuid2 = ObjectGuid(), uint8 newRank = 0);

        // ** Guild bank **
        // Content & item deposit/withdraw
        void   DisplayGuildBankContent(WorldSession* session, uint8 TabId,
                    bool sendAllSlots = true, bool withTabInfo = false);

        void   SwapItems(Player* pl, uint8 BankTab, uint8 BankTabSlot, uint8 BankTabDst, uint8 BankTabSlotDst, uint32 SplitedAmount);
        void   MoveFromBankToChar(Player* pl, uint8 BankTab, uint8 BankTabSlot, uint8 PlayerBag, uint8 PlayerSlot, uint32 SplitedAmount);
        void   MoveFromCharToBank(Player* pl, uint8 PlayerBag, uint8 PlayerSlot, uint8 BankTab, uint8 BankTabSlot, uint32 SplitedAmount);

        // Everything a client needs before it can show the guild it belongs to.
        // Membership itself travels as object fields, which say WHICH guild but
        // nothing about it, so this has to be sent on joining as well as on login.
        void   SendGuildStateTo(WorldSession* session);

        // Tabs
        void   DisplayGuildBankTabsInfo(WorldSession* session, uint8 TabId = 0);
        /// Queues its rows into the caller's transaction and does not commit.
        void   CreateNewBankTab();
        void   SetGuildBankTabText(uint8 TabId, std::string text);
        void   SendGuildBankTabText(WorldSession* session, uint8 TabId);
        void   SetGuildBankTabInfo(uint8 TabId, std::string name, std::string icon);
        uint8  GetPurchasedTabs() const { return m_TabListMap.size(); }
        uint32 GetBankRights(uint32 rankId, uint8 TabId) const;
        bool   IsMemberHaveRights(uint32 LowGuid, uint8 TabId, uint32 rights) const;
        bool   CanMemberViewTab(uint32 LowGuid, uint8 TabId) const;
        // Load
        void   LoadGuildBankFromDB();
        // Money deposit/withdraw
        void   SendMoneyInfo(WorldSession* session, uint32 LowGuid);
        bool   MemberMoneyWithdraw(uint64 amount, uint32 LowGuid);
        uint64 GetGuildBankMoney() { return m_GuildBankMoney; }
        void   SetBankMoney(int64 money);
        /// Memory only, no database write. For the recovery path after an
        /// ambiguous commit, where the value has just been read back from the
        /// database and writing it out again would be a pointless round trip
        /// that could itself fail. Everywhere else wants SetBankMoney.
        void   AdoptBankMoneyFromDB(uint64 money) { m_GuildBankMoney = money; }

        /// Same, for one member's remaining daily withdrawal. A withdraw mutates
        /// three balances, not two: the bank total, the player's money and this.
        void   AdoptMemberRemainingWithdrawFromDB(uint32 lowGuid, uint32 remaining)
        {
            MemberList::iterator itr = members.find(lowGuid);
            if (itr != members.end())
            {
                itr->second.BankRemMoney = remaining;
            }
        }

        /// Set when an ambiguous commit leaves the in-memory bank state possibly
        /// disagreeing with the rows and the rows could not be re-read. It covers
        /// the bank TOTAL and the purchased-tab list, not money alone.
        ///
        /// Money, because the next deposit would otherwise compute a new total
        /// from an untrusted one and write THAT durably, minting or burning the
        /// difference on behalf of a different player. Tabs, because a purchase
        /// whose commit could not be confirmed may leave a tab in m_TabListMap
        /// that has no guild_bank_tab row: an item stored into it would write a
        /// guild_bank_item row whose TabId no reload can match, and
        /// LoadGuildBankFromDB drops exactly those rows -- the item would be gone
        /// for good. Only a reload of the guild clears the flag.
        /// True when the named bank slot still holds the item entry the client
        /// said it did -- zero meaning "empty". A guild bank is shared, so a
        /// request can arrive after another member has changed the slot under
        /// it; applying it anyway acts on whatever is there now. Slot 0xFF is
        /// the client's "anywhere in this tab" and names nothing to compare.
        bool   BankSlotHoldsEntry(uint8 tabId, uint8 slotId, uint32 expectedEntry);

        /// True when the named bank slot still holds the stack SIZE the client
        /// said it did. Checking the entry alone is not enough for a request
        /// that asks for a whole stack: the entry can match while the stack has
        /// grown underneath it. Slot 0xFF names nothing to compare.
        bool   BankSlotStackCountIs(uint8 tabId, uint8 slotId, uint32 expectedCount);

        bool   IsBankStateTrusted() const { return m_bankStateTrusted; }
        void   MarkBankStateUntrusted() { m_bankStateTrusted = false; }

        /// Commits an item mutation SYNCHRONOUSLY and reports whether the
        /// database accepted it -- CommitTransactionDirect, never the queuing
        /// CommitTransaction, which returns true before MySQL has seen anything.
        /// A false return means memory and the durable rows may now disagree: it
        /// marks the bank untrusted AND quarantines the player's session, and
        /// every caller must abandon the operation without broadcasting. See the
        /// commentary on the definition in GuildBank.cpp.
        bool   CommitBankMutation(Player* pl, char const* context);
        // per days
        bool   MemberItemWithdraw(uint8 TabId, uint32 LowGuid);
        uint32 GetMemberSlotWithdrawRem(uint32 LowGuid, uint8 TabId);
        uint64 GetMemberMoneyWithdrawRem(uint32 LowGuid);
        void   SetBankMoneyPerDay(uint32 rankId, uint32 money);
        void   SetBankRightsAndSlots(uint32 rankId, uint8 TabId, uint32 right, uint32 SlotPerDay, bool db);
        uint32 GetBankMoneyPerDay(uint32 rankId);
        uint32 GetBankSlotPerDay(uint32 rankId, uint8 TabId);
        // rights per day
        bool   LoadBankRightsFromDB(QueryResult* guildBankTabRightsResult);
        // Guild Bank Event Logs
        void   LoadGuildBankEventLogFromDB();
        void   DisplayGuildBankLogs(WorldSession* session, uint8 TabId);
        void   LogBankEvent(uint8 EventType, uint8 TabId, uint32 PlayerGuidLow, uint32 ItemOrMoney, uint8 ItemStackCount = 0, uint8 DestTabId = 0);
        bool   AddGBankItemToDB(uint32 GuildId, uint32 BankTab , uint32 BankTabSlot , uint32 GUIDLow, uint32 Entry);

    protected:
        void AddRank(const std::string& name, uint32 rights, uint32 money);

        uint32 m_Id;
        uint32 m_Level;
        std::string m_Name;
        ObjectGuid m_LeaderGuid;
        std::string MOTD;
        std::string GINFO;
        time_t m_CreatedDate;

        uint32 m_EmblemStyle;
        uint32 m_EmblemColor;
        uint32 m_BorderStyle;
        uint32 m_BorderColor;
        uint32 m_BackgroundColor;
        uint32 m_accountsNumber;                            // 0 used as marker for need lazy calculation at request

        RankList m_Ranks;

        MemberList members;

        typedef std::vector<GuildBankTab*> TabListMap;
        TabListMap m_TabListMap;

        /** These are actually ordered lists. The first element is the oldest entry.*/
        typedef std::list<GuildEventLogEntry> GuildEventLog;
        typedef std::list<GuildBankEventLogEntry> GuildBankEventLog;
        GuildEventLog m_GuildEventLog;
        GuildBankEventLog m_GuildBankEventLog_Money;
        GuildBankEventLog m_GuildBankEventLog_Item[GUILD_BANK_MAX_TABS];

        uint32 m_GuildEventLogNextGuid;
        uint32 m_GuildBankEventLogNextGuid_Money;
        uint32 m_GuildBankEventLogNextGuid_Item[GUILD_BANK_MAX_TABS];

        uint64 m_GuildBankMoney;
        bool   m_bankStateTrusted = true;

    private:
        void UpdateAccountsNumber() { m_accountsNumber = 0;}// mark for lazy calculation at request in GetAccountsNumber
        void _ChangeRank(ObjectGuid guid, MemberSlot* slot, uint32 newRank);

        // used only from high level Swap/Move functions
        Item*  GetItem(uint8 TabId, uint8 SlotId);
        InventoryResult CanStoreItem(uint8 tab, uint8 slot, GuildItemPosCountVec& dest, uint32 count, Item* pItem, bool swap = false) const;
        Item*  StoreItem(uint8 tab, GuildItemPosCountVec const& pos, Item* pItem);
        void   RemoveItem(uint8 tab, uint8 slot);
        void   DisplayGuildBankContentUpdate(uint8 TabId, int32 slot1, int32 slot2 = -1);
        void   DisplayGuildBankContentUpdate(uint8 TabId, GuildItemPosCountVec const& slots);

        // internal common parts for CanStore/StoreItem functions
        bool AppendDisplayGuildBankSlot(MopGuildBankPackets::GuildBankList& list,
            GuildBankTab const* tab, int32 slot) const;
        InventoryResult _CanStoreItem_InSpecificSlot(uint8 tab, uint8 slot, GuildItemPosCountVec& dest, uint32& count, bool swap, Item* pSrcItem) const;
        InventoryResult _CanStoreItem_InTab(uint8 tab, GuildItemPosCountVec& dest, uint32& count, bool merge, Item* pSrcItem, uint8 skip_slot) const;
        Item* _StoreItem(uint8 tab, uint8 slot, Item* pItem, uint32 count, bool clone);
};
#endif
