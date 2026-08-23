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
 * @file Unit.h
 * @brief Unit (player, creature) base class definition and related structures.
 *
 * This file defines the Unit class which is the base class for all combatant entities
 * in the game world including players, creatures, pets, and other combat-capable objects.
 *
 * Key functionality includes:
 * - Health, mana, and resource management
 * - Combat and threat management
 * - Spell casting and interruption
 * - Aura and spell effect application
 * - Damage calculation and resistance
 * - Crowd control effects (stuns, slows, roots)
 * - Movement and pathfinding control
 * - Combat AI state machines
 * - Summon and pet mechanics
 * - In-combat event broadcasting
 * - Stat calculation and modification
 * - Power regeneration systems
 *
 * Derived classes include:
 * - Creature: Non-player characters with AI
 * - Player: Playable character with inventory and skills
 * - Pet: Player-controlled pets and minions
 *
 * @see Unit for the main unit class
 * @see Player for player-specific implementation
 * @see Creature for NPC-specific implementation
 * @see Pet for summoned pet implementation
 */

#ifndef MANGOS_H_UNIT
#define MANGOS_H_UNIT

#include "Common.h"
#include "Object.h"
#include "Opcodes.h"
#include "SpellAuraDefines.h"
#include "UpdateFields.h"
#include "SharedDefines.h"
#include "ThreatManager.h"
#include "HostileRefManager.h"
#include "FollowerReference.h"
#include "FollowerRefManager.h"
#include "Utilities/EventProcessor.h"
#include "MotionMaster.h"
#include "DBCStructure.h"
#include "Path.h"
#include "WorldPacket.h"
#include "Timer.h"

#include <array>
#include <vector>

namespace MopCompactPackets
{
    /// Body-only 18414 SMSG_MOVE_KNOCK_BACK serializer. The caller selects the
    /// opcode and supplies the already-computed horizontal direction vector.
    inline void BuildMoveKnockBack(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float horizontalSpeed, float verticalSpeed,
        float directionX, float directionY)
    {
        ObjectGuid const guid(moverGuid);
        out << horizontalSpeed << directionY << -verticalSpeed << counter
            << directionX;
        out.WriteGuidMask<2, 0, 7, 1, 4, 6, 5, 3>(guid);
        out.WriteGuidBytes<6, 0, 7, 5, 4, 3, 1, 2>(guid);
    }

    inline uint8 AttackGuidByte(uint64 guid, uint8 index)
    {
        return uint8(guid >> (8 * index));
    }

    inline ObjectGuid ReadAttackSwingTarget(WorldPacket& in)
    {
        ObjectGuid target;
        in.ReadGuidMask<6, 5, 7, 0, 3, 1, 4, 2>(target);
        in.ReadGuidBytes<6, 7, 1, 3, 2, 0, 4, 5>(target);
        return target;
    }

    /// CMSG_PET_ACTION (0x025B), the 18414 body.
    ///
    /// Recovered from decoded corpus bodies at catalogue 2BE10C89. Five bodies
    /// consume exactly: four with a zero position, and the single positional one
    /// in all 21,530 packets of the build. Every pet GUID that falls out decodes
    /// under HIGHGUID_PET and the one populated target under HIGHGUID_UNIT,
    /// which is a check independent of the packet itself.
    ///
    /// The action leads, then the position, then SIXTEEN presence bits
    /// INTERLEAVED across the two GUIDs -- not one mask byte each -- then the
    /// present bytes in their own order. An earlier reading here had it as one
    /// mask byte per GUID; that fitted every observed length, because only the
    /// total popcount sets the length, and it was still wrong.
    ///
    /// The bodies CONSTRAIN that order; the client's own writer PROVES it. Bits
    /// present together in every observed body stay mutually permutable, which
    /// left 86,400 equivalent orders. sub_68C8C8, the client's writer for this
    /// opcode, emits all sixteen presence bits and all sixteen bytes in exactly
    /// the sequence below, collapsing those to one.
    ///
    /// It settles the position too. The writer emits the three floats from
    /// object offsets +36, +40 and +32, so with x, y, z laid out at +32, +36,
    /// +40 the wire order is y, z, x -- which the coordinate bands had only
    /// supported, not established.
    ///
    /// Nothing consumes the position regardless, so no behaviour depends on it.
    inline bool ReadPetAction(WorldPacket& in, uint32& action,
        float& posY, float& posZ, float& posX,
        ObjectGuid& petGuid, ObjectGuid& targetGuid)
    {
        size_t const start = in.rpos();
        size_t const remaining = in.size() - start;
        if (remaining < 18)
        {
            in.rfinish();
            return false;
        }

        size_t presentByteCount = 0;
        for (size_t index = start + 16; index < start + 18; ++index)
        {
            for (uint8 bits = in[index]; bits; bits >>= 1)
                presentByteCount += bits & 1;
        }

        if (remaining != 18 + presentByteCount)
        {
            in.rfinish();
            return false;
        }

        for (size_t index = start + 18; index < in.size(); ++index)
        {
            if (in[index] == 1)
            {
                in.rfinish();
                return false;
            }
        }

        uint32 parsedAction = 0;
        float parsedPosY = 0.0f;
        float parsedPosZ = 0.0f;
        float parsedPosX = 0.0f;
        in >> parsedAction;
        in >> parsedPosY >> parsedPosZ >> parsedPosX;

        uint8 pet[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8 target[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        in.ResetBitReader();
        pet[1]    = in.ReadBit();  pet[0]    = in.ReadBit();
        pet[6]    = in.ReadBit();  pet[7]    = in.ReadBit();
        pet[5]    = in.ReadBit();  target[7] = in.ReadBit();
        pet[2]    = in.ReadBit();  pet[3]    = in.ReadBit();
        target[6] = in.ReadBit();  target[3] = in.ReadBit();
        target[0] = in.ReadBit();  target[2] = in.ReadBit();
        target[5] = in.ReadBit();  pet[4]    = in.ReadBit();
        target[4] = in.ReadBit();  target[1] = in.ReadBit();

        in.ReadByteSeq(pet[7]);     in.ReadByteSeq(pet[6]);
        in.ReadByteSeq(pet[1]);     in.ReadByteSeq(pet[2]);
        in.ReadByteSeq(pet[5]);     in.ReadByteSeq(pet[4]);
        in.ReadByteSeq(target[5]);  in.ReadByteSeq(pet[3]);
        in.ReadByteSeq(target[0]);  in.ReadByteSeq(target[1]);
        in.ReadByteSeq(target[7]);  in.ReadByteSeq(target[4]);
        in.ReadByteSeq(target[6]);  in.ReadByteSeq(target[2]);
        in.ReadByteSeq(target[3]);  in.ReadByteSeq(pet[0]);

        uint64 petRaw = 0;
        uint64 targetRaw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            petRaw |= uint64(pet[index]) << (8 * index);
            targetRaw |= uint64(target[index]) << (8 * index);
        }
        ObjectGuid const parsedPetGuid(petRaw);
        ObjectGuid const parsedTargetGuid(targetRaw);
        if (parsedPetGuid.IsEmpty() || in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        action = parsedAction;
        posY = parsedPosY;
        posZ = parsedPosZ;
        posX = parsedPosX;
        petGuid = parsedPetGuid;
        targetGuid = parsedTargetGuid;
        return true;
    }

    /// CMSG_PET_STOP_ATTACK (0x065B), the 18414 body: one packed
    /// controlled-unit GUID and no trailing fields. The reader consumes the
    /// two captured vehicle bodies documented at catalogue 2BE10C89; the dense
    /// and single-zero discriminator bodies are binary-derived from the client
    /// writer, not captured traffic.
    inline bool ReadPetStopAttack(WorldPacket& in, ObjectGuid& petGuid)
    {
        petGuid.Clear();
        if (in.rpos() >= in.size())
        {
            return false;
        }

        in.ResetBitReader();
        in.ReadGuidMask<7, 5, 1, 6, 0, 2, 4, 3>(petGuid);

        size_t presentByteCount = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            presentByteCount += petGuid[index] != 0;
        }

        if (in.size() - in.rpos() != presentByteCount)
        {
            petGuid.Clear();
            in.rfinish();
            return false;
        }

        for (size_t index = in.rpos(); index < in.size(); ++index)
        {
            if (in[index] == 1)
            {
                petGuid.Clear();
                in.rfinish();
                return false;
            }
        }

        in.ReadGuidBytes<2, 5, 0, 4, 1, 7, 6, 3>(petGuid);
        if (petGuid.IsEmpty() || in.rpos() != in.size())
        {
            petGuid.Clear();
            in.rfinish();
            return false;
        }

        return true;
    }

    /// CMSG_PET_SET_ACTION (0x12E9), the build-18414 single-record body.
    /// Layout changes and two-record swaps remain deliberately unsupported;
    /// this reader only establishes the exact wire record consumed by the
    /// bounded same-slot autocast handler.
    inline bool ReadPetSetAction(WorldPacket& in, uint32& position,
        uint32& actionData, ObjectGuid& petGuid)
    {
        size_t const start = in.rpos();
        size_t const remaining = in.size() - start;
        if (remaining < 9)
        {
            in.rfinish();
            return false;
        }

        uint8 const mask = in[start + 8];
        size_t presentByteCount = 0;
        for (uint8 bit = 0; bit < 8; ++bit)
            presentByteCount += (mask & (uint8(0x80) >> bit)) != 0;

        if (remaining != 9 + presentByteCount)
        {
            in.rfinish();
            return false;
        }

        for (size_t index = start + 9; index < in.size(); ++index)
        {
            if (in[index] == 1)
            {
                in.rfinish();
                return false;
            }
        }

        uint32 parsedPosition = 0;
        uint32 parsedActionData = 0;
        ObjectGuid parsedGuid;
        in >> parsedPosition >> parsedActionData;
        in.ResetBitReader();
        in.ReadGuidMask<1, 0, 5, 3, 2, 7, 6, 4>(parsedGuid);
        in.ReadGuidBytes<5, 6, 7, 3, 2, 1, 4, 0>(parsedGuid);

        if (parsedGuid.IsEmpty() || in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        position = parsedPosition;
        actionData = parsedActionData;
        petGuid = parsedGuid;
        return true;
    }

    /// CMSG_PET_NAME_QUERY (0x1C62), the 18414 body.
    ///
    /// Sixteen presence bits interleaved across the pet GUID and the pet number,
    /// the latter carried as a packed eight-byte value rather than a uint32.
    /// Nothing else: the body is 2 + popcount, which is 2 to 18.
    ///
    /// Verified against four decoded bodies of three different lengths. Each
    /// consumes exactly, every pet GUID decodes under HIGHGUID_PET, and every
    /// pet number falls in a plausible range with its top four bytes zero, which
    /// is what makes the shorter bodies short.
    ///
    /// Those bodies constrain the order only weakly -- a pet number's top bytes
    /// are absent in all of them, leaving over 87 million equivalent orders. The
    /// client's writer for this opcode, sub_6951B5, emits the sequence below
    /// element for element, which is what actually proves it.
    inline void ReadPetNameQuery(WorldPacket& in, ObjectGuid& petGuid,
        uint64& petNumber)
    {
        uint8 pet[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8 number[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        in.ResetBitReader();
        number[0] = in.ReadBit();  number[5] = in.ReadBit();
        pet[1]    = in.ReadBit();  pet[7]    = in.ReadBit();
        number[7] = in.ReadBit();  pet[6]    = in.ReadBit();
        pet[4]    = in.ReadBit();  pet[5]    = in.ReadBit();
        pet[0]    = in.ReadBit();  number[3] = in.ReadBit();
        number[6] = in.ReadBit();  number[2] = in.ReadBit();
        pet[3]    = in.ReadBit();  pet[2]    = in.ReadBit();
        number[1] = in.ReadBit();  number[4] = in.ReadBit();

        in.ReadByteSeq(number[2]); in.ReadByteSeq(number[1]);
        in.ReadByteSeq(number[0]); in.ReadByteSeq(number[7]);
        in.ReadByteSeq(pet[5]);    in.ReadByteSeq(pet[0]);
        in.ReadByteSeq(number[6]); in.ReadByteSeq(pet[4]);
        in.ReadByteSeq(number[5]); in.ReadByteSeq(pet[2]);
        in.ReadByteSeq(pet[6]);    in.ReadByteSeq(number[3]);
        in.ReadByteSeq(pet[3]);    in.ReadByteSeq(number[4]);
        in.ReadByteSeq(pet[1]);    in.ReadByteSeq(pet[7]);

        uint64 petRaw = 0;
        uint64 numberRaw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            petRaw |= uint64(pet[index]) << (8 * index);
            numberRaw |= uint64(number[index]) << (8 * index);
        }
        petGuid = ObjectGuid(petRaw);
        petNumber = numberRaw;
    }

    /// SMSG_PET_NAME_QUERY_RESPONSE (0x0ABE), the 18414 body.
    ///
    /// The pre-MoP body was petNumber, a null-terminated name, the timestamp and
    /// a declined-names flag. At 18414 the lengths lead as bit fields, the
    /// strings follow unterminated, and the pet number TRAILS as eight bytes.
    ///
    /// Verified by decoding real responses: they yield the pet names "Blue" and
    /// "Werenika" with a plausible timestamp, and one of them answers a request
    /// decoded separately whose pet number matches the response's exactly, which
    /// ties the two directions together.
    ///
    /// CAVEAT. Every observed response had no declined names, so all five length
    /// fields were zero. That pins the TOTAL width of the run before the name
    /// length at 36 bits, but not its division: five 7-bit fields plus one spare
    /// bit is the reading taken here and matches MAX_DECLINED_NAME_CASES, yet the
    /// same 36 bits would also admit other splits. Declined names are a
    /// locale-specific feature, so the common path is unaffected either way.
    inline void BuildPetNameQueryResponse(WorldPacket& out, uint64 petNumber,
        std::string const* name, uint32 timestamp,
        std::string const* declinedNames)
    {
        out.WriteBit(name != NULL);
        if (name != NULL)
        {
            for (uint8 index = 0; index < 5; ++index)
            {
                out.WriteBits(declinedNames != NULL ? declinedNames[index].size() : 0, 7);
            }
            out.WriteBit(0);
            out.WriteBits(name->size(), 8);
            out.FlushBits();

            if (declinedNames != NULL)
            {
                for (uint8 index = 0; index < 5; ++index)
                {
                    if (!declinedNames[index].empty())
                    {
                        out.append((uint8 const*)declinedNames[index].c_str(),
                                   declinedNames[index].size());
                    }
                }
            }
            if (!name->empty())
            {
                out.append((uint8 const*)name->c_str(), name->size());
            }
            out << uint32(timestamp);
        }
        else
        {
            out.FlushBits();
        }
        out << uint64(petNumber);
    }

    /// CMSG_LFG_JOIN (0x046B), the 18414 body.
    ///
    ///     uint8   partyIndex          0x7F in every observed body
    ///     uint32  unknown[3]          zero in every observed body
    ///     uint32  roles
    ///     22 bits dungeon count       MSB-first, packed with the two below
    ///     8 bits  comment length
    ///     1 bit   flag
    ///     1 bit   flush padding
    ///     uint32  dungeon[count]      id = value & 0xFFFFFF, type = value >> 24
    ///     bytes   comment             length-prefixed above, NOT terminated
    ///
    /// Total is 21 + 4 * count + commentLength. The inherited reader shared no
    /// field with this: it took the roles first, read a uint8 count, invented a
    /// trailing counted array that is not on the wire, and finished with a
    /// NUL-terminated string. Fed a real 25-byte body it decoded a count of zero
    /// and left sixteen bytes unread.
    ///
    /// COUNT BOUND. The count field is 22 bits, so a client may claim up to
    /// 4,194,303 dungeons in a 25-byte packet. The old reader resized a vector
    /// from the count BEFORE reading, which turns that into an allocation the
    /// packet never justified. This one refuses an impossible count outright: the
    /// body cannot hold more entries than its remaining bytes allow, and that is
    /// checkable before a single one is read.
    ///
    /// Returns false unless the body is exactly the size its own fields describe,
    /// leaving the caller to drop the request rather than trust a partial parse.
    inline bool ReadLfgJoin(WorldPacket& in, uint8& partyIndex, uint32& roles,
        uint32& flag, std::vector<uint32>& dungeons, std::string& comment)
    {
        in >> partyIndex;
        in.read_skip<uint32>();
        in.read_skip<uint32>();
        in.read_skip<uint32>();
        in >> roles;

        in.ResetBitReader();
        uint32 const count = in.ReadBits(22);
        uint32 const commentLength = in.ReadBits(8);
        flag = in.ReadBits(1);
        in.ReadBits(1);                                     // flush padding

        // Every remaining entry costs four bytes and the comment costs its own
        // length, so anything larger than that cannot be in this packet.
        // The grammar has an exact total, so a body with bytes left over is
        // malformed too, not merely one whose claim fits. Requiring equality
        // rejects a trailing suffix as well as an oversized claim.
        size_t const remaining = in.size() - in.rpos();
        if (size_t(count) * 4 + size_t(commentLength) != remaining)
        {
            return false;
        }

        dungeons.clear();
        dungeons.reserve(count);
        for (uint32 index = 0; index < count; ++index)
        {
            uint32 slot = 0;
            in >> slot;
            dungeons.push_back(slot);
        }

        comment.clear();
        if (commentLength)
        {
            comment.assign((char const*)in.contents() + in.rpos(), commentLength);
            in.read_skip(commentLength);
        }
        return true;
    }

    /// The mailbox family, and the guild-bank tab query that shares its shape.
    ///
    /// All four carry one packed GUID and differ only in where the scalars sit
    /// and how the presence bits are ordered. Each inherited reader took a raw
    /// ObjectGuid first, which matches none of them.
    ///
    /// The GUID orders here are constrained by more than presence masks. In five
    /// sessions the mailbox GUID recovered from MARK_AS_READ and TAKE_ITEM is
    /// byte-identical to the one GET_MAIL_LIST yields seconds earlier through a
    /// different mask order AND a different byte order. A wrong byte order
    /// permutes distinct byte values, so three independent orders agreeing on the
    /// same GUID is evidence no single opcode's fixtures could give.
    inline ObjectGuid ReadMailboxGuid(WorldPacket& in,
        uint8 const (&maskOrder)[8], uint8 const (&byteOrder)[8])
    {
        uint8 guid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        for (uint8 index = 0; index < 8; ++index)
        {
            guid[maskOrder[index]] = in.ReadBit();
        }
        for (uint8 index = 0; index < 8; ++index)
        {
            in.ReadByteSeq(guid[byteOrder[index]]);
        }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            raw |= uint64(guid[index]) << (8 * index);
        }
        return ObjectGuid(raw);
    }

    inline size_t MailGuidPresentByteCount(std::array<uint8, 8> const& bytes)
    {
        size_t count = 0;
        for (uint8 value : bytes)
            count += value != 0;
        return count;
    }

    inline ObjectGuid MailGuidFromBytes(std::array<uint8, 8> const& bytes)
    {
        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
            raw |= uint64(bytes[index]) << (8 * index);
        return ObjectGuid(raw);
    }

    inline bool ReadCanonicalMailGuidByte(WorldPacket& in, uint8& value)
    {
        if (!value)
            return true;
        if (in.rpos() >= in.size() || in[in.rpos()] == 0x01)
            return false;
        in.ReadByteSeq(value);
        return true;
    }

    struct MailAttachmentRequest
    {
        uint8 slot = 0;
        ObjectGuid itemGuid;
    };

    struct SendMailRequest
    {
        uint32 stationeryId = 0;
        uint32 packageId = 0;
        uint64 COD = 0;
        uint64 money = 0;
        ObjectGuid mailboxGuid;
        std::string receiver;
        std::string subject;
        std::string body;
        std::vector<MailAttachmentRequest> attachments;
    };

    /// CMSG_SEND_MAIL (0x1DBA), client writer sub_66C61A. The reader first
    /// proves the complete variable body size, then consumes it exactly.
    inline bool ReadSendMail(WorldPacket& in, SendMailRequest& request)
    {
        request = SendMailRequest();
        size_t const start = in.rpos();
        if (in.size() - start < 30)
        {
            in.rfinish();
            return false;
        }

        SendMailRequest parsed;
        in >> parsed.stationeryId >> parsed.packageId >> parsed.COD >> parsed.money;
        in.ResetBitReader();

        std::array<uint8, 8> mailbox = {};
        mailbox[0] = in.ReadBit();
        mailbox[6] = in.ReadBit();
        mailbox[4] = in.ReadBit();
        mailbox[1] = in.ReadBit();
        uint32 const bodyLength = in.ReadBits(11);
        mailbox[3] = in.ReadBit();
        uint32 const receiverLength = in.ReadBits(9);
        mailbox[7] = in.ReadBit();
        mailbox[5] = in.ReadBit();
        uint32 const attachmentCount = in.ReadBits(5);
        if (attachmentCount > 12 || in.size() - start < 30 + attachmentCount)
        {
            in.rfinish();
            return false;
        }

        std::vector<std::array<uint8, 8>> itemBytes(attachmentCount);
        for (uint32 item = 0; item < attachmentCount; ++item)
        {
            std::array<uint8, 8>& guid = itemBytes[item];
            guid[1] = in.ReadBit();
            guid[7] = in.ReadBit();
            guid[2] = in.ReadBit();
            guid[5] = in.ReadBit();
            guid[0] = in.ReadBit();
            guid[6] = in.ReadBit();
            guid[3] = in.ReadBit();
            guid[4] = in.ReadBit();
        }
        uint32 const subjectLength = in.ReadBits(9);
        mailbox[2] = in.ReadBit();
        in.ResetBitReader();

        size_t required = bodyLength + subjectLength + receiverLength +
            MailGuidPresentByteCount(mailbox);
        for (std::array<uint8, 8> const& guid : itemBytes)
            required += 1 + MailGuidPresentByteCount(guid);
        if (in.size() - in.rpos() != required)
        {
            in.rfinish();
            return false;
        }

        parsed.attachments.reserve(attachmentCount);
        for (std::array<uint8, 8>& guid : itemBytes)
        {
            MailAttachmentRequest attachment;
            in >> attachment.slot;
            for (uint8 index : { uint8(3), uint8(0), uint8(2), uint8(1),
                                 uint8(6), uint8(5), uint8(7), uint8(4) })
            {
                if (!ReadCanonicalMailGuidByte(in, guid[index]))
                {
                    in.rfinish();
                    return false;
                }
            }
            attachment.itemGuid = MailGuidFromBytes(guid);
            parsed.attachments.push_back(attachment);
        }

        if (!ReadCanonicalMailGuidByte(in, mailbox[1]))
        {
            in.rfinish();
            return false;
        }
        parsed.body = in.ReadString(bodyLength);
        if (!ReadCanonicalMailGuidByte(in, mailbox[0]))
        {
            in.rfinish();
            return false;
        }
        parsed.subject = in.ReadString(subjectLength);
        for (uint8 index : { uint8(2), uint8(6), uint8(5), uint8(7),
                             uint8(3), uint8(4) })
        {
            if (!ReadCanonicalMailGuidByte(in, mailbox[index]))
            {
                in.rfinish();
                return false;
            }
        }
        parsed.receiver = in.ReadString(receiverLength);
        if (in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        parsed.mailboxGuid = MailGuidFromBytes(mailbox);
        request = parsed;
        return true;
    }

    struct MailDeleteRequest
    {
        uint32 mailId = 0;
        uint32 deleteMode = 0;
    };

    inline bool ReadMailDelete(WorldPacket& in, MailDeleteRequest& request)
    {
        request = MailDeleteRequest();
        if (in.size() - in.rpos() != 8)
        {
            in.rfinish();
            return false;
        }
        MailDeleteRequest parsed;
        in >> parsed.mailId >> parsed.deleteMode;
        request = parsed;
        return true;
    }

    inline bool ReadStrictMailGuid(WorldPacket& in, uint8 const (&maskOrder)[8],
        uint8 const (&byteOrder)[8], ObjectGuid& result)
    {
        std::array<uint8, 8> guid = {};
        in.ResetBitReader();
        for (uint8 index : maskOrder)
            guid[index] = in.ReadBit();
        in.ResetBitReader();
        if (in.size() - in.rpos() != MailGuidPresentByteCount(guid))
        {
            in.rfinish();
            return false;
        }
        for (uint8 index : byteOrder)
        {
            if (!ReadCanonicalMailGuidByte(in, guid[index]))
            {
                in.rfinish();
                return false;
            }
        }
        result = MailGuidFromBytes(guid);
        return in.rpos() == in.size();
    }

    struct MailReturnRequest
    {
        uint32 mailId = 0;
        ObjectGuid senderGuid;
    };

    inline bool ReadMailReturnToSender(WorldPacket& in, MailReturnRequest& request)
    {
        request = MailReturnRequest();
        if (in.size() - in.rpos() < 5)
        {
            in.rfinish();
            return false;
        }
        MailReturnRequest parsed;
        in >> parsed.mailId;
        uint8 const maskOrder[] = { 2, 0, 4, 6, 3, 1, 7, 5 };
        uint8 const byteOrder[] = { 5, 6, 2, 0, 3, 1, 4, 7 };
        if (!ReadStrictMailGuid(in, maskOrder, byteOrder, parsed.senderGuid))
            return false;
        request = parsed;
        return true;
    }

    struct MailCreateTextItemRequest
    {
        uint32 mailId = 0;
        ObjectGuid mailboxGuid;
    };

    inline bool ReadMailCreateTextItem(WorldPacket& in,
        MailCreateTextItemRequest& request)
    {
        request = MailCreateTextItemRequest();
        if (in.size() - in.rpos() < 5)
        {
            in.rfinish();
            return false;
        }
        MailCreateTextItemRequest parsed;
        in >> parsed.mailId;
        uint8 const maskOrder[] = { 4, 1, 6, 2, 5, 3, 0, 7 };
        uint8 const byteOrder[] = { 6, 5, 4, 3, 0, 7, 2, 1 };
        if (!ReadStrictMailGuid(in, maskOrder, byteOrder, parsed.mailboxGuid))
            return false;
        request = parsed;
        return true;
    }

    /// CMSG_GET_MAIL_LIST (0x077A): the mask, then the present bytes. Nothing else.
    inline ObjectGuid ReadGetMailList(WorldPacket& in)
    {
        uint8 const maskOrder[] = { 6, 3, 7, 5, 4, 1, 2, 0 };
        uint8 const byteOrder[] = { 7, 1, 6, 5, 4, 2, 3, 0 };
        in.ResetBitReader();
        return ReadMailboxGuid(in, maskOrder, byteOrder);
    }

    /// CMSG_MAIL_MARK_AS_READ (0x0241): the mail id leads, then NINE mask bits
    /// spanning two bytes -- eight GUID bits plus one that belongs to no GUID
    /// byte and is zero in every observed body -- then the present bytes.
    inline ObjectGuid ReadMailMarkAsRead(WorldPacket& in, uint32& mailId)
    {
        uint8 const byteOrder[] = { 1, 7, 2, 5, 6, 3, 4, 0 };
        in >> mailId;

        uint8 guid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        in.ResetBitReader();
        guid[0] = in.ReadBit();  guid[2] = in.ReadBit();
        guid[3] = in.ReadBit();  in.ReadBit();               // not a GUID bit
        guid[4] = in.ReadBit();  guid[6] = in.ReadBit();
        guid[7] = in.ReadBit();  guid[1] = in.ReadBit();
        guid[5] = in.ReadBit();                              // opens the second byte

        for (uint8 index = 0; index < 8; ++index)
        {
            in.ReadByteSeq(guid[byteOrder[index]]);
        }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            raw |= uint64(guid[index]) << (8 * index);
        }
        return ObjectGuid(raw);
    }

    /// CMSG_MAIL_TAKE_ITEM (0x1371): mail id, then the item's low GUID, then the
    /// mask and the present bytes.
    inline ObjectGuid ReadMailTakeItem(WorldPacket& in, uint32& mailId, uint32& itemId)
    {
        uint8 const maskOrder[] = { 6, 5, 2, 3, 0, 1, 4, 7 };
        uint8 const byteOrder[] = { 0, 1, 4, 2, 5, 6, 3, 7 };
        in >> mailId;
        in >> itemId;
        in.ResetBitReader();
        return ReadMailboxGuid(in, maskOrder, byteOrder);
    }

    struct MailTakeMoneyRequest
    {
        uint32 mailId = 0;
        uint64 claimedMoney = 0;
        ObjectGuid mailboxGuid;
    };

    /// CMSG_MAIL_TAKE_MONEY (0x06FA): mail id and displayed money precede the
    /// packed mailbox GUID. The money is client-supplied context only; callers
    /// must use the player-owned Mail record as the credit authority.
    inline bool ReadMailTakeMoney(WorldPacket& in, MailTakeMoneyRequest& request)
    {
        request = MailTakeMoneyRequest();
        size_t const start = in.rpos();
        if (in.size() - start < 13)
        {
            in.rfinish();
            return false;
        }

        uint8 const mask = in.contents()[start + 12];
        size_t presentBytes = 0;
        for (uint8 bit = 0; bit < 8; ++bit)
        {
            presentBytes += (mask >> bit) & 1;
        }
        if (in.size() - start != 13 + presentBytes)
        {
            in.rfinish();
            return false;
        }
        for (size_t index = start + 13; index < in.size(); ++index)
        {
            if (in[index] == 0x01)
            {
                in.rfinish();
                return false;
            }
        }

        MailTakeMoneyRequest parsed;
        in >> parsed.mailId;
        in >> parsed.claimedMoney;
        uint8 const maskOrder[] = { 7, 6, 3, 2, 4, 5, 0, 1 };
        uint8 const byteOrder[] = { 7, 1, 4, 0, 3, 2, 6, 5 };
        in.ResetBitReader();
        parsed.mailboxGuid = ReadMailboxGuid(in, maskOrder, byteOrder);

        if (in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        request = parsed;
        return true;
    }

    /// CMSG_GUILD_BANK_QUERY_TAB (0x1372): the tab id leads, then nine mask bits
    /// over two bytes of which one is a standalone boolean, then the bytes. That
    /// boolean is proven by two bodies of equal length differing only in it.
    inline ObjectGuid ReadGuildBankQueryTab(WorldPacket& in, uint8& tabId, bool& sendAllSlots)
    {
        uint8 const byteOrder[] = { 3, 7, 6, 4, 2, 5, 0, 1 };
        in >> tabId;

        uint8 guid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        in.ResetBitReader();
        guid[7] = in.ReadBit();  guid[3] = in.ReadBit();
        sendAllSlots = in.ReadBit();
        guid[0] = in.ReadBit();  guid[2] = in.ReadBit();
        guid[4] = in.ReadBit();  guid[1] = in.ReadBit();
        guid[6] = in.ReadBit();
        guid[5] = in.ReadBit();                              // opens the second byte

        for (uint8 index = 0; index < 8; ++index)
        {
            in.ReadByteSeq(guid[byteOrder[index]]);
        }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            raw |= uint64(guid[index]) << (8 * index);
        }
        return ObjectGuid(raw);
    }

    /// CMSG_GUILD_BANKER_ACTIVATE (0x0372): eight packed GUID-presence bits
    /// and one standalone full-slot-refresh bit span two mask bytes. The low
    /// seven bits of the second byte are writer padding, and there are no
    /// scalar fields before or after the XOR-obfuscated GUID bytes.
    inline bool ReadGuildBankerActivate(WorldPacket& in,
        ObjectGuid& bankGuid, bool& fullSlotRefresh)
    {
        size_t const remaining = in.size() - in.rpos();
        if (remaining < 2)
        {
            in.rfinish();
            return false;
        }

        uint8 const firstMask = in[in.rpos()];
        uint8 const secondMask = in[in.rpos() + 1];
        if ((secondMask & 0x7F) != 0)
        {
            in.rfinish();
            return false;
        }

        size_t guidByteCount = (secondMask & 0x80) ? 1 : 0;
        for (uint8 bits = firstMask & 0xBF; bits; bits >>= 1)
        {
            guidByteCount += bits & 1;
        }
        if (remaining != 2 + guidByteCount)
        {
            in.rfinish();
            return false;
        }

        // ReadByteSeq XORs each present wire byte with one. A raw one would
        // decode to zero despite its presence bit and is non-canonical.
        for (size_t index = in.rpos() + 2; index < in.size(); ++index)
        {
            if (in[index] == 1)
            {
                in.rfinish();
                return false;
            }
        }

        uint8 guid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        in.ResetBitReader();
        guid[3] = in.ReadBit();
        bool const parsedFullSlotRefresh = in.ReadBit();
        guid[0] = in.ReadBit();
        guid[7] = in.ReadBit();
        guid[1] = in.ReadBit();
        guid[5] = in.ReadBit();
        guid[2] = in.ReadBit();
        guid[6] = in.ReadBit();
        guid[4] = in.ReadBit();

        uint8 const byteOrder[] = { 7, 1, 0, 6, 4, 2, 5, 3 };
        for (uint8 index = 0; index < 8; ++index)
        {
            in.ReadByteSeq(guid[byteOrder[index]]);
        }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            raw |= uint64(guid[index]) << (8 * index);
        }
        if (raw == 0 || in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        bankGuid = ObjectGuid(raw);
        fullSlotRefresh = parsedFullSlotRefresh;
        return true;
    }

    /// The two guild-bank money opcodes: a uint64 amount, then one mask byte,
    /// then the present bytes of the bank object's packed GUID. Both writers --
    /// sub_68E68D for deposit, sub_68D659 for withdraw -- emit the amount before
    /// the bit section and differ only in their two orders, so the walk is
    /// written once. The single retail deposit in the corpus is fifteen bytes
    /// (capture-000888 sequence 307413): eight of amount, one mask, six present
    /// GUID bytes. The inherited handlers read a raw eight-byte GUID for sixteen
    /// bytes total and could not have parsed it.
    inline bool ReadGuildBankMoneyBody(WorldPacket& in, uint64& money,
        ObjectGuid& bankGuid,
        uint8 const (&maskOrder)[8], uint8 const (&byteOrder)[8])
    {
        size_t const remaining = in.size() - in.rpos();
        if (remaining < 9)
        {
            in.rfinish();
            return false;
        }

        uint8 const mask = in[in.rpos() + 8];
        size_t guidByteCount = 0;
        for (uint8 bits = mask; bits; bits >>= 1)
        {
            guidByteCount += bits & 1;
        }
        if (remaining != 9 + guidByteCount)
        {
            in.rfinish();
            return false;
        }

        // ReadByteSeq XORs each present wire byte with one, so a raw one decodes
        // to zero despite its presence bit. Refuse rather than accept a GUID the
        // client could not have written.
        for (size_t index = in.rpos() + 9; index < in.size(); ++index)
        {
            if (in[index] == 1)
            {
                in.rfinish();
                return false;
            }
        }

        uint64 parsedMoney = 0;
        in >> parsedMoney;

        uint8 guid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        in.ResetBitReader();
        for (uint8 index = 0; index < 8; ++index)
        {
            guid[maskOrder[index]] = in.ReadBit();
        }
        for (uint8 index = 0; index < 8; ++index)
        {
            in.ReadByteSeq(guid[byteOrder[index]]);
        }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            raw |= uint64(guid[index]) << (8 * index);
        }
        if (raw == 0 || in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        money = parsedMoney;
        bankGuid = ObjectGuid(raw);
        return true;
    }

    /// CMSG_GUILD_BANK_DEPOSIT_MONEY (0x0770), writer sub_68E68D. Verified
    /// against capture-000888 sequence 307413: fifteen of fifteen bytes, one
    /// gold, and a gameobject GUID as the handler's interaction check requires.
    inline bool ReadGuildBankDepositMoney(WorldPacket& in, uint64& money,
        ObjectGuid& bankGuid)
    {
        uint8 const maskOrder[] = { 2, 7, 6, 4, 0, 1, 5, 3 };
        uint8 const byteOrder[] = { 1, 4, 5, 0, 2, 7, 6, 3 };
        return ReadGuildBankMoneyBody(in, money, bankGuid, maskOrder, byteOrder);
    }

    /// CMSG_GUILD_BANK_WITHDRAW_MONEY (0x07EA), writer sub_68D659. NO retail
    /// capture of this opcode exists anywhere in the 18414 corpus, so unlike
    /// deposit these orders have never met a real body. They are not guesswork
    /// either: IDA and Binary Ninja were read independently and agree on both,
    /// and the function boundary was checked (sub_68D659 ends at 0x68D7B2, right
    /// after the eighth byte write) so nothing follows the GUID and the reader's
    /// insistence on consuming the whole body is right. The shape is otherwise
    /// identical to deposit.
    inline bool ReadGuildBankWithdrawMoney(WorldPacket& in, uint64& money,
        ObjectGuid& bankGuid)
    {
        uint8 const maskOrder[] = { 1, 3, 7, 6, 5, 0, 4, 2 };
        uint8 const byteOrder[] = { 0, 7, 4, 2, 1, 6, 3, 5 };
        return ReadGuildBankMoneyBody(in, money, bankGuid, maskOrder, byteOrder);
    }

    /// CMSG_GUILD_BANK_BUY_TAB (0x0251), writer sub_688164: the tab id as a
    /// plain byte -- written raw, not XOR-obfuscated -- then the bank object's
    /// packed GUID. The inherited handler read a raw eight-byte GUID and THEN the
    /// tab, so it had both the order and the encoding wrong.
    ///
    /// No capture of this opcode exists at build 18414, so these orders have
    /// never met a real body. (Joining by value AND direction, CMSG 593 does
    /// appear at other builds, but every one of those bodies is a single byte,
    /// which this writer cannot produce -- it always emits at least a tab and a
    /// mask -- so they are a different opcode at those builds.) Binary Ninja and IDA were read
    /// independently and agree on both, and the function ends at 0x6882BC right
    /// after the eighth byte write, so nothing follows the GUID.
    ///
    /// ReadPrefixedPackedValue below walks this same shape for two other opcodes,
    /// but it performs no validation at all. Buying a tab spends gold, so this
    /// one rejects rather than returns whatever it happened to read.
    inline bool ReadGuildBankBuyTab(WorldPacket& in, uint8& tabId,
        ObjectGuid& bankGuid)
    {
        size_t const remaining = in.size() - in.rpos();
        if (remaining < 2)
        {
            in.rfinish();
            return false;
        }

        uint8 const mask = in[in.rpos() + 1];
        size_t guidByteCount = 0;
        for (uint8 bits = mask; bits; bits >>= 1)
        {
            guidByteCount += bits & 1;
        }
        if (remaining != 2 + guidByteCount)
        {
            in.rfinish();
            return false;
        }

        for (size_t index = in.rpos() + 2; index < in.size(); ++index)
        {
            if (in[index] == 1)
            {
                in.rfinish();
                return false;
            }
        }

        uint8 parsedTab = 0;
        in >> parsedTab;

        uint8 const maskOrder[] = { 0, 1, 3, 7, 2, 6, 5, 4 };
        uint8 const byteOrder[] = { 1, 4, 6, 7, 3, 5, 2, 0 };
        uint8 guid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        in.ResetBitReader();
        for (uint8 index = 0; index < 8; ++index)
        {
            guid[maskOrder[index]] = in.ReadBit();
        }
        for (uint8 index = 0; index < 8; ++index)
        {
            in.ReadByteSeq(guid[byteOrder[index]]);
        }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            raw |= uint64(guid[index]) << (8 * index);
        }
        if (raw == 0 || in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        tabId = parsedTab;
        bankGuid = ObjectGuid(raw);
        return true;
    }

    /// CMSG_GUILD_BANK_UPDATE_TAB (0x07C2) -- thunk sub_686A1D, vtable 0xD64874
    /// with the 0x00C84A3D signature in slot +12, body writer sub_68B694.
    ///
    /// The tab id leads as a plain byte. Then a bit stream that interleaves the
    /// bank GUID's presence mask with BOTH string lengths, and only afterwards
    /// the byte section, where the two strings sit between GUID bytes rather
    /// than after them. It comes to exactly 24 bits, so the client's flush adds
    /// no padding and the body is byte-aligned from the tab id onwards.
    ///
    /// Which string is which is NOT decidable from the writer. It emits two
    /// variable-length strings and nothing on the wire labels either. The Lua
    /// binding settles it: sub_96F6DE is SetGuildBankTabInfo(tab, name,
    /// iconFileName), and it calls sub_96EFA2(tab, name, icon), which stores the
    /// name at object +0x11 and the icon path at +0x60. The writer gives +0x11 a
    /// 7-bit length and +0x60 a 9-bit one, which fits those two fields and would
    /// not fit them reversed.
    ///
    /// The caps below are the client's copy limits, not the bit fields' ranges:
    /// 7 bits would allow 127 and 9 would allow 511, but the client cannot emit
    /// more than it copies. Both copy loops are the same shape -- count down from
    /// a limit, then write the terminator wherever the pointer stopped -- so the
    /// limit is the maximum strlen, NOT one less than it: the name loop at
    /// 0x96F037 runs from 0x40 and permits 64, and the icon loop at 0x96F05F runs
    /// from 0x100 and permits 256. Anything longer did not come from a stock
    /// client. (Both are far above what is reachable in practice -- the name is
    /// sanitised at 0x96EFB8 before it is even copied -- sub_CBCC7F terminates as
    /// its count REACHES 16, so 15 characters survive, which the client's own
    /// Blizzard_GuildBankUI.xml confirms with letters="15" -- and the icon is a
    /// bare macro filename -- but a reader that refused a body the
    /// client can legitimately produce would be the worse error.)
    ///
    /// The 9-bit length is written as a whole byte (sub_665185) followed by one
    /// bit (sub_665157), high part first, so it reassembles as (hi << 1) | lo.
    ///
    /// No capture of this opcode exists at build 18414 under catalogue
    /// generation 2BE10C89...88752, so this layout has never met a real body.
    /// The fixture covering it is synthetic and encoded from the writer above,
    /// not from this reader.
    inline bool ReadGuildBankUpdateTab(WorldPacket& in, uint8& tabId,
        std::string& name, std::string& icon, ObjectGuid& bankGuid)
    {
        size_t const remaining = in.size() - in.rpos();
        if (remaining < 4)                                  // tab id plus 24 mask bits
        {
            in.rfinish();
            return false;
        }

        uint8 parsedTab = 0;
        in >> parsedTab;

        // Mask bits and the two lengths share one stream, in the writer's order:
        // guid[5], the 9-bit icon length, the remaining seven mask bits, then the
        // 7-bit name length.
        uint8 guid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8 const maskTail[] = { 1, 4, 2, 7, 0, 6, 3 };
        in.ResetBitReader();
        guid[5] = in.ReadBit();
        uint32 const iconHigh = in.ReadBits(8);
        uint32 const iconLow = in.ReadBits(1);
        uint32 const iconLength = (iconHigh << 1) | iconLow;
        for (uint8 index = 0; index < 7; ++index)
        {
            guid[maskTail[index]] = in.ReadBit();
        }
        uint32 const nameLength = in.ReadBits(7);

        // The client's own limits. Deliberately literals rather than
        // MopGuildBankPackets::MAX_TAB_*_BYTES: this header cannot include that one
        // without dragging its dependencies into everything that includes Unit.h.
        // They must stay in step all the same -- they were 255 and 256 for a while,
        // and that skew is what a review caught, so change both or neither.
        if (iconLength > 256 || nameLength > 64)
        {
            in.rfinish();
            return false;
        }

        uint8 present[8];
        size_t guidByteCount = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            present[index] = guid[index];
            guidByteCount += guid[index] ? 1 : 0;
        }
        if (remaining != 4 + guidByteCount + iconLength + nameLength)
        {
            in.rfinish();
            return false;
        }

        // guid[7], guid[4], icon, guid[5], guid[1], guid[0], name, guid[2],
        // guid[3], guid[6] -- the strings are not NUL terminated.
        uint8 const beforeIcon[] = { 7, 4 };
        uint8 const betweenStrings[] = { 5, 1, 0 };
        uint8 const afterName[] = { 2, 3, 6 };
        for (uint8 index = 0; index < 2; ++index)
        {
            in.ReadByteSeq(guid[beforeIcon[index]]);
        }
        // Parsed into locals, not the caller's out-params: two checks remain
        // below, and a reader that rejects a body must leave every out-param
        // untouched, the way the buy-tab and money readers do.
        std::string parsedIcon = in.ReadString(iconLength);
        for (uint8 index = 0; index < 3; ++index)
        {
            in.ReadByteSeq(guid[betweenStrings[index]]);
        }
        std::string parsedName = in.ReadString(nameLength);
        for (uint8 index = 0; index < 3; ++index)
        {
            in.ReadByteSeq(guid[afterName[index]]);
        }

        // Neither string may carry an embedded NUL. The client's writer takes both
        // lengths from strlen, so it cannot emit one; and downstream they stop
        // being length-delimited -- Guild::SetGuildBankTabInfo stores them in a
        // std::string but the reload path hands the column back as a C string, so
        // "A\0B" would come back as "A" and memory would disagree with the
        // database. It also defeats the handler's own non-empty policy: a name of
        // one NUL byte is non-empty to size(), and empty to every downstream
        // C-string consumer and to whatever survives a reload.
        if (parsedName.find('\0') != std::string::npos ||
            parsedIcon.find('\0') != std::string::npos)
        {
            in.rfinish();
            return false;
        }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            // A byte the mask called present cannot decode to zero: the client
            // marks a zero byte absent, so 0x01 on the wire (which XORs to 0) is
            // a body no stock client produces. Checked per byte rather than by
            // scanning the tail, because the two strings sit inside that tail
            // and may legitimately contain 0x01.
            if (present[index] && guid[index] == 0)
            {
                in.rfinish();
                return false;
            }
            raw |= uint64(guid[index]) << (8 * index);
        }
        if (raw == 0 || in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        tabId = parsedTab;
        name = parsedName;
        icon = parsedIcon;
        bankGuid = ObjectGuid(raw);
        return true;
    }

    /// One CMSG_GUILD_BANK_SWAP_ITEMS body, as the 18414 client builds it.
    ///
    /// Absent fields are left at the sentinels the client's own constructor
    /// (sub_686794) uses, so "absent" and "zero" stay distinguishable: srcTab is
    /// 0xFF when there is no bank-side source, and the rest are zero.
    struct GuildBankSwapItems
    {
        ObjectGuid bankGuid;                                // +0x30, packed
        uint32 splitAmount;                                 // +0x18, 0 = whole stack
        uint32 entryAtBankSlot;                             // +0x20, 0 = that slot is empty
        uint32 srcEntry;                                    // +0x28, entry at (srcTab, srcSlot)
        uint32 autoStoreCount;                              // +0x1c, full stack size
        uint8  bankTab;                                     // +0x14
        uint8  bankSlot;                                    // +0x26, 0xFF = anywhere in tab
        uint8  toChar;                                      // +0x25, 1 = bank -> player
        uint8  playerBag;                                   // +0x10, 0xFF = backpack
        uint8  playerSlot;                                  // +0x24
        uint8  srcTab;                                      // +0x12, 0xFF = none
        uint8  srcSlot;                                     // +0x13
        bool   autoStore;                                   // +0x11
        bool   bankToBank;                                  // +0x2c
    };

    /// CMSG_GUILD_BANK_SWAP_ITEMS (0x136A) -- thunk sub_6865DF, vtable 0xD648EC
    /// with the 0x00C84A3D signature in slot +12, body writer sub_68A2FD.
    ///
    /// FOUR different player actions build this one opcode, with different
    /// subsets of the fields set, so the server must dispatch on the flags rather
    /// than assume a shape. The inherited handler read a raw GUID first and then
    /// branched on a plain BankToBank byte; at 18414 neither of those is where it
    /// thought, and both flags live in the bit stream.
    ///
    /// Eleven plain bytes lead, then a 16-bit stream, then the packed GUID and
    /// six optional scalars. Six of those sixteen bits are INVERTED presence --
    /// the bit is SET when the field is absent -- because the writer emits
    /// `sete` on a comparison against the field's own "none" value. srcTab's
    /// none-value is 0xFF; the other five use zero.
    ///
    /// The dangerous field pair: bankTab/bankSlot is NOT always the source. It is
    /// the bank-side slot of the operation, which is the source for a withdrawal
    /// but the DESTINATION for a bank-to-bank move, where the source is
    /// srcTab/srcSlot. A reference fork naming these "BankTab" and "BankTabDst"
    /// has them the other way round for that case.
    ///
    /// That moves the WRONG item; it does not duplicate one, and an earlier
    /// version of this comment said it did. Guild::SwapItems reads its first pair
    /// as the source, so with the pairs reversed a move into an empty slot finds
    /// nothing there and returns, and a move between two occupied slots still
    /// conserves both stacks. What you get is a silent no-op or an item dragged
    /// the opposite way -- bad on an item path, but not a dupe.
    ///
    /// That was settled from the wire, not just the binary: querying the corpus
    /// (generation 2BE10C89...88752) for bank-to-bank bodies whose entryAtBankSlot
    /// is zero returns twelve packets -- capture-000188 seq 6613 and
    /// capture-000192 seq 18440 among them -- in which bankTab/bankSlot names an
    /// EMPTY slot while srcTab/srcSlot holds a real item. An empty slot cannot be
    /// a source. Ordinary swaps are symmetric and cannot tell the two apart.
    inline bool ReadGuildBankSwapItems(WorldPacket& in, GuildBankSwapItems& out)
    {
        size_t const remaining = in.size() - in.rpos();
        if (remaining < 13)                                 // 11 plain bytes plus 16 mask bits
        {
            in.rfinish();
            return false;
        }

        GuildBankSwapItems parsed;
        parsed.srcTab = 0xFF;                               // the client's own "none"
        parsed.srcSlot = 0;
        parsed.playerBag = 0;
        parsed.playerSlot = 0;
        parsed.srcEntry = 0;
        parsed.autoStoreCount = 0;

        in >> parsed.splitAmount;                           // +0x18, written first
        in >> parsed.bankSlot;                              // +0x26
        in >> parsed.toChar;                                // +0x25
        in >> parsed.entryAtBankSlot;                       // +0x20
        in >> parsed.bankTab;                               // +0x14

        uint8 guid[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        bool absent[6] = { false, false, false, false, false, false };
        enum { ABS_SRC_ENTRY, ABS_PLAYER_BAG, ABS_PLAYER_SLOT, ABS_SRC_SLOT, ABS_AUTOSTORE, ABS_SRC_TAB };

        in.ResetBitReader();
        guid[5]                  = in.ReadBit();
        absent[ABS_SRC_TAB]      = in.ReadBit() != 0;
        guid[1]                  = in.ReadBit();
        absent[ABS_PLAYER_BAG]   = in.ReadBit() != 0;
        parsed.autoStore         = in.ReadBit() != 0;
        guid[0]                  = in.ReadBit();
        absent[ABS_SRC_ENTRY]    = in.ReadBit() != 0;
        absent[ABS_SRC_SLOT]     = in.ReadBit() != 0;
        guid[2]                  = in.ReadBit();
        parsed.bankToBank        = in.ReadBit() != 0;
        guid[4]                  = in.ReadBit();
        guid[7]                  = in.ReadBit();
        guid[3]                  = in.ReadBit();
        absent[ABS_PLAYER_SLOT]  = in.ReadBit() != 0;
        guid[6]                  = in.ReadBit();
        absent[ABS_AUTOSTORE]    = in.ReadBit() != 0;

        uint8 present[8];
        size_t guidByteCount = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            present[index] = guid[index];
            guidByteCount += guid[index] ? 1 : 0;
        }

        size_t const optionalBytes =
            (absent[ABS_SRC_ENTRY]   ? 0 : 4) +
            (absent[ABS_PLAYER_BAG]  ? 0 : 1) +
            (absent[ABS_PLAYER_SLOT] ? 0 : 1) +
            (absent[ABS_SRC_SLOT]    ? 0 : 1) +
            (absent[ABS_AUTOSTORE]   ? 0 : 4) +
            (absent[ABS_SRC_TAB]     ? 0 : 1);

        if (remaining != 13 + guidByteCount + optionalBytes)
        {
            in.rfinish();
            return false;
        }

        uint8 const byteOrder[] = { 2, 6, 5, 4, 0, 3, 1, 7 };
        for (uint8 index = 0; index < 8; ++index)
        {
            in.ReadByteSeq(guid[byteOrder[index]]);
        }

        if (!absent[ABS_SRC_ENTRY])   { in >> parsed.srcEntry; }
        if (!absent[ABS_PLAYER_BAG])  { in >> parsed.playerBag; }
        if (!absent[ABS_PLAYER_SLOT]) { in >> parsed.playerSlot; }
        if (!absent[ABS_SRC_SLOT])    { in >> parsed.srcSlot; }
        if (!absent[ABS_AUTOSTORE])   { in >> parsed.autoStoreCount; }
        if (!absent[ABS_SRC_TAB])     { in >> parsed.srcTab; }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            if (present[index] && guid[index] == 0)         // 0x01 on the wire XORs to 0
            {
                in.rfinish();
                return false;
            }
            raw |= uint64(guid[index]) << (8 * index);
        }
        if (raw == 0 || in.rpos() != in.size())
        {
            in.rfinish();
            return false;
        }

        parsed.bankGuid = ObjectGuid(raw);
        out = parsed;
        return true;
    }

    /// A plain byte, then one mask byte, then the present bytes of a packed
    /// eight-byte value. Two unrelated opcodes share this exact shape at 18414
    /// and differ only in their orders, so the walk is written once.
    inline uint64 ReadPrefixedPackedValue(WorldPacket& in, uint8& prefix,
        uint8 const (&maskOrder)[8], uint8 const (&byteOrder)[8])
    {
        in >> prefix;

        uint8 value[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        in.ResetBitReader();
        for (uint8 index = 0; index < 8; ++index)
        {
            value[maskOrder[index]] = in.ReadBit();
        }
        for (uint8 index = 0; index < 8; ++index)
        {
            in.ReadByteSeq(value[byteOrder[index]]);
        }

        uint64 raw = 0;
        for (uint8 index = 0; index < 8; ++index)
        {
            raw |= uint64(value[index]) << (8 * index);
        }
        return raw;
    }

    /// CMSG_TOTEM_DESTROYED (0x1263): the slot, then the totem's packed GUID.
    /// The inherited reader took the slot then a raw uint64 it discarded.
    inline ObjectGuid ReadTotemDestroyed(WorldPacket& in, uint8& slotId)
    {
        uint8 const maskOrder[] = { 4, 2, 1, 3, 0, 6, 7, 5 };
        uint8 const byteOrder[] = { 6, 2, 4, 1, 5, 0, 3, 7 };
        return ObjectGuid(ReadPrefixedPackedValue(in, slotId, maskOrder, byteOrder));
    }

    inline bool HasCanonicalTotemDestroyedGuidEncoding(WorldPacket const& in)
    {
        // Present GUID bytes are XOR-obfuscated. A wire byte of 0x01 decodes
        // to zero, which the retail writer represents by clearing its mask bit.
        for (size_t index = 2; index < in.size(); ++index)
        {
            if (in[index] == 0x01)
            {
                return false;
            }
        }
        return true;
    }

    inline bool IsTotemDestroyedRequestAdmissible(WorldPacket const& in, uint8 slotId)
    {
        return slotId < MAX_TOTEM_SLOT && in.rpos() == in.size() &&
               HasCanonicalTotemDestroyedGuidEncoding(in);
    }

    inline bool TotemDestroyedGuidMatches(ObjectGuid const& requestedGuid, ObjectGuid const& occupiedGuid)
    {
        return requestedGuid.IsEmpty() || requestedGuid == occupiedGuid;
    }

    /// CMSG_SET_ACTION_BUTTON (0x1F8C): the button, then a packed eight-byte
    /// value, byte-for-byte identical to the client's writer sub_669CAE.
    ///
    /// The value is NOT the pre-MoP packed uint32. The action occupies the full
    /// low 32 bits rather than 24, and the type is byte 7 -- so the inherited
    /// ACTION_BUTTON_ACTION and ACTION_BUTTON_TYPE macros, which split a uint32
    /// at bit 24, cut it in the wrong place. Bytes 4 to 6 belong to neither
    /// field, are never set on the wire, and are not read here.
    ///
    /// The client dispatches on the type's HIGH NIBBLE -- every one of its type
    /// predicates tests type & 0xF0 -- so callers should do the same rather than
    /// switch on the exact byte.
    ///
    /// There are MORE families than this tree's enum knows. The client tests at
    /// least 0x00, 0x10, 0x30, 0x40, 0x50 and 0x80 (sub_8B5DF1, sub_8B5EAF,
    /// sub_8B5EE0, sub_8B5E4D, sub_8B5E7E, sub_8B5E1C). An earlier note here said
    /// four, which undercounted. 0x10 and 0x50 have no entry in ActionButtonType
    /// and no observed body carries them, so their meaning is unrecovered.
    inline void ReadSetActionButton(WorldPacket& in, uint8& button,
        uint32& action, uint8& type)
    {
        uint8 const maskOrder[] = { 7, 0, 5, 2, 1, 6, 3, 4 };
        uint8 const byteOrder[] = { 6, 7, 3, 5, 2, 1, 4, 0 };
        uint64 const packed = ReadPrefixedPackedValue(in, button, maskOrder, byteOrder);
        action = uint32(packed & UINT64_C(0xFFFFFFFF));
        type = uint8((packed >> 56) & 0xFF);
    }

    /// SMSG_SEND_MAIL_RESULT (0x1A9B), the 18414 body.
    ///
    /// Six little-endian uint32, always 24 bytes. No bit packing, no mask, no
    /// GUID obfuscation:
    ///
    ///     mailId, equipError, mailError, mailAction, itemGuidLow, itemCount
    ///
    /// The inherited sender wrote mailId, mailAction, mailError and then made
    /// the last three CONDITIONAL, giving a 12, 16 or 20 byte body. Both the
    /// order and the shape were wrong.
    ///
    /// The field order is discriminated rather than merely consistent. Across
    /// all 1,317 bodies of this build, the set carrying a non-zero word at
    /// offset 4 is EXACTLY the set carrying 1 at offset 8 -- three bodies -- which
    /// reproduces the rule that an equip error is only meaningful when the mail
    /// error is MAIL_ERR_EQUIP_ERROR. Swapping the two would put 50 in a field
    /// whose range stops around 21. Likewise itemGuidLow and itemCount cannot
    /// swap: a count of 965 million is not a count.
    inline void BuildSendMailResult(WorldPacket& out, uint32 mailId,
        uint32 equipError, uint32 mailError, uint32 mailAction,
        uint32 itemGuidLow, uint32 itemCount)
    {
        out << uint32(mailId);
        out << uint32(equipError);
        out << uint32(mailError);
        out << uint32(mailAction);
        out << uint32(itemGuidLow);
        out << uint32(itemCount);
    }

    inline void BuildAttackStart(WorldPacket& out, uint64 attackerGuid,
        uint64 victimGuid)
    {
        out.Initialize(SMSG_ATTACKSTART, 18);
        out.WriteBit(AttackGuidByte(victimGuid, 7) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 7) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 3) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 3) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 5) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 4) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 1) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 4) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 0) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 6) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 5) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 2) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 6) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 1) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 2) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 0) != 0);
        out.FlushBits();

        out.WriteByteSeq(AttackGuidByte(attackerGuid, 5));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 0));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 5));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 4));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 6));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 6));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 1));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 0));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 7));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 4));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 2));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 3));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 7));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 2));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 3));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 1));
    }

    inline void BuildAttackStop(WorldPacket& out, uint64 attackerGuid,
        uint64 victimGuid, bool hasVictimContext)
    {
        out.Initialize(SMSG_ATTACKSTOP, 19);
        out.WriteBit(AttackGuidByte(attackerGuid, 5) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 6) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 3) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 6) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 7) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 2) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 5) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 4) != 0);
        out.WriteBit(hasVictimContext);
        out.WriteBit(AttackGuidByte(attackerGuid, 3) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 0) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 2) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 7) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 4) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 1) != 0);
        out.WriteBit(AttackGuidByte(victimGuid, 0) != 0);
        out.WriteBit(AttackGuidByte(attackerGuid, 1) != 0);
        out.FlushBits();

        out.WriteByteSeq(AttackGuidByte(attackerGuid, 0));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 3));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 5));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 2));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 0));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 6));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 3));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 4));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 1));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 4));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 6));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 5));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 7));
        out.WriteByteSeq(AttackGuidByte(victimGuid, 2));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 1));
        out.WriteByteSeq(AttackGuidByte(attackerGuid, 7));
    }

    inline void BuildCancelAutoRepeat(WorldPacket& out, uint64 targetGuid)
    {
        // The 18414 reader sub_6FA553 consumes one packed unit GUID. Its
        // Unit_C terminal clears that unit's auto-repeat/ranged state.
        ObjectGuid guid(targetGuid);
        out.Initialize(SMSG_CANCEL_AUTO_REPEAT, 9);
        out.WriteGuidMask<1, 3, 0, 4, 6, 7, 5, 2>(guid);
        out.WriteGuidBytes<7, 6, 2, 5, 0, 4, 1, 3>(guid);
    }

    inline void BuildPartyKillLog(WorldPacket& out, ObjectGuid killer,
        ObjectGuid victim)
    {
        out.Initialize(SMSG_PARTYKILLLOG, 18);

        // Reader sub_6F2FE4 in Wow.exe 18414 decodes two packed GUIDs into adjacent buffers:
        // slot A at this+16..23 and slot B at this+24..31. Its 16-bit mask order is
        //   B7 B2 A1 B4 A2 A5 B3 B1 B0 A3 A0 A4 B6 A7 B5 A6
        // which is exactly the interleaving written below, so the layout here is faithful.
        //
        // SLOT B IS THE KILLER, slot A the victim, and that is established from the binary:
        //
        //   dispatch  sub_659694 case 176 (0xB0) builds the message through sub_706C0A, which
        //             installs off_D6AB30 and calls the reader, then calls sub_6C4FBA
        //   terminal  sub_6C4FBA reaches its handler through a computed call:
        //               0xCE6A6758 + 0xD283DFE6 - 0xA06A2BBB = 0x00841B83
        //             landing on a push ebp/mov ebp,esp at .text:00841B83
        //   roles     that handler copies slot B ([eax+18h]) and slot A ([eax+10h]) into
        //             sub_8413FA, which routes B to event+0x18 and A to event+0x30; sub_840352
        //             then pushes those as COMBAT_LOG_EVENT sourceGUID and destGUID respectively
        //   subevent  0x2B = 43, and off_F58AD0[43] is "PARTY_KILL"
        //
        // Corroborated independently: the terminal resolves SLOT A through a path that sets the
        // twelfth PARTY_KILL argument, unconsciousOnDeath -- a property of the unit that died.
        //
        // A note on the citation, because this comment has now been wrong in both directions.
        // An earlier revision cited "terminal sub_841B83". No such SYMBOL exists: IDA defines no
        // function at 0x841B83 (sub_841A86 ends at .text:00841B82, the next proc is sub_841DF7)
        // because the only caller is the computed call above. A later revision concluded from that
        // absence that the citation was fabricated and set the roles from observed behaviour
        // instead -- also wrong. The ADDRESS was always correct; only the name never existed.
        // Searching Wow.exe.c finds defined functions only, and searching Wow.exe.asm for an
        // address finds nothing because that export carries no address column at all. Use
        // Wow.exe.lst, which does: the code is at Wow.exe.lst:2111029.
        //
        // off_D6AB30 is { sub_6C4038, nullsub_2, nullsub_2, sub_708A54, sub_7677FF }. Slot 3
        // tail-calls sub_6F2B7B, a schema-identical second copy of this reader, which corroborates
        // the layout but does not resolve the roles.
        out.WriteGuidMask<7, 2>(killer);
        out.WriteGuidMask<1>(victim);
        out.WriteGuidMask<4>(killer);
        out.WriteGuidMask<2, 5>(victim);
        out.WriteGuidMask<3, 1, 0>(killer);
        out.WriteGuidMask<3, 0, 4>(victim);
        out.WriteGuidMask<6>(killer);
        out.WriteGuidMask<7>(victim);
        out.WriteGuidMask<5>(killer);
        out.WriteGuidMask<6>(victim);
        out.FlushBits();

        out.WriteGuidBytes<0, 5>(killer);
        out.WriteGuidBytes<0, 2>(victim);
        out.WriteGuidBytes<7, 6, 1, 4>(killer);
        out.WriteGuidBytes<4, 1>(victim);
        out.WriteGuidBytes<2>(killer);
        out.WriteGuidBytes<6, 3, 5, 7>(victim);
        out.WriteGuidBytes<3>(killer);
    }

    struct AttackStateUpdateData
    {
        uint32 hitInfo = 0;
        ObjectGuid attacker;
        ObjectGuid target;
        uint32 damage = 0;
        uint32 overkill = 0;
        uint32 schoolMask = 0;
        uint32 absorb = 0;
        uint32 resist = 0;
        uint8 victimState = 0;
        uint32 blocked = 0;
    };

    inline void BuildAttackerStateUpdate(WorldPacket& out,
        AttackStateUpdateData const& update)
    {
        // The 18414 route first reads an optional-metadata bit and a byte length,
        // then gives that exact slice to the UnitCombat_C record reader. The
        // normal server path has no optional metadata and one sub-damage record.
        uint32 const absorbMask = 0x00000060u;
        uint32 const resistMask = 0x00000180u;
        uint32 const trailingFloatMask = 0x00003000u;
        uint32 const blockMask = 0x00002000u;
        uint32 const rageMask = 0x00800000u;
        uint32 const extendedMask = 0x00000001u;

        ByteBuffer body(96);
        body << update.hitInfo;
        body.appendPackGUID(update.attacker.GetRawValue());
        body.appendPackGUID(update.target.GetRawValue());
        body << update.damage;
        body << update.overkill;
        body << uint8(1);
        body << update.schoolMask;
        body << float(update.damage);
        body << update.damage;

        if (update.hitInfo & absorbMask)
            body << update.absorb;
        if (update.hitInfo & resistMask)
            body << update.resist;

        body << update.victimState;
        body << uint32(0); // unknown combat context
        body << uint32(0); // melee spell id

        if (update.hitInfo & blockMask)
            body << update.blocked;
        if (update.hitInfo & rageMask)
            body << uint32(0);
        if (update.hitInfo & extendedMask)
        {
            body << uint32(0);
            for (uint8 i = 0; i < 12; ++i)
                body << float(0);
            body << uint32(0);
        }
        if (update.hitInfo & trailingFloatMask)
            body << float(0);

        out.Initialize(SMSG_ATTACKERSTATEUPDATE, 5 + body.size());
        out.WriteBit(false);
        out.FlushBits();
        out << uint32(body.size());
        out.append(body);
    }

    inline void BuildAIReaction(WorldPacket& out, ObjectGuid guid, uint32 reaction)
    {
        // The 18414 Unit_C reader interleaves the reaction value between the
        // first three and final five packed-GUID bytes.
        out.Initialize(SMSG_AI_REACTION, 13);
        out.WriteGuidMask<5, 7, 0, 4, 6, 2, 3, 1>(guid);
        out.WriteGuidBytes<4, 6, 5>(guid);
        out << reaction;
        out.WriteGuidBytes<7, 1, 2, 0, 3>(guid);
    }

    inline void BuildPowerUpdate(WorldPacket& out, ObjectGuid guid,
        uint8 powerType, uint32 value)
    {
        // The 18414 Unit_C reader consumes eight GUID mask bits followed by a
        // 21-bit record count. Each record is one power selector and value;
        // GUID byte 6 trails the records rather than preceding them.
        out.Initialize(SMSG_POWER_UPDATE, 17);
        out.WriteGuidMask<4, 6, 7, 5, 2, 3, 0, 1>(guid);
        out.WriteBits(uint32(1), 21);
        out.WriteGuidBytes<7, 0, 5, 3, 1, 2, 4>(guid);
        out << powerType;
        out << value;
        out.WriteGuidBytes<6>(guid);
    }

    inline void BuildPetActionSound(WorldPacket& out, ObjectGuid guid,
        uint32 action)
    {
        // The 18414 Unit_C reader places the pet action selector between two
        // packed-GUID byte phases. Selectors 0 and 1 choose the special-spell
        // and attack vocalizations respectively.
        out.Initialize(SMSG_PET_ACTION_SOUND, 13);
        out.WriteGuidMask<2, 7, 6, 0, 5, 1, 3, 4>(guid);
        out.WriteGuidBytes<7, 4, 6, 1>(guid);
        out << action;
        out.WriteGuidBytes<2, 3, 5, 0>(guid);
    }

    inline void BuildPetActionFeedback(WorldPacket& out, uint8 feedback,
        uint32 spellId = 0)
    {
        // The 18414 reader uses an inverse presence bit: one omits the spell
        // context, while zero appends it after the feedback selector.
        out.Initialize(SMSG_PET_ACTION_FEEDBACK, spellId ? 6 : 2);
        out.WriteBit(spellId == 0);
        out.FlushBits();
        out << feedback;
        if (spellId != 0)
            out << spellId;
    }

    inline uint8 SwimSpeedGuidByte(uint64 guid, uint8 index)
    {
        return uint8(guid >> (8 * index));
    }

    /// The can-fly family, all four opcodes, from their client readers.
    ///
    /// The mover pair carries a uint32 counter interleaved into the GUID byte
    /// run at a fixed point; the observer pair carries nothing but the GUID.
    /// Body is 1 + popcount(mask) [+ 4] and there is no other field.
    ///
    /// Every one of the four was wrong before. SET had the mask and byte orders
    /// permuted, which a length check cannot see. UNSET was worse: it wrote two
    /// GUID bytes before the counter where the client reads THREE, so the client
    /// takes a GUID byte as the counter's low byte and then over-reads. The total
    /// length matched either way, which is exactly why it survived.
    ///
    /// Verified against 28 real bodies across 11 distinct presence masks.
    inline void BuildMoveSetCanFly(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 6, 1, 4, 0, 3, 7, 5, 2 };
        uint8 const beforeCounter[] = { 4, 2 };
        uint8 const afterCounter[] = { 6, 3, 1, 0, 7, 5 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        for (uint8 index : afterCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildMoveUnsetCanFly(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 6, 5, 0, 4, 3, 7, 2, 1 };
        uint8 const beforeCounter[] = { 4, 5, 7 };
        uint8 const afterCounter[] = { 6, 2, 3, 1, 0 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        for (uint8 index : afterCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    /// Water walking, mover halves. Readers sub_C8F544 (0x1F9A) and sub_C8DFF2
    /// (0x086A), reached through the movement dispatcher sub_C80E74 at cases 314
    /// and 410; the dispatcher's case formula was validated against two opcodes
    /// whose readers are already pinned in this file before being trusted here.
    ///
    /// Both replace inherited orders that were wrong in every field. The old
    /// bodies were the right LENGTH, so nothing caught them: this family is a
    /// mask byte, some XOR-1 GUID bytes and a uint32 in some order, and every
    /// permutation of that occupies the same space.
    inline void BuildMoveWaterWalk(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 2, 0, 4, 5, 3, 7, 1, 6 };
        uint8 const byteOrder[] = { 4, 7, 0, 1, 6, 2, 3, 5 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
    }

    /// Land walk splits its GUID around the counter: byte 5 is written AFTER the
    /// uint32, alone. That is the reader's order, not a guess -- but note that
    /// byte 5 is absent from every captured body, because the GUIDs observed all
    /// have a zero there, so the fixtures below cannot exercise that arm. It
    /// rests on sub_C8DFF2 alone.
    inline void BuildMoveLandWalk(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 0, 7, 3, 1, 6, 5, 2, 4 };
        uint8 const beforeCounter[] = { 7, 6, 4, 3, 2, 0, 1 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, 5));
    }

    /// Falling, mover halves. Readers sub_C8BE56 (0x0C60) and sub_C898EA (0x08E0).
    ///
    /// These two put the counter in different places, and neither matches the
    /// water-walk pair, so there is no family-wide rule to lean on. Feather fall
    /// reads the counter immediately after the mask, before any GUID byte. Normal
    /// fall reads GUID bytes 3 and 2 first, THEN the counter, then the rest --
    /// which means the counter's byte offset is variable, depending on how many
    /// of those two bytes are present.
    inline void BuildMoveFeatherFall(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 4, 1, 7, 3, 0, 5, 2, 6 };
        uint8 const byteOrder[] = { 1, 0, 5, 4, 6, 3, 2, 7 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        out << uint32(counter);
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildMoveNormalFall(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 3, 1, 6, 0, 4, 7, 2, 5 };
        uint8 const beforeCounter[] = { 3, 2 };
        uint8 const afterCounter[] = { 1, 5, 4, 7, 0, 6 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        for (uint8 index : afterCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    /// Hovering. Readers sub_C89BBE (0x1802), sub_C8C65B (0x02D3),
    /// sub_C8EFCF (0x0258) and sub_C8AC47 (0x0CE1).
    ///
    /// The mover pair is ASYMMETRIC on the scalar: SET writes it after two GUID
    /// bytes, UNSET writes it second-to-last. Assuming a family is symmetric is
    /// how the inherited builders came to be wrong in every position, so the two
    /// are written out separately rather than parameterised.
    ///
    /// All four inherited orders were wrong. The old ones decode 0 of 51 real
    /// bodies to a plausible high-GUID class.
    inline void BuildMoveSetHover(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 7, 1, 0, 4, 2, 5, 6, 3 };
        uint8 const beforeCounter[] = { 1, 6 };
        uint8 const afterCounter[] = { 2, 4, 3, 5, 0, 7 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        for (uint8 index : afterCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildMoveUnsetHover(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 3, 5, 6, 0, 1, 2, 7, 4 };
        uint8 const beforeCounter[] = { 6, 4, 5, 3, 2, 1, 0 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, 7));
    }

    inline void BuildSplineMoveSetHover(WorldPacket& out, uint64 moverGuid)
    {
        uint8 const maskOrder[] = { 6, 5, 1, 3, 0, 4, 7, 2 };
        uint8 const byteOrder[] = { 7, 1, 0, 6, 3, 2, 5, 4 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildSplineMoveUnsetHover(WorldPacket& out, uint64 moverGuid)
    {
        uint8 const maskOrder[] = { 3, 1, 5, 7, 4, 6, 2, 0 };
        uint8 const byteOrder[] = { 3, 5, 2, 1, 6, 7, 4, 0 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    /// Rooting. Readers sub_C6915A (0x15AE), sub_C8A419 (0x1FAE),
    /// sub_C8CED2 (0x0728) and sub_C8B308 (0x01E1).
    ///
    /// The mover pair splits its GUID around the counter in opposite proportions:
    /// ROOT writes six bytes before and two after, UNROOT two before and six
    /// after. Both are unconditional -- the scalar is always present -- so the
    /// split point is what a wrong layout gets wrong while keeping the length.
    /// Real bodies at two different masks pin it.
    inline void BuildForceMoveRoot(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 0, 3, 4, 1, 5, 2, 6, 7 };
        uint8 const beforeCounter[] = { 4, 7, 1, 2, 6, 5 };
        uint8 const afterCounter[] = { 0, 3 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        for (uint8 index : afterCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildForceMoveUnroot(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 3, 5, 7, 1, 0, 2, 4, 6 };
        uint8 const beforeCounter[] = { 0, 7 };
        uint8 const afterCounter[] = { 5, 4, 2, 1, 3, 6 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        for (uint8 index : afterCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    /// The observer halves read no scalar at all -- neither reader contains the
    /// uint32 primitive, so the body is GUID and nothing else.
    inline void BuildSplineMoveRoot(WorldPacket& out, uint64 moverGuid)
    {
        uint8 const maskOrder[] = { 3, 7, 2, 4, 5, 6, 0, 1 };
        uint8 const byteOrder[] = { 2, 4, 5, 7, 1, 0, 3, 6 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildSplineMoveUnroot(WorldPacket& out, uint64 moverGuid)
    {
        uint8 const maskOrder[] = { 1, 5, 2, 0, 3, 6, 4, 7 };
        uint8 const byteOrder[] = { 2, 7, 1, 3, 5, 0, 4, 6 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    /// Gravity. Readers sub_6D75A9 (0x159F), sub_C8DD60 (0x0A27),
    /// sub_C8D6DA (0x0845) and sub_C8AAE3 (0x0865). The first sits outside the
    /// C8xxxx reader cluster, which looked like a routing error and is not --
    /// the 64-bit client places it out-of-cluster too.
    ///
    /// The SENSE was not decidable from the binary -- no "gravity" strings in
    /// either image, no boolean in the readers, generic obfuscated
    /// post-handlers -- so it was settled by live test instead. levitate-on selects DISABLE, and that is CONFIRMED against a live 18414
    /// client: the mover floats and holds altitude, the client refuses to jump
    /// locally (it stops sending CMSG_MOVE_JUMP entirely rather than having one
    /// rejected), and it acknowledges each packet with the matching ack --
    /// 0x09D3 for DISABLE, 0x11D8 for ENABLE.
    ///
    /// Live capture, GUID 0x1, counters 4 then 5:
    ///     .levitate on   0x159F  01 00 04 00 00 00
    ///     .levitate off  0x0A27  40 00 05 00 00 00
    /// Both consumed exactly by an independent decoder.
    ///
    /// This tree also disagreed with itself: Creature::SetLevitate mapped
    /// levitate-on to DISABLE while Unit::BuildMoveLevitatePacket mapped it to
    /// ENABLE, so mover and observers were told opposite things. Creature had it
    /// right.
    inline void BuildMoveGravityDisable(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 6, 1, 3, 7, 4, 2, 5, 0 };
        uint8 const beforeCounter[] = { 5, 2, 1, 6, 0 };
        uint8 const afterCounter[] = { 3, 4, 7 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        for (uint8 index : afterCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildMoveGravityEnable(WorldPacket& out, uint64 moverGuid, uint32 counter)
    {
        uint8 const maskOrder[] = { 3, 0, 7, 1, 5, 2, 6, 4 };
        uint8 const beforeCounter[] = { 3, 2, 1, 7, 6, 0, 4 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : beforeCounter) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
        out << uint32(counter);
        out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, 5));
    }

    inline void BuildSplineMoveGravityDisable(WorldPacket& out, uint64 moverGuid)
    {
        uint8 const maskOrder[] = { 1, 7, 4, 5, 6, 0, 2, 3 };
        uint8 const byteOrder[] = { 3, 4, 5, 6, 0, 1, 2, 7 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildSplineMoveGravityEnable(WorldPacket& out, uint64 moverGuid)
    {
        uint8 const maskOrder[] = { 5, 7, 4, 2, 3, 6, 1, 0 };
        uint8 const byteOrder[] = { 6, 3, 2, 4, 1, 5, 7, 0 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    /// The observer halves. No counter at all -- the mask and the bytes are the
    /// whole body. Landing these with the mover pair is the point: admitting one
    /// side alone is what left four other movement states telling everyone except
    /// the player they applied to.
    inline void BuildSplineMoveSetFlying(WorldPacket& out, uint64 moverGuid)
    {
        uint8 const maskOrder[] = { 4, 1, 2, 0, 7, 5, 3, 6 };
        uint8 const byteOrder[] = { 4, 7, 1, 0, 3, 5, 6, 2 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }

    inline void BuildSplineMoveUnsetFlying(WorldPacket& out, uint64 moverGuid)
    {
        uint8 const maskOrder[] = { 1, 5, 7, 2, 6, 3, 0, 4 };
        uint8 const byteOrder[] = { 2, 5, 4, 6, 1, 0, 7, 3 };

        for (uint8 index : maskOrder) { out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0); }
        out.FlushBits();
        for (uint8 index : byteOrder) { out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index)); }
    }


    /// SMSG_MOVE_SET_WALK_SPEED (0x0469), from reader sub_C8F849 (dispatcher
    /// sub_C80E74, case 405).
    ///
    /// Verified against capture-000004 seq 23263: 14 bytes consuming exactly,
    /// yielding guid 0x04000000053CC8E8, counter 497, speed 1.25. That is the
    /// same mover as the run-speed bodies from the same capture, decoded under a
    /// completely different interleave, which is a useful cross-check that the
    /// per-opcode layouts really are distinct rather than a misreading.
    ///
    /// Walk puts TWO GUID bytes before the counter where run puts one and swim
    /// puts none; there is no shared template across this family.
    inline void BuildMoveSetWalkSpeed(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float speed)
    {
        uint8 const maskOrder[] = { 6, 7, 3, 1, 2, 0, 4, 5 };
        uint8 const beforeCounter[] = { 5, 6 };
        uint8 const beforeSpeed[] = { 4 };
        uint8 const afterSpeed[] = { 2, 3, 0, 1, 7 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeCounter)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << uint32(counter);
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_SPLINE_MOVE_SET_RUN_SPEED (0x02F1), from reader sub_C8C923.
    ///
    /// This is the observer broadcast, and unlike every direct speed packet it
    /// carries NO counter -- only the mover and the speed.
    ///
    /// Verified against capture-000004 seq 2506: 12 bytes consuming exactly,
    /// yielding guid 0xF1308319002275D5 (high 0xF13, HIGHGUID_UNIT) and speed
    /// 4.85.
    /// The spline speed broadcasts. Like spline-run these carry NO counter,
    /// only the mover and the speed, and each has its own interleave.
    ///
    /// All four are pinned to retail bodies from capture-000004 at catalogue
    /// 2BE10C89 -- sequences 2510, 2507, 2508 and 2509 -- which are consecutive
    /// packets for one mover, 0xF1308319002275D5. Their speeds are each exactly
    /// half the base for that movement type (walk 1.25 of 2.5, run-back 2.25 of
    /// 4.5, swim 2.36111 of 4.72222), which is that creature uniformly slowed and
    /// a useful independent check that the layouts are not merely self-consistent.

    /// SMSG_SPLINE_MOVE_SET_WALK_SPEED (0x08B2), reader sub_C8A6C4, case 28.
    inline void BuildSplineMoveSetWalkSpeed(WorldPacket& out, uint64 moverGuid, float speed)
    {
        uint8 const maskOrder[] = { 4, 1, 7, 6, 3, 2, 5, 0 };
        uint8 const byteOrder[] = { 2, 3, 1, 0, 6, 5, 4, 7 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : byteOrder)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
    }

    /// SMSG_SPLINE_MOVE_SET_RUN_BACK_SPEED (0x1F9F), reader sub_C8BBFE, case 319.
    inline void BuildSplineMoveSetRunBackSpeed(WorldPacket& out, uint64 moverGuid, float speed)
    {
        uint8 const maskOrder[] = { 7, 4, 0, 3, 2, 5, 6, 1 };
        uint8 const beforeSpeed[] = { 6, 4, 1, 5, 2, 3, 7 };
        uint8 const afterSpeed[] = { 0 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_SPLINE_MOVE_SET_SWIM_SPEED (0x1D8E), reader sub_C8CD2E, case 278.
    inline void BuildSplineMoveSetSwimSpeed(WorldPacket& out, uint64 moverGuid, float speed)
    {
        uint8 const maskOrder[] = { 5, 6, 7, 3, 4, 2, 1, 0 };
        uint8 const beforeSpeed[] = { 4, 1, 6, 7, 3 };
        uint8 const afterSpeed[] = { 5, 0, 2 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_SPLINE_MOVE_SET_FLIGHT_SPEED (0x1DAB), reader sub_C8E89B, case 291.
    /// The speed leads, before the mask byte -- as with the direct flight packet.
    inline void BuildSplineMoveSetFlightSpeed(WorldPacket& out, uint64 moverGuid, float speed)
    {
        uint8 const maskOrder[] = { 1, 4, 7, 3, 2, 6, 5, 0 };
        uint8 const byteOrder[] = { 5, 1, 0, 6, 2, 4, 7, 3 };

        out << float(speed);
        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : byteOrder)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    inline void BuildSplineMoveSetRunSpeed(WorldPacket& out, uint64 moverGuid,
        float speed)
    {
        uint8 const maskOrder[] = { 3, 0, 1, 4, 7, 5, 6, 2 };
        uint8 const beforeSpeed[] = { 4 };
        uint8 const afterSpeed[] = { 1, 5, 3, 7, 6, 2, 0 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// 4.86.
    /// The four remaining spline speed broadcasts. These are derived from their
    /// client readers alone: the corpus carries no observation of any of them at
    /// 18414, so unlike the group above none can be pinned to a retail body.
    ///
    /// They are nonetheless ADMITTED. The absence of a capture is a gap in the
    /// sniff corpus, not evidence against the layout, and suppressing the packet
    /// is a certain failure where sending a reader-correct body is at worst an
    /// uncertain one. Both mask order and byte order are pinned by test, so a
    /// transposition cannot pass silently.
    ///
    /// The opcode-to-reader mapping was resolved through the dispatcher
    /// sub_C80E74, whose case arithmetic reproduces all four cases of the pinned
    /// group above exactly, and each case was then read through to its reader.

    /// SMSG_SPLINE_MOVE_SET_SWIM_BACK_SPEED (0x0046), reader sub_C8BFCE, case 418.
    inline void BuildSplineMoveSetSwimBackSpeed(WorldPacket& out, uint64 moverGuid, float speed)
    {
        uint8 const maskOrder[] = { 2, 6, 5, 0, 4, 3, 1, 7 };
        uint8 const byteOrder[] = { 7, 6, 5, 3, 2, 4, 1, 0 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : byteOrder)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
    }

    /// SMSG_SPLINE_MOVE_SET_TURN_RATE (0x0832), reader sub_C8C7C7, case 12.
    inline void BuildSplineMoveSetTurnRate(WorldPacket& out, uint64 moverGuid, float speed)
    {
        uint8 const maskOrder[] = { 5, 7, 4, 0, 1, 6, 3, 2 };
        uint8 const beforeSpeed[] = { 1, 7 };
        uint8 const afterSpeed[] = { 6, 0, 4, 2, 5, 3 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_SPLINE_MOVE_SET_FLIGHT_BACK_SPEED (0x0B28), reader sub_C8B0B4, case 184.
    inline void BuildSplineMoveSetFlightBackSpeed(WorldPacket& out, uint64 moverGuid, float speed)
    {
        uint8 const maskOrder[] = { 6, 0, 2, 7, 5, 4, 3, 1 };
        uint8 const beforeSpeed[] = { 7, 6, 4 };
        uint8 const afterSpeed[] = { 1, 3, 2, 0, 5 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_SPLINE_MOVE_SET_PITCH_RATE (0x0AB3), reader sub_C8ED13, case 61.
    inline void BuildSplineMoveSetPitchRate(WorldPacket& out, uint64 moverGuid, float speed)
    {
        uint8 const maskOrder[] = { 2, 6, 0, 5, 1, 3, 7, 4 };
        uint8 const beforeSpeed[] = { 5 };
        uint8 const afterSpeed[] = { 4, 0, 3, 6, 1, 2, 7 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_MOVE_SET_RUN_BACK_SPEED (0x0A83), reader sub_C8977A, case 49.
    /// Pinned to capture-000004 seq 23260: 14 bytes consuming exactly, guid
    /// 0x04000000053CC8E8, counter 494, speed 2.25.
    inline void BuildMoveSetRunBackSpeed(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float speed)
    {
        uint8 const maskOrder[] = { 7, 1, 0, 2, 4, 3, 6, 5 };
        uint8 const beforeSpeed[] = { 0, 3, 7, 5, 2, 4, 1 };
        uint8 const afterSpeed[] = { 6 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        out << uint32(counter);
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_MOVE_SET_FLIGHT_SPEED (0x006E), reader sub_C8A820, case 482.
    ///
    /// The odd one out: the float and the counter are written BEFORE the mask
    /// byte, so this cannot reuse the mask-first shape every sibling has.
    ///
    /// Pinned to capture-000004 seq 582: 14 bytes consuming exactly, guid
    /// 0x04000000053CC8E8, counter 68, speed 31.57.
    inline void BuildMoveSetFlightSpeed(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float speed)
    {
        uint8 const maskOrder[] = { 6, 5, 0, 4, 1, 7, 3, 2 };
        uint8 const byteOrder[] = { 0, 7, 4, 5, 6, 2, 3, 1 };

        out << float(speed);                                // scalars lead here
        out << uint32(counter);
        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : byteOrder)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_MOVE_SET_TURN_RATE (0x0069), reader sub_C8B48D, case 401.
    /// Reads the float first and the counter almost last.
    /// NOT corpus-verified: zero observations at 18414. Ungated.
    inline void BuildMoveSetTurnRate(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float rate)
    {
        uint8 const maskOrder[] = { 6, 5, 1, 4, 0, 7, 3, 2 };
        uint8 const afterRate[] = { 7, 3, 5, 0, 4, 6, 2 };
        uint8 const afterCounter[] = { 1 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        out << float(rate);
        for (uint8 index : afterRate)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << uint32(counter);
        for (uint8 index : afterCounter)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_MOVE_SET_FLIGHT_BACK_SPEED (0x0319), reader sub_C8B650, case 149.
    /// NOT corpus-verified: zero observations at 18414. Ungated.
    inline void BuildMoveSetFlightBackSpeed(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float speed)
    {
        uint8 const maskOrder[] = { 2, 7, 6, 4, 0, 1, 5, 3 };
        uint8 const beforeCounter[] = { 4, 1, 6, 0, 2 };
        uint8 const beforeSpeed[] = { 7, 3, 5 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeCounter)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << uint32(counter);
        for (uint8 index : beforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
    }

    /// SMSG_MOVE_SET_PITCH_RATE (0x17AB), reader sub_C8E72B, case 259.
    /// NOT corpus-verified: zero observations at 18414. Ungated.
    inline void BuildMoveSetPitchRate(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float rate)
    {
        uint8 const maskOrder[] = { 7, 5, 4, 1, 6, 3, 2, 0 };
        uint8 const beforeCounter[] = { 4 };
        uint8 const beforeRate[] = { 2, 5, 6, 1 };
        uint8 const afterRate[] = { 7, 3, 0 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeCounter)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << uint32(counter);
        for (uint8 index : beforeRate)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(rate);
        for (uint8 index : afterRate)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_MOVE_SET_SWIM_BACK_SPEED (0x0962), from reader sub_C8AF44
    /// (dispatcher sub_C80E74, case 378).
    ///
    /// The body this replaced was broken on its face, independently of any
    /// client evidence: it wrote GUID byte 0 TWICE and byte 2 never, nine
    /// byte-writes for eight bytes.
    ///
    /// NOT corpus-verified. The layout is transcribed from the reader, but no
    /// retail body for this opcode has been decoded, so unlike walk, run and
    /// spline this one has no capture-pinned fixture and stays outside the send
    /// gate until it does.
    inline void BuildMoveSetSwimBackSpeed(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float speed)
    {
        uint8 const maskOrder[] = { 5, 0, 4, 2, 1, 3, 6, 7 };
        uint8 const beforeCounter[] = { 5, 6, 0, 4 };
        uint8 const afterSpeed[] = { 1, 7, 2, 3 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : beforeCounter)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << uint32(counter);                             // counter and speed
        out << float(speed);                                // are adjacent here
        for (uint8 index : afterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    /// SMSG_MOVE_SET_RUN_SPEED (0x184C).
    ///
    /// Recovered from the client's own reader, sub_C8B928, reached from the
    /// second-stage dispatcher sub_C80E74 at case index 444. Its bit and byte
    /// sequence is transcribed directly from that function; a reference fork's
    /// movement table agrees element for element, which is corroboration rather
    /// than the source.
    ///
    /// The interleave differs from swim, which is why the swim builder could not
    /// simply be reused: run writes one GUID byte BEFORE the counter, three
    /// between the counter and the speed, and four after it.
    ///
    /// Verified against six retail bodies (14, 15 and 16 byte cases) from
    /// captures 4 and 20 at catalogue 2BE10C89: every one consumes exactly, and
    /// the GUIDs that fall out carry the same high bytes as the movers those
    /// captures' name queries report -- 0x0400 for the creature, 0x0180 for the
    /// player.
    inline void BuildMoveSetRunSpeed(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float speed)
    {
        uint8 const maskOrder[] = { 1, 7, 4, 2, 5, 3, 6, 0 };
        uint8 const bytesBeforeCounter[] = { 1 };
        uint8 const bytesBeforeSpeed[] = { 7, 3, 0 };
        uint8 const bytesAfterSpeed[] = { 2, 4, 6, 5 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        for (uint8 index : bytesBeforeCounter)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << uint32(counter);
        for (uint8 index : bytesBeforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : bytesAfterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    inline void BuildMoveSetSwimSpeed(WorldPacket& out, uint64 moverGuid,
        uint32 counter, float speed)
    {
        uint8 const maskOrder[] = { 5, 0, 6, 3, 7, 2, 4, 1 };
        uint8 const bytesBeforeSpeed[] = { 1, 3 };
        uint8 const bytesAfterSpeed[] = { 6, 7, 0, 5, 2, 4 };

        for (uint8 index : maskOrder)
        {
            out.WriteBit(SwimSpeedGuidByte(moverGuid, index) != 0);
        }
        out.FlushBits();
        out << uint32(counter);
        for (uint8 index : bytesBeforeSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
        out << float(speed);
        for (uint8 index : bytesAfterSpeed)
        {
            out.WriteByteSeq(SwimSpeedGuidByte(moverGuid, index));
        }
    }

    inline void BuildSplineMoveSetNormalFall(WorldPacket& out, ObjectGuid guid)
    {
        out.WriteGuidMask<6, 1, 4, 5, 2, 7, 0, 3>(guid);
        out.WriteGuidBytes<7, 5, 1, 0, 6, 4, 2, 3>(guid);
    }

    inline void BuildSplineMoveSetRunMode(WorldPacket& out, ObjectGuid guid)
    {
        out.WriteGuidMask<5, 6, 2, 4, 7, 1, 3, 0>(guid);
        out.WriteGuidBytes<5, 1, 4, 0, 7, 3, 6, 2>(guid);
    }

    inline void BuildSplineMoveSetWalkMode(WorldPacket& out, ObjectGuid guid)
    {
        out.WriteGuidMask<4, 3, 0, 2, 1, 6, 5, 7>(guid);
        out.WriteGuidBytes<1, 4, 5, 6, 2, 0, 3, 7>(guid);
    }

    inline void BuildSplineMoveSetWaterWalk(WorldPacket& out, ObjectGuid guid)
    {
        out.WriteGuidMask<3, 1, 5, 6, 4, 0, 7, 2>(guid);
        out.WriteGuidBytes<4, 3, 6, 2, 1, 5, 7, 0>(guid);
    }

    inline void BuildSplineMoveSetFeatherFall(WorldPacket& out, ObjectGuid guid)
    {
        out.WriteGuidMask<1, 5, 6, 3, 7, 2, 4, 0>(guid);
        out.WriteGuidBytes<7, 1, 6, 4, 5, 3, 2, 0>(guid);
    }

    inline void BuildSplineMoveSetLandWalk(WorldPacket& out, ObjectGuid guid)
    {
        out.WriteGuidMask<1, 5, 6, 0, 7, 2, 3, 4>(guid);
        out.WriteGuidBytes<1, 6, 4, 3, 7, 0, 2, 5>(guid);
    }

    inline void BuildDismount(WorldPacket& out, ObjectGuid guid)
    {
        // Wow.exe 18414 reader helper sub_6D3AD4 consumes one packed unit
        // GUID; terminal sub_82E6E0 applies a zero mount state to that unit.
        out.Initialize(SMSG_DISMOUNT, 9);
        out.WriteGuidMask<6, 3, 0, 7, 1, 2, 5, 4>(guid);
        out.WriteGuidBytes<3, 6, 7, 5, 1, 4, 2, 0>(guid);
    }

    inline void BuildShowBank(WorldPacket& out, ObjectGuid guid)
    {
        // SMSG_SHOW_BANK (0x0007). The 32- and 64-bit 18414 readers agree on
        // this packed banker GUID, and the focused fixtures pin both observed
        // retail masks. The inherited flat uint64 body could not be parsed.
        out.Initialize(SMSG_SHOW_BANK, 9);
        out.WriteGuidMask<2, 4, 3, 6, 5, 1, 7, 0>(guid);
        out.WriteGuidBytes<7, 0, 5, 3, 6, 1, 4, 2>(guid);
    }

    inline void BuildPreResurrect(WorldPacket& out, ObjectGuid guid)
    {
        // Wow.exe 18414 parser sub_709F6B (dispatcher sub_659694 case 696,
        // the dense selector for 0x19C0) constructs the message and reads one
        // packed GUID and nothing else. Two independent readers agree on the
        // order: the constructor's sub_6E7875 and the class's virtual
        // deserialize slot sub_6D6EF4.
        //
        // The consumer is reached only through the per-message Arxan guard
        // trampoline sub_6D1F55, whose target is assembled at runtime, so the
        // GUID's role is NOT binary-proved. It is the repopping player's own
        // GUID here only because that is what the inherited sender already
        // supplied; this conversion changes the encoding, not the semantics.
        out.Initialize(SMSG_PRE_RESURRECT, 9);
        out.WriteGuidMask<1, 7, 5, 2, 6, 0, 3, 4>(guid);
        out.WriteGuidBytes<5, 1, 7, 0, 6, 4, 2, 3>(guid);
    }
}

namespace MopThreatPackets
{
    struct ThreatEntry
    {
        ObjectGuid target;
        uint32 threat = 0;
    };

    using ThreatEntries = std::vector<ThreatEntry>;

    inline void BuildUpdate(WorldPacket& out, ObjectGuid owner,
        ThreatEntries const& entries)
    {
        // Wow.exe 18414 reader sub_7344A4 consumes a 21-bit threat count,
        // per-target packed GUIDs and values, then the threatened unit GUID.
        MANGOS_ASSERT(entries.size() < (uint32(1) << 21));
        out.Initialize(SMSG_THREAT_UPDATE, 12 + entries.size() * 13);
        out.WriteGuidMask<5, 6, 1, 3, 7, 0, 4>(owner);
        out.WriteBits(uint32(entries.size()), 21);
        for (ThreatEntry const& entry : entries)
            out.WriteGuidMask<2, 3, 6, 5, 1, 4, 0, 7>(entry.target);
        out.WriteGuidMask<2>(owner);
        out.FlushBits();

        for (ThreatEntry const& entry : entries)
        {
            out.WriteGuidBytes<6, 7, 0, 1, 2, 5, 3, 4>(entry.target);
            out << entry.threat;
        }
        out.WriteGuidBytes<1, 4, 2, 3, 5, 6, 0, 7>(owner);
    }

    inline void BuildHighest(WorldPacket& out, ObjectGuid owner,
        ObjectGuid selected, ThreatEntries const& entries)
    {
        // Wow.exe 18414 reader sub_736527 interleaves the selected target,
        // threatened unit and the same 21-bit threat-list representation.
        MANGOS_ASSERT(entries.size() < (uint32(1) << 21));
        out.Initialize(SMSG_HIGHEST_THREAT_UPDATE, 21 + entries.size() * 13);
        out.WriteGuidMask<3, 0>(selected);
        out.WriteGuidMask<3, 6, 1>(owner);
        out.WriteGuidMask<5, 1, 6>(selected);
        out.WriteGuidMask<2, 5>(owner);
        out.WriteGuidMask<7, 4>(selected);
        out.WriteGuidMask<4>(owner);
        out.WriteBits(uint32(entries.size()), 21);
        for (ThreatEntry const& entry : entries)
            out.WriteGuidMask<6, 1, 0, 2, 7, 4, 3, 5>(entry.target);
        out.WriteGuidMask<7, 0>(owner);
        out.WriteGuidMask<2>(selected);
        out.FlushBits();

        out.WriteGuidBytes<4>(owner);
        for (ThreatEntry const& entry : entries)
        {
            out.WriteGuidBytes<6>(entry.target);
            out << entry.threat;
            out.WriteGuidBytes<4, 0, 3, 5, 2, 1, 7>(entry.target);
        }
        out.WriteGuidBytes<3>(selected);
        out.WriteGuidBytes<5>(owner);
        out.WriteGuidBytes<2>(selected);
        out.WriteGuidBytes<1, 0, 2>(owner);
        out.WriteGuidBytes<6, 1>(selected);
        out.WriteGuidBytes<7>(owner);
        out.WriteGuidBytes<0, 4, 7>(selected);
        out.WriteGuidBytes<3, 6>(owner);
        out.WriteGuidBytes<5>(selected);
    }

    inline void BuildClear(WorldPacket& out, ObjectGuid owner)
    {
        // Reader sub_6F2392 and terminal sub_820714 identify this GUID as the
        // unit whose client-side threat state is cleared.
        out.Initialize(SMSG_THREAT_CLEAR, 9);
        out.WriteGuidMask<6, 7, 4, 5, 2, 1, 0, 3>(owner);
        out.WriteGuidBytes<7, 0, 4, 3, 2, 1, 6, 5>(owner);
    }

    inline void BuildRemove(WorldPacket& out, ObjectGuid owner,
        ObjectGuid removed)
    {
        // Reader sub_6DBFD5 gives terminal sub_8206E1 the threatened unit
        // first and the removed target second.
        out.Initialize(SMSG_THREAT_REMOVE, 18);
        out.WriteGuidMask<0, 1, 5>(owner);
        out.WriteGuidMask<4, 0>(removed);
        out.WriteGuidMask<4, 6>(owner);
        out.WriteGuidMask<7, 6, 3>(removed);
        out.WriteGuidMask<2>(owner);
        out.WriteGuidMask<1>(removed);
        out.WriteGuidMask<3, 7>(owner);
        out.WriteGuidMask<5, 2>(removed);
        out.FlushBits();

        out.WriteGuidBytes<3, 0, 2>(removed);
        out.WriteGuidBytes<5, 4, 7, 3, 0>(owner);
        out.WriteGuidBytes<4>(removed);
        out.WriteGuidBytes<1>(owner);
        out.WriteGuidBytes<1>(removed);
        out.WriteGuidBytes<6>(owner);
        out.WriteGuidBytes<7, 6>(removed);
        out.WriteGuidBytes<2>(owner);
        out.WriteGuidBytes<5>(removed);
    }
}

#include <list>

/**
 * @brief Spell interrupt flags
 *
 * Flags that determine what can interrupt a spell cast.
 */
enum SpellInterruptFlags
{
    SPELL_INTERRUPT_FLAG_MOVEMENT = 0x01,    ///< Interrupted by movement
    SPELL_INTERRUPT_FLAG_DAMAGE = 0x02,      ///< Interrupted by damage
    SPELL_INTERRUPT_FLAG_INTERRUPT = 0x04,   ///< Interrupted by interrupt ability
    SPELL_INTERRUPT_FLAG_AUTOATTACK = 0x08,  ///< Interrupted by auto-attack
    SPELL_INTERRUPT_FLAG_ABORT_ON_DMG = 0x10 ///< Complete interrupt on direct damage
    // SPELL_INTERRUPT_UNK             = 0x20               // unk, 564 of 727 spells having this spell start with "Glyph"
};

/**
 * @brief Spell channel interrupt flags
 *
 * Flags that determine what can interrupt a channeled spell.
 */
enum SpellChannelInterruptFlags
{
    CHANNEL_FLAG_DAMAGE = 0x0002,   ///< Interrupted by damage
    CHANNEL_FLAG_MOVEMENT = 0x0008, ///< Interrupted by movement
    CHANNEL_FLAG_TURNING = 0x0010,  ///< Interrupted by turning
    CHANNEL_FLAG_DAMAGE2 = 0x0080,  ///< Interrupted by damage (secondary)
    CHANNEL_FLAG_DELAY = 0x4000     ///< Interrupted by delay
};

/**
 * @brief Spell aura interrupt flags
 *
 * Flags that determine what can interrupt an aura effect.
 */
enum SpellAuraInterruptFlags
{
    AURA_INTERRUPT_FLAG_UNK0 = 0x00000001,                     ///< Unknown (removed when getting hit by a negative spell?)
    AURA_INTERRUPT_FLAG_DAMAGE = 0x00000002,                   ///< Removed by any damage
    AURA_INTERRUPT_FLAG_UNK2 = 0x00000004,                     ///< Unknown
    AURA_INTERRUPT_FLAG_MOVE = 0x00000008,                     ///< Removed by any movement
    AURA_INTERRUPT_FLAG_TURNING = 0x00000010,                  ///< Removed by any turning
    AURA_INTERRUPT_FLAG_ENTER_COMBAT = 0x00000020,             ///< Removed by entering combat
    AURA_INTERRUPT_FLAG_NOT_MOUNTED = 0x00000040,              ///< Removed by unmounting
    AURA_INTERRUPT_FLAG_NOT_ABOVEWATER = 0x00000080,           ///< Removed by entering water
    AURA_INTERRUPT_FLAG_NOT_UNDERWATER = 0x00000100,           ///< Removed by leaving water
    AURA_INTERRUPT_FLAG_NOT_SHEATHED = 0x00000200,             ///< Removed by unsheathing
    AURA_INTERRUPT_FLAG_TALK = 0x00000400,                     ///< Unknown
    AURA_INTERRUPT_FLAG_USE = 0x00000800,                      ///< Unknown
    AURA_INTERRUPT_FLAG_MELEE_ATTACK = 0x00001000,             ///< Removed by melee attacks
    AURA_INTERRUPT_FLAG_UNK13 = 0x00002000,                    ///< Unknown
    AURA_INTERRUPT_FLAG_UNK14 = 0x00004000,                    ///< Unknown
    AURA_INTERRUPT_FLAG_UNK15 = 0x00008000,                    ///< Unknown (removed by casting a spell?)
    AURA_INTERRUPT_FLAG_UNK16 = 0x00010000,                    ///< Unknown
    AURA_INTERRUPT_FLAG_MOUNTING = 0x00020000,                 ///< Removed by mounting
    AURA_INTERRUPT_FLAG_NOT_SEATED = 0x00040000,               ///< Removed by standing up (used by food, drink, sleep, Fake Death)
    AURA_INTERRUPT_FLAG_CHANGE_MAP = 0x00080000,               ///< Removed by leaving map/getting teleported
    AURA_INTERRUPT_FLAG_IMMUNE_OR_LOST_SELECTION = 0x00100000, ///< Removed by invulnerability or lost selection
    AURA_INTERRUPT_FLAG_UNK21 = 0x00200000,                    ///< Unknown
    AURA_INTERRUPT_FLAG_UNK22 = 0x00400000,                    ///< Unknown
    AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT = 0x00800000,         ///< Removed by entering PvP combat
    AURA_INTERRUPT_FLAG_DIRECT_DAMAGE = 0x01000000,            ///< Removed by any direct damage
    AURA_INTERRUPT_FLAG_UNK25 = 0x02000000,                    ///< 25
    AURA_INTERRUPT_FLAG_UNK26 = 0x04000000,                    ///< 26
    AURA_INTERRUPT_FLAG_DAMAGE2 = 0x08000000,                  ///< 27   removed by damage spells + removed by damage, other than .. (diseases, Censure)
    AURA_INTERRUPT_FLAG_ENTER_COMBAT2 = 0x10000000,            ///< 28
    AURA_INTERRUPT_FLAG_UNK29 = 0x20000000,                    ///< 29
    AURA_INTERRUPT_FLAG_UNK30 = 0x40000000,                    ///< 30
    AURA_INTERRUPT_FLAG_UNK31 = 0x80000000,                    ///< 31
};

/**
 * @brief Spell modifier operation enumeration
 *
 * Defines the different operations that can be modified on spells.
 */
enum SpellModOp
{
    SPELLMOD_DAMAGE = 0,                ///< Damage modifier
    SPELLMOD_DURATION = 1,              ///< Duration modifier
    SPELLMOD_THREAT = 2,                ///< Threat modifier
    SPELLMOD_EFFECT1 = 3,               ///< Attack power modifier
    SPELLMOD_CHARGES = 4,               ///< Charges modifier
    SPELLMOD_RANGE = 5,                 ///< Range modifier
    SPELLMOD_RADIUS = 6,                ///< Radius modifier
    SPELLMOD_CRITICAL_CHANCE = 7,       ///< Critical chance modifier
    SPELLMOD_ALL_EFFECTS = 8,           ///< All effects modifier
    SPELLMOD_NOT_LOSE_CASTING_TIME = 9, ///< Don't lose casting time modifier
    SPELLMOD_CASTING_TIME = 10,         ///< Casting time modifier
    SPELLMOD_COOLDOWN = 11,             ///< Cooldown modifier
    SPELLMOD_EFFECT2 = 12,              ///< Speed modifier
    // spellmod 13 unused
    SPELLMOD_COST = 14,                 ///< Cost modifier
    SPELLMOD_CRIT_DAMAGE_BONUS = 15,    ///< Critical damage bonus modifier
    SPELLMOD_RESIST_MISS_CHANCE = 16,   ///< Resist miss chance modifier
    SPELLMOD_JUMP_TARGETS = 17,         ///< Jump targets modifier
    SPELLMOD_CHANCE_OF_SUCCESS = 18,    ///< Chance of success (only used with SPELL_AURA_ADD_FLAT_MODIFIER and affects proc spells)
    SPELLMOD_ACTIVATION_TIME = 19,      ///< Activation time modifier
    SPELLMOD_EFFECT_PAST_FIRST = 20,    ///< Effect past first modifier
    SPELLMOD_GLOBAL_COOLDOWN = 21,      ///< Casting time old modifier
    SPELLMOD_DOT = 22,                  ///< DoT modifier
    SPELLMOD_EFFECT3 = 23,              ///< Haste modifier
    SPELLMOD_SPELL_BONUS_DAMAGE = 24,   ///< Spell bonus damage modifier
    // spellmod 25 unused
    SPELLMOD_FREQUENCY_OF_SUCCESS = 26, ///< Only used with SPELL_AURA_ADD_PCT_MODIFIER and affects used on proc spells
    SPELLMOD_MULTIPLE_VALUE = 27,       ///< Multiple value modifier
    SPELLMOD_RESIST_DISPEL_CHANCE = 28,  ///< Resist dispel chance modifier
    SPELLMOD_SPELL_COST_REFUND_ON_FAIL = 30,
};

#define MAX_SPELLMOD 32

/**
 * @brief Spell facing flags enumeration
 *
 * Flags that determine facing requirements for spells.
 */
enum SpellFacingFlags
{
    SPELL_FACING_FLAG_INFRONT = 0x0001 ///< Target must be in front
};

#define BASE_MELEERANGE_OFFSET 1.33f
#define BASE_MINDAMAGE 1.0f
#define BASE_MAXDAMAGE 2.0f
#define BASE_ATTACK_TIME 2000
#define BASE_BLOCK_DAMAGE_PERCENT 30

#define SCALE_SPELLPOWER_HEALING        1.88f

/**
 * byte value (UNIT_FIELD_BYTES_1,0).
 *
 * This is not to be used as a bitmask but as one value
 * each, ie: you can't be standing and sitting down at
 * the same time.
 * \see Unit::getStandState
 * \see Unit::SetStandState
 * \see Unit::IsSitState
 * \see Unit::IsStandState
 */
enum UnitStandStateType
{
    UNIT_STAND_STATE_STAND             = 0,
    UNIT_STAND_STATE_SIT               = 1,
    UNIT_STAND_STATE_SIT_CHAIR         = 2,
    UNIT_STAND_STATE_SLEEP             = 3,
    UNIT_STAND_STATE_SIT_LOW_CHAIR     = 4,
    UNIT_STAND_STATE_SIT_MEDIUM_CHAIR  = 5,
    UNIT_STAND_STATE_SIT_HIGH_CHAIR    = 6,
    UNIT_STAND_STATE_DEAD              = 7,
    UNIT_STAND_STATE_KNEEL             = 8,
    UNIT_STAND_STATE_CUSTOM            = 9                  // Depends on model animation. Submerge, freeze, hide, hibernate, rest
};

#define MAX_UNIT_STAND_STATE             10

/* byte flag value not exist in 1.12, moved/merged in (UNIT_FIELD_BYTES_1,3), in post-1.x it's in (UNIT_FIELD_BYTES_1,2)
enum UnitStandFlags
*/

// byte flags value (UNIT_FIELD_BYTES_1,1)
// This corresponds to free talent points (pet case)

// byte flags value (UNIT_FIELD_BYTES_1,2)
enum UnitStandFlags
{
    UNIT_STAND_FLAGS_UNK1         = 0x01,
    UNIT_STAND_FLAGS_CREEP        = 0x02,
    UNIT_STAND_FLAGS_UNK3         = 0x04,
    UNIT_STAND_FLAGS_UNK4         = 0x08,
    UNIT_STAND_FLAGS_UNK5         = 0x10,
    UNIT_STAND_FLAGS_ALL          = 0xFF
};

// byte flags value (UNIT_FIELD_BYTES_1,3)
enum UnitBytes1_Flags
{
    UNIT_BYTE1_FLAG_ALWAYS_STAND = 0x01,
    UNIT_BYTE1_FLAG_FLY_ANIM     = 0x02,                    // Creature that can fly and are not on the ground appear to have this flag. If they are on the ground, flag is not present.
    UNIT_BYTE1_FLAG_UNTRACKABLE  = 0x04,
    UNIT_BYTE1_FLAG_ALL          = 0xFF
};

/**
 *  byte value (UNIT_FIELD_BYTES_2,0)
 */
enum SheathState
{
    /// non prepared weapon
    SHEATH_STATE_UNARMED  = 0,
    /// prepared melee weapon
    SHEATH_STATE_MELEE    = 1,
    /// prepared ranged weapon
    SHEATH_STATE_RANGED   = 2
};

#define MAX_SHEATH_STATE    3

// byte flags value (UNIT_FIELD_BYTES_2,1)
enum UnitPVPStateFlags
{
    UNIT_BYTE2_FLAG_PVP         = 0x01,
    UNIT_BYTE2_FLAG_UNK1        = 0x02,
    UNIT_BYTE2_FLAG_FFA_PVP     = 0x04,
    UNIT_BYTE2_FLAG_SUPPORTABLE = 0x08,                       // allows for being targeted for healing/bandaging by friendlies
    UNIT_BYTE2_FLAG_AURAS       = 0x10,                       // show possitive auras as positive, and allow its dispel
    UNIT_BYTE2_FLAG_UNK5        = 0x20,                       // show negative auras as positive, *not* allowing dispel (at least for pets)
    UNIT_BYTE2_FLAG_UNK6        = 0x40,
    UNIT_BYTE2_FLAG_UNK7        = 0x80,
    UNIT_BYTE2_FLAG_SANCTUARY   = UNIT_BYTE2_FLAG_SUPPORTABLE // Make Eluna Happy
};

// byte flags value (UNIT_FIELD_BYTES_2,2)
enum UnitRename
{
    UNIT_CAN_BE_RENAMED     = 0x01,
    UNIT_CAN_BE_ABANDONED   = 0x02,
};

// byte flags value (UNIT_FIELD_BYTES_2,3)                  See enum ShapeshiftForm in SharedDefines.h

#define CREATURE_MAX_SPELLS     10

enum Swing
{
    NOSWING                    = 0,
    SINGLEHANDEDSWING          = 1,
    TWOHANDEDSWING             = 2
};

/**
 * This seems to be the state a target of an attack can be in.
 * \todo Document more
 */
enum VictimState
{
    VICTIMSTATE_UNAFFECTED     = 0,                         // seen in relation with HITINFO_MISS
    VICTIMSTATE_NORMAL         = 1,
    VICTIMSTATE_DODGE          = 2,
    VICTIMSTATE_PARRY          = 3,
    VICTIMSTATE_INTERRUPT      = 4,
    VICTIMSTATE_BLOCKS         = 5,
    VICTIMSTATE_EVADES         = 6,
    VICTIMSTATE_IS_IMMUNE      = 7,
    VICTIMSTATE_DEFLECTS       = 8
};

/**
 * OFFSWING and BASESWING/2 or MAINSWING/2 to be more
 * in line with what is used in the other parts?
 *
 * \todo Rename the LEFTSWING and NORMALSWING/2 to:
 */
enum HitInfo
{
    HITINFO_NORMALSWING         = 0x00000000,
    HITINFO_UNK0                = 0x00000001,               // req correct packet structure
    HITINFO_NORMALSWING2        = 0x00000002,
    HITINFO_LEFTSWING           = 0x00000004,
    HITINFO_UNK3                = 0x00000008,
    HITINFO_MISS                = 0x00000010,
    HITINFO_ABSORB              = 0x00000020,               // plays absorb sound
    HITINFO_ABSORB2             = 0x00000040,               // absorbed damage
    HITINFO_RESIST              = 0x00000080,               // resisted atleast some damage
    HITINFO_RESIST2             = 0x00000100,               // resisted atleast some damage
    HITINFO_CRITICALHIT         = 0x00000200,               // critical hit
    // 0x00000400
    // 0x00000800
    // 0x00001000
    HITINFO_BLOCK               = 0x00002000,               // blocked damage
    // 0x00004000
    // 0x00008000
    HITINFO_GLANCING            = 0x00010000,
    HITINFO_CRUSHING            = 0x00020000,
    HITINFO_NOACTION            = 0x00040000,               // guessed
    // 0x00080000
    // 0x00100000
    HITINFO_SWINGNOHITSOUND     = 0x00200000,               // guessed
    // 0x00400000
    HITINFO_UNK22               = 0x00800000
};

// i would like to remove this: (it is defined in item.h
enum InventorySlot
{
    NULL_BAG                   = 0,
    NULL_SLOT                  = 255
};

struct FactionTemplateEntry;
struct Modifier;
struct SpellEntry;
struct SpellEntryExt;

class Aura;
class SpellAuraHolder;
class Creature;
class Spell;
class DynamicObject;
class GameObject;
class Item;
class Pet;
class PetAura;
class Totem;
class VehicleInfo;

struct SpellImmune
{
    uint32 type;
    uint32 spellId;
};

typedef std::list<SpellImmune> SpellImmuneList;

enum UnitModifierType
{
    BASE_VALUE = 0,
    BASE_PCT = 1,
    TOTAL_VALUE = 2,
    TOTAL_PCT = 3,
    MODIFIER_TYPE_END = 4
};

enum WeaponDamageRange
{
    MINDAMAGE,
    MAXDAMAGE
};

enum DamageTypeToSchool
{
    RESISTANCE,
    DAMAGE_DEALT,
    DAMAGE_TAKEN
};

/**
 * This is what decides how an \ref Aura was removed, the cause of it being removed.
 */
enum AuraRemoveMode
{
    AURA_REMOVE_BY_DEFAULT,
    AURA_REMOVE_BY_STACK,           ///< at replace by similar aura
    AURA_REMOVE_BY_CANCEL,          ///< It was cancelled by the user (needs confirmation)
    AURA_REMOVE_BY_DISPEL,          ///< It was dispelled by ie Remove Magic
    AURA_REMOVE_BY_DEATH,           ///< The \ref Unit died and there for it was removed
    AURA_REMOVE_BY_DELETE,          ///< use for speedup and prevent unexpected effects at player logout/pet unsummon (must be used _only_ after save), delete.
    AURA_REMOVE_BY_SHIELD_BREAK,    ///< when absorb shield is removed by damage
    AURA_REMOVE_BY_EXPIRE,          ///< at duration end
    AURA_REMOVE_BY_TRACKING         ///< aura is removed because of a conflicting tracked aura
};

enum UnitMods
{
    UNIT_MOD_STAT_STRENGTH,                                 // UNIT_MOD_STAT_STRENGTH..UNIT_MOD_STAT_SPIRIT must be in existing order, it's accessed by index values of Stats enum.
    UNIT_MOD_STAT_AGILITY,
    UNIT_MOD_STAT_STAMINA,
    UNIT_MOD_STAT_INTELLECT,
    UNIT_MOD_STAT_SPIRIT,
    UNIT_MOD_HEALTH,
    UNIT_MOD_MANA,                                          // UNIT_MOD_MANA..UNIT_MOD_ALTERNATIVE must be in existing order, it's accessed by index values of Powers enum.
    UNIT_MOD_RAGE,
    UNIT_MOD_FOCUS,
    UNIT_MOD_ENERGY,
    UNIT_MOD_HAPPINESS,                                     // REQUIRED for fast indexing to work.
    UNIT_MOD_RUNE,
    UNIT_MOD_RUNIC_POWER,
    UNIT_MOD_SOUL_SHARDS,
    UNIT_MOD_ECLIPSE,
    UNIT_MOD_HOLY_POWER,
    UNIT_MOD_ALTERNATIVE,
    UNIT_MOD_CHI,
    UNIT_MOD_ARMOR,                                         // UNIT_MOD_ARMOR..UNIT_MOD_RESISTANCE_ARCANE must be in existing order, it's accessed by index values of SpellSchools enum.
    UNIT_MOD_RESISTANCE_HOLY,
    UNIT_MOD_RESISTANCE_FIRE,
    UNIT_MOD_RESISTANCE_NATURE,
    UNIT_MOD_RESISTANCE_FROST,
    UNIT_MOD_RESISTANCE_SHADOW,
    UNIT_MOD_RESISTANCE_ARCANE,
    UNIT_MOD_ATTACK_POWER,
    UNIT_MOD_ATTACK_POWER_RANGED,
    UNIT_MOD_DAMAGE_MAINHAND,
    UNIT_MOD_DAMAGE_OFFHAND,
    UNIT_MOD_DAMAGE_RANGED,
    UNIT_MOD_END,
    // synonyms
    UNIT_MOD_STAT_START = UNIT_MOD_STAT_STRENGTH,
    UNIT_MOD_STAT_END = UNIT_MOD_STAT_SPIRIT + 1,
    UNIT_MOD_RESISTANCE_START = UNIT_MOD_ARMOR,
    UNIT_MOD_RESISTANCE_END = UNIT_MOD_RESISTANCE_ARCANE + 1,
    UNIT_MOD_POWER_START = UNIT_MOD_MANA,
    UNIT_MOD_POWER_END = UNIT_MOD_CHI + 1
};

enum BaseModGroup
{
    CRIT_PERCENTAGE,
    RANGED_CRIT_PERCENTAGE,
    OFFHAND_CRIT_PERCENTAGE,
    SHIELD_BLOCK_DAMAGE_VALUE,
    BASEMOD_END
};

enum BaseModType
{
    FLAT_MOD,
    PCT_MOD
};

#define MOD_END (PCT_MOD+1)

enum DeathState
{
    ALIVE          = 0,     ///< show as alive
    JUST_DIED      = 1,     ///< temporary state at die, for creature auto converted to CORPSE, for player at next update call
    CORPSE         = 2,     ///< corpse state, for player this also meaning that player not leave corpse
    DEAD           = 3,     ///< for creature despawned state (corpse despawned), for player CORPSE/DEAD not clear way switches (FIXME), and use m_deathtimer > 0 check for real corpse state
    JUST_ALIVED    = 4      ///< temporary state at resurrection, for creature auto converted to ALIVE, for player at next update call
};

/**
 * internal state flags for some auras and movement generators, other. (Taken from comment)
 */
enum UnitState
{
    // persistent state (applied by aura/etc until expire)
    UNIT_STAT_MELEE_ATTACKING = 0x00000001,                 // unit is melee attacking someone Unit::Attack
    UNIT_STAT_ATTACK_PLAYER   = 0x00000002,                 // unit attack player or player's controlled unit and have contested pvpv timer setup, until timer expire, combat end and etc
    UNIT_STAT_DIED            = 0x00000004,                 // Unit::SetFeignDeath
    UNIT_STAT_STUNNED         = 0x00000008,                 // Aura::HandleAuraModStun
    UNIT_STAT_ROOT            = 0x00000010,                 // Aura::HandleAuraModRoot
    UNIT_STAT_ISOLATED        = 0x00000020,                 // area auras do not affect other players, Aura::HandleAuraModSchoolImmunity
    UNIT_STAT_CONTROLLED      = 0x00000040,                 // Aura::HandleAuraModPossess

    // persistent movement generator state (all time while movement generator applied to unit (independent from top state of movegen)
    UNIT_STAT_TAXI_FLIGHT     = 0x00000080,                 // player is in flight mode (in fact interrupted at far teleport until next map telport landing)
    UNIT_STAT_DISTRACTED      = 0x00000100,                 // DistractedMovementGenerator active

    // persistent movement generator state with non-persistent mirror states for stop support
    // (can be removed temporary by stop command or another movement generator apply)
    // not use _MOVE versions for generic movegen state, it can be removed temporary for unit stop and etc
    UNIT_STAT_CONFUSED        = 0x00000200,                 // ConfusedMovementGenerator active/onstack
    UNIT_STAT_CONFUSED_MOVE   = 0x00000400,
    UNIT_STAT_ROAMING         = 0x00000800,                 // RandomMovementGenerator/PointMovementGenerator/WaypointMovementGenerator active (now always set)
    UNIT_STAT_ROAMING_MOVE    = 0x00001000,
    UNIT_STAT_CHASE           = 0x00002000,                 // ChaseMovementGenerator active
    UNIT_STAT_CHASE_MOVE      = 0x00004000,
    UNIT_STAT_FOLLOW          = 0x00008000,                 // FollowMovementGenerator active
    UNIT_STAT_FOLLOW_MOVE     = 0x00010000,
    UNIT_STAT_FLEEING         = 0x00020000,                 // FleeMovementGenerator/TimedFleeingMovementGenerator active/onstack
    UNIT_STAT_FLEEING_MOVE    = 0x00040000,
    // More room for other MMGens

    // High-Level states (usually only with Creatures)
    UNIT_STAT_NO_COMBAT_MOVEMENT    = 0x01000000,           // Combat Movement for MoveChase stopped
    UNIT_STAT_RUNNING               = 0x02000000,           // SetRun for waypoints and such
    UNIT_STAT_WAYPOINT_PAUSED       = 0x04000000,           // Waypoint-Movement paused genericly (ie by script)

    UNIT_STAT_IGNORE_PATHFINDING    = 0x10000000,           // do not use pathfinding in any MovementGenerator

    // masks (only for check)

    // can't move currently
    UNIT_STAT_CAN_NOT_MOVE    = UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_DIED,

    // stay by different reasons
    UNIT_STAT_NOT_MOVE        = UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_DIED |
                                UNIT_STAT_DISTRACTED,

    // stay or scripted movement for effect( = in player case you can't move by client command)
    UNIT_STAT_NO_FREE_MOVE    = UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_DIED |
                                UNIT_STAT_TAXI_FLIGHT |
                                UNIT_STAT_CONFUSED | UNIT_STAT_FLEEING,

    // not react at move in sight or other
    UNIT_STAT_CAN_NOT_REACT   = UNIT_STAT_STUNNED | UNIT_STAT_DIED |
                                UNIT_STAT_CONFUSED | UNIT_STAT_FLEEING,

    // AI disabled by some reason
    UNIT_STAT_LOST_CONTROL    = UNIT_STAT_FLEEING | UNIT_STAT_CONTROLLED,

    // above 2 state cases
    UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL  = UNIT_STAT_CAN_NOT_REACT | UNIT_STAT_LOST_CONTROL,

    // masks (for check or reset)

    // for real move using movegen check and stop (except unstoppable flight)
    UNIT_STAT_MOVING          = UNIT_STAT_ROAMING_MOVE | UNIT_STAT_CHASE_MOVE | UNIT_STAT_FOLLOW_MOVE | UNIT_STAT_FLEEING_MOVE,

    UNIT_STAT_RUNNING_STATE   = UNIT_STAT_CHASE_MOVE | UNIT_STAT_FLEEING_MOVE | UNIT_STAT_RUNNING,

    UNIT_STAT_ALL_STATE       = 0xFFFFFFFF,
    UNIT_STAT_ALL_DYN_STATES  = UNIT_STAT_ALL_STATE & ~(UNIT_STAT_NO_COMBAT_MOVEMENT | UNIT_STAT_RUNNING | UNIT_STAT_WAYPOINT_PAUSED | UNIT_STAT_IGNORE_PATHFINDING),

};

enum UnitMoveType
{
    MOVE_WALK           = 0,
    MOVE_RUN            = 1,
    MOVE_RUN_BACK       = 2,
    MOVE_SWIM           = 3,
    MOVE_SWIM_BACK      = 4,
    MOVE_TURN_RATE      = 5,
    MOVE_FLIGHT         = 6,
    MOVE_FLIGHT_BACK    = 7,
    MOVE_PITCH_RATE     = 8
};

#define MAX_MOVE_TYPE     9

enum CombatRating
{
    CR_WEAPON_SKILL             = 0,
    CR_DEFENSE_SKILL            = 1,                        // obsolete
    CR_DODGE                    = 2,
    CR_PARRY                    = 3,
    CR_BLOCK                    = 4,
    CR_HIT_MELEE                = 5,
    CR_HIT_RANGED               = 6,
    CR_HIT_SPELL                = 7,
    CR_CRIT_MELEE               = 8,
    CR_CRIT_RANGED              = 9,
    CR_CRIT_SPELL               = 10,
    CR_HIT_TAKEN_MELEE          = 11,                       // obsolete
    CR_HIT_TAKEN_RANGED         = 12,                       // obsolete
    CR_HIT_TAKEN_SPELL          = 13,                       // obsolete
    CR_CRIT_TAKEN_MELEE         = 14,                       // COMBAT_RATING_RESILIENCE_CRIT_TAKEN obsolete
    CR_RESILIENCE_DAMAGE_TAKEN  = 15,                       // old CR_CRIT_TAKEN_RANGED
    CR_CRIT_TAKEN_SPELL         = 16,                       // obsolete
    CR_HASTE_MELEE              = 17,
    CR_HASTE_RANGED             = 18,
    CR_HASTE_SPELL              = 19,
    CR_WEAPON_SKILL_MAINHAND    = 20,                       // obsolete
    CR_WEAPON_SKILL_OFFHAND     = 21,                       // obsolete
    CR_WEAPON_SKILL_RANGED      = 22,                       // obsolete
    CR_EXPERTISE                = 23,
    CR_ARMOR_PENETRATION        = 24,
    CR_MASTERY                  = 25,
    CR_PVP_POWER                = 26                        // 5.x: PLAYER_FIELD_COMBAT_RATING_1[26]
};

#define MAX_COMBAT_RATING         27

/// internal used flags for marking special auras - for example some dummy-auras
enum UnitAuraFlags
{
    UNIT_AURAFLAG_ALIVE_INVISIBLE   = 0x1,                  // aura which makes unit invisible for alive
};

enum UnitVisibility
{
    VISIBILITY_OFF                = 0,                      // absolute, not detectable, GM-like, can see all other
    VISIBILITY_ON                 = 1,
    VISIBILITY_GROUP_STEALTH      = 2,                      // detect chance, seen and can see group members
    VISIBILITY_GROUP_INVISIBILITY = 3,                      // invisibility, can see and can be seen only another invisible unit or invisible detection unit, set only if not stealthed, and in checks not used (mask used instead)
    VISIBILITY_GROUP_NO_DETECT    = 4,                      // state just at stealth apply for update Grid state. Don't remove, otherwise stealth spells will break
    VISIBILITY_REMOVE_CORPSE      = 5                       // special totally not detectable visibility for force delete object while removing a corpse
};

// Value masks for UNIT_FIELD_FLAGS
enum UnitFlags
{
    UNIT_FLAG_UNK_0                 = 0x00000001,
    UNIT_FLAG_NON_ATTACKABLE        = 0x00000002,           // not attackable
    UNIT_FLAG_DISABLE_MOVE          = 0x00000004,
    UNIT_FLAG_PVP_ATTACKABLE        = 0x00000008,           // allow apply pvp rules to attackable state in addition to faction dependent state
    UNIT_FLAG_RENAME                = 0x00000010,
    UNIT_FLAG_PREPARATION           = 0x00000020,           // don't take reagents for spells with SPELL_ATTR_EX5_NO_REAGENT_WHILE_PREP
    UNIT_FLAG_UNK_6                 = 0x00000040,
    UNIT_FLAG_NOT_ATTACKABLE_1      = 0x00000080,           // ?? (UNIT_FLAG_PVP_ATTACKABLE | UNIT_FLAG_NOT_ATTACKABLE_1) is NON_PVP_ATTACKABLE
    UNIT_FLAG_OOC_NOT_ATTACKABLE    = 0x00000100,           // 2.0.8 - (OOC Out Of Combat) Can not be attacked when not in combat. Removed if unit for some reason enter combat (flag probably removed for the attacked and it's party/group only)
    UNIT_FLAG_PASSIVE               = 0x00000200,           // makes you unable to attack everything. Almost identical to our "civilian"-term. Will ignore it's surroundings and not engage in combat unless "called upon" or engaged by another unit.
    UNIT_FLAG_LOOTING               = 0x00000400,           // loot animation
    UNIT_FLAG_PET_IN_COMBAT         = 0x00000800,           // in combat?, 2.0.8
    UNIT_FLAG_PVP                   = 0x00001000,           // changed in 3.0.3
    UNIT_FLAG_SILENCED              = 0x00002000,           // silenced, 2.1.1
    UNIT_FLAG_CANT_SWIM             = 0x00004000,           // 2.0.8
    UNIT_FLAG_CAN_SWIM              = 0x00008000,           // shows swim animation in water
    UNIT_FLAG_UNK_14                = UNIT_FLAG_CANT_SWIM,  ///< legacy alias
    UNIT_FLAG_UNK_15                = UNIT_FLAG_CAN_SWIM,   ///< legacy alias (kept for SD3)
    UNIT_FLAG_UNK_16                = 0x00010000,           // removes attackable icon
    UNIT_FLAG_PACIFIED              = 0x00020000,           // 3.0.3 ok
    UNIT_FLAG_STUNNED               = 0x00040000,           // 3.0.3 ok
    UNIT_FLAG_IN_COMBAT             = 0x00080000,
    UNIT_FLAG_TAXI_FLIGHT           = 0x00100000,           // disable casting at client side spell not allowed by taxi flight (mounted?), probably used with 0x4 flag
    UNIT_FLAG_DISARMED              = 0x00200000,           // 3.0.3, disable melee spells casting..., "Required melee weapon" added to melee spells tooltip.
    UNIT_FLAG_CONFUSED              = 0x00400000,
    UNIT_FLAG_FLEEING               = 0x00800000,
    UNIT_FLAG_PLAYER_CONTROLLED     = 0x01000000,           // used in spell Eyes of the Beast for pet... let attack by controlled creature
    UNIT_FLAG_NOT_SELECTABLE        = 0x02000000,
    UNIT_FLAG_SKINNABLE             = 0x04000000,
    UNIT_FLAG_MOUNT                 = 0x08000000,
    UNIT_FLAG_UNK_28                = 0x10000000,
    UNIT_FLAG_UNK_29                = 0x20000000,           // used in Feing Death spell
    UNIT_FLAG_SHEATHE               = 0x40000000,
    UNIT_FLAG_UNK_31                = 0x80000000            // set skinnable icon and also changes color of portrait
};

// Value masks for UNIT_FIELD_FLAGS_2 (initialy is not implemented)
enum UnitFlags2
{
    UNIT_FLAG2_FEIGN_DEATH          = 0x00000001,
    UNIT_FLAG2_UNK1                 = 0x00000002,           // Hides body and body armor. Weapons and shoulder and head armor still visible
    UNIT_FLAG2_UNK2                 = 0x00000004,
    UNIT_FLAG2_COMPREHEND_LANG      = 0x00000008,
    UNIT_FLAG2_CLONED               = 0x00000010,           // Used in SPELL_AURA_MIRROR_IMAGE
    UNIT_FLAG2_UNK5                 = 0x00000020,
    UNIT_FLAG2_FORCE_MOVE           = 0x00000040,
    UNIT_FLAG2_DISARM_OFFHAND       = 0x00000080,           // also shield case
    UNIT_FLAG2_UNK8                 = 0x00000100,
    UNIT_FLAG2_UNK9                 = 0x00000200,
    UNIT_FLAG2_DISARM_RANGED        = 0x00000400,
    UNIT_FLAG2_REGENERATE_POWER     = 0x00000800,
    UNIT_FLAG2_WORGEN_TRANSFORM     = 0x00080000,           // transform to worgen
    UNIT_FLAG2_WORGEN_TRANSFORM2    = 0x00100000,           // transform to worgen, but less animation?
    UNIT_FLAG2_WORGEN_TRANSFORM3    = 0x00200000            // transform to worgen, but less animation?
};

/// Non Player Character flags
enum NPCFlags
{
    UNIT_NPC_FLAG_NONE                  = 0x00000000,
    UNIT_NPC_FLAG_GOSSIP                = 0x00000001,       // 100%
    UNIT_NPC_FLAG_QUESTGIVER            = 0x00000002,       // guessed, probably ok
    UNIT_NPC_FLAG_UNK1                  = 0x00000004,
    UNIT_NPC_FLAG_UNK2                  = 0x00000008,
    UNIT_NPC_FLAG_TRAINER               = 0x00000010,       // 100%
    UNIT_NPC_FLAG_TRAINER_CLASS         = 0x00000020,       // 100%
    UNIT_NPC_FLAG_TRAINER_PROFESSION    = 0x00000040,       // 100%
    UNIT_NPC_FLAG_VENDOR                = 0x00000080,       // 100%
    UNIT_NPC_FLAG_VENDOR_AMMO           = 0x00000100,       // 100%, general goods vendor
    UNIT_NPC_FLAG_VENDOR_FOOD           = 0x00000200,       // 100%
    UNIT_NPC_FLAG_VENDOR_POISON         = 0x00000400,       // guessed
    UNIT_NPC_FLAG_VENDOR_REAGENT        = 0x00000800,       // 100%
    UNIT_NPC_FLAG_REPAIR                = 0x00001000,       // 100%
    UNIT_NPC_FLAG_FLIGHTMASTER          = 0x00002000,       // 100%
    UNIT_NPC_FLAG_SPIRITHEALER          = 0x00004000,       // guessed
    UNIT_NPC_FLAG_SPIRITGUIDE           = 0x00008000,       // guessed
    UNIT_NPC_FLAG_INNKEEPER             = 0x00010000,       // 100%
    UNIT_NPC_FLAG_BANKER                = 0x00020000,       // 100%
    UNIT_NPC_FLAG_PETITIONER            = 0x00040000,       // 100% 0xC0000 = guild petitions, 0x40000 = arena team petitions
    UNIT_NPC_FLAG_TABARDDESIGNER        = 0x00080000,       // 100%
    UNIT_NPC_FLAG_BATTLEMASTER          = 0x00100000,       // 100%
    UNIT_NPC_FLAG_AUCTIONEER            = 0x00200000,       // 100%
    UNIT_NPC_FLAG_STABLEMASTER          = 0x00400000,       // 100%
    UNIT_NPC_FLAG_GUILD_BANKER          = 0x00800000,       // cause client to send 997 opcode
    UNIT_NPC_FLAG_SPELLCLICK            = 0x01000000,       // cause client to send 1015 opcode (spell click), dynamic, set at loading and don't must be set in DB
    UNIT_NPC_FLAG_PLAYER_VEHICLE        = 0x02000000,       // players with mounts that have vehicle data should have it set
    UNIT_NPC_FLAG_REFORGER              = 0x08000000,       // reforging
};

// used in most movement packets (send and received), 30 bits in client
enum MovementFlags
{
    MOVEFLAG_NONE               = 0x00000000,
    MOVEFLAG_FORWARD            = 0x00000001,
    MOVEFLAG_BACKWARD           = 0x00000002,
    MOVEFLAG_STRAFE_LEFT        = 0x00000004,
    MOVEFLAG_STRAFE_RIGHT       = 0x00000008,
    MOVEFLAG_TURN_LEFT          = 0x00000010,
    MOVEFLAG_TURN_RIGHT         = 0x00000020,
    MOVEFLAG_PITCH_UP           = 0x00000040,
    MOVEFLAG_PITCH_DOWN         = 0x00000080,
    MOVEFLAG_WALK_MODE          = 0x00000100,               // Walking
    MOVEFLAG_LEVITATING         = 0x00000200,
    MOVEFLAG_ROOT               = 0x00000400,
    MOVEFLAG_FALLING            = 0x00000800,
    MOVEFLAG_FALLINGFAR         = 0x00001000,
    MOVEFLAG_PENDINGSTOP        = 0x00002000,
    MOVEFLAG_PENDINGSTRAFESTOP  = 0x00004000,
    MOVEFLAG_PENDINGFORWARD     = 0x00008000,
    MOVEFLAG_PENDINGBACKWARD    = 0x00010000,
    MOVEFLAG_PENDINGSTRAFELEFT  = 0x00020000,
    MOVEFLAG_PENDINGSTRAFERIGHT = 0x00040000,
    MOVEFLAG_PENDINGROOT        = 0x00080000,
    MOVEFLAG_SWIMMING           = 0x00100000,               // appears with fly flag also
    MOVEFLAG_ASCENDING          = 0x00200000,               // swim up also
    MOVEFLAG_DESCENDING         = 0x00400000,               // swim down also
    MOVEFLAG_CAN_FLY            = 0x00800000,               // can fly in 3.3?
    MOVEFLAG_FLYING             = 0x01000000,               // Actual flying mode
    MOVEFLAG_SPLINE_ELEVATION   = 0x02000000,               // used for flight paths
    MOVEFLAG_WATERWALKING       = 0x04000000,               // prevent unit from falling through water
    MOVEFLAG_SAFE_FALL          = 0x08000000,               // active rogue safe fall spell (passive)
    MOVEFLAG_HOVER              = 0x10000000,
    MOVEFLAG_LOCAL_DIRTY        = 0x20000000,
};

// flags that use in movement check for example at spell casting
MovementFlags const movementFlagsMask = MovementFlags(
        MOVEFLAG_FORWARD | MOVEFLAG_BACKWARD  | MOVEFLAG_STRAFE_LEFT | MOVEFLAG_STRAFE_RIGHT |
        MOVEFLAG_PITCH_UP | MOVEFLAG_PITCH_DOWN | MOVEFLAG_ROOT        |
        MOVEFLAG_FALLING | MOVEFLAG_FALLINGFAR | MOVEFLAG_ASCENDING   |
        MOVEFLAG_FLYING  | MOVEFLAG_SPLINE_ELEVATION
                                        );

MovementFlags const movementOrTurningFlagsMask = MovementFlags(
            movementFlagsMask | MOVEFLAG_TURN_LEFT | MOVEFLAG_TURN_RIGHT
        );

// 12 bits in client
enum MovementFlags2
{
    MOVEFLAG2_NONE              = 0x0000,
    MOVEFLAG2_NO_STRAFE         = 0x0001,
    MOVEFLAG2_NO_JUMPING        = 0x0002,
    MOVEFLAG2_FULLSPEEDTURNING  = 0x0004,
    MOVEFLAG2_FULLSPEEDPITCHING = 0x0008,
    MOVEFLAG2_ALLOW_PITCHING    = 0x0010,
    MOVEFLAG2_UNK4              = 0x0020,
    MOVEFLAG2_UNK5              = 0x0040,
    MOVEFLAG2_UNK6              = 0x0080,                   // transport related
    MOVEFLAG2_UNK7              = 0x0100,
    MOVEFLAG2_INTERP_MOVEMENT   = 0x0200,
    MOVEFLAG2_INTERP_TURNING    = 0x0400,
    MOVEFLAG2_INTERP_PITCHING   = 0x0800,
    MOVEFLAG2_INTERP_MASK       = MOVEFLAG2_INTERP_MOVEMENT | MOVEFLAG2_INTERP_TURNING | MOVEFLAG2_INTERP_PITCHING
};

class MovementInfo
{
    public:
        MovementInfo() : moveFlags(MOVEFLAG_NONE), moveFlags2(MOVEFLAG2_NONE), time(0),
            t_time(0), t_seat(-1), t_time2(0), t_time3(0), s_pitch(0.0f), fallTime(0), splineElevation(0.0f),
            unknownBit148(false), unknownBit149(false), unknownBit172(false), movementCounter(0), movementForceCount(0),
            hasUnknownUInt32(false), unknownUInt32(0), nonZeroBitPadding(false), byteParam(0), speedFloat(0.0f) {}

        // Read/Write methods
        void Read(ByteBuffer& data, uint16 opcode);
        void Write(ByteBuffer& data, uint16 opcode) const;

        // Movement flags manipulations
        void AddMovementFlag(MovementFlags f) { moveFlags |= f; }
        void RemoveMovementFlag(MovementFlags f) { moveFlags &= ~f; }
        bool HasMovementFlag(MovementFlags f) const { return moveFlags & f; }
        bool HasMovementFlag2(MovementFlags2 f) const { return moveFlags2 & f; }
        MovementFlags GetMovementFlags() const { return MovementFlags(moveFlags); }
        void SetMovementFlags(MovementFlags f) { moveFlags = f; }
        MovementFlags2 GetMovementFlags2() const { return MovementFlags2(moveFlags2); }
        void AddMovementFlags2(MovementFlags2 f) { moveFlags2 |= f; }

        // Position manipulations
        Position const* GetPos() const { return &pos; }
        void SetTransportData(ObjectGuid guid, float x, float y, float z, float o, uint32 time, int8 seat)
        {
            t_guid = guid;
            t_pos.x = x;
            t_pos.y = y;
            t_pos.z = z;
            t_pos.o = o;
            t_time = time;
            t_seat = seat;
        }
        void ClearTransportData()
        {
            t_guid = ObjectGuid();
            t_pos.x = 0.0f;
            t_pos.y = 0.0f;
            t_pos.z = 0.0f;
            t_pos.o = 0.0f;
            t_time = 0;
            t_seat = -1;
        }
        /// Drop stale fall state so object creates built after a teleport carry no fall block.
        void ClearFallData()
        {
            fallTime = 0;
            jump = JumpInfo();
            si.hasFallData = false;
            si.hasFallDirection = false;
        }
        ObjectGuid const& GetGuid() const { return guid; }
        void SetMoverGuid(ObjectGuid guid) { this->guid = guid; }
        ObjectGuid const& GetGuid2() const { return guid2; }
        ObjectGuid const& GetTransportGuid() const { return t_guid; }
        Position const* GetTransportPos() const { return &t_pos; }
        int8 GetTransportSeat() const { return t_seat; }
        uint32 GetTime() const { return time; }
        uint32 GetTransportTime() const { return t_time; }
        uint32 GetTransportTime2() const { return t_time2; }
        uint32 GetTransportTime3() const { return t_time3; }
        uint32 GetMovementCounter() const { return movementCounter; }
        uint32 GetFallTime() const { return fallTime; }
        int8 GetByteParam() const { return byteParam; }
        /// The forced speed carried INSIDE the movement block. Four of the nine
        /// speed acknowledgements lead with the speed and are read before this
        /// struct; the other five carry it among the leading scalars, in a
        /// per-opcode order, so it has to come through the sequence instead.
        float GetSpeedFloat() const { return speedFloat; }
        void ChangeOrientation(float o) { pos.o = o; }
        void ChangePosition(float x, float y, float z, float o) { pos.x = x; pos.y = y; pos.z = z; pos.o = o; }
        void UpdateTime(uint32 _time) { time = _time; }

        struct JumpInfo
        {
            JumpInfo() : velocity(0.f), sinAngle(0.f), cosAngle(0.f), xyspeed(0.f) {}
            float   velocity, sinAngle, cosAngle, xyspeed;
        };

        // used only for SMSG_PLAYER_MOVE currently
        struct StatusInfo
        {
            StatusInfo() : hasFallData(false), hasFallDirection(false), hasOrientation(false),
                hasPitch(false), hasSpline(false), hasSplineElevation(false),
                hasTimeStamp(false), hasTransportTime2(false), hasTransportTime3(false) { }
            bool hasFallData        : 1;
            bool hasFallDirection   : 1;
            bool hasOrientation     : 1;
            bool hasPitch           : 1;
            bool hasSpline          : 1;
            bool hasSplineElevation : 1;
            bool hasTimeStamp       : 1;
            bool hasTransportTime2  : 1;
            bool hasTransportTime3  : 1;
        };

        JumpInfo const& GetJumpInfo() const { return jump; }
        StatusInfo const& GetStatusInfo() const { return si; }
        float GetSplineElevation() const { return splineElevation; }
        float GetPitch() const { return s_pitch; }
        bool GetUnknownBit148() const { return unknownBit148; }
        bool GetUnknownBit149() const { return unknownBit149; }
        bool GetUnknownBit172() const { return unknownBit172; }
        std::vector<uint32> const& GetMovementForceIds() const { return movementForceIds; }
        bool HasUnknownUInt32() const { return hasUnknownUInt32; }
        uint32 GetUnknownUInt32() const { return unknownUInt32; }
        bool HasNonZeroBitPadding() const { return nonZeroBitPadding; }

    private:
        // common
        ObjectGuid guid;
        ObjectGuid guid2;
        uint32   moveFlags;                                 // see enum MovementFlags
        uint16   moveFlags2;                                // see enum MovementFlags2
        uint32   time;
        Position pos;
        // transport
        ObjectGuid t_guid;
        Position t_pos;
        uint32   t_time;
        int8     t_seat;
        uint32   t_time2;
        uint32   t_time3;
        // swimming and flying
        float    s_pitch;
        // last fall time
        uint32   fallTime;
        // jumping
        JumpInfo jump;
        // spline
        float    splineElevation;
        bool unknownBit148;
        bool unknownBit149;
        bool unknownBit172;
        uint32 movementCounter;
        uint32 movementForceCount;
        std::vector<uint32> movementForceIds;
        bool hasUnknownUInt32;
        uint32 unknownUInt32;
        bool nonZeroBitPadding;
        // status info
        StatusInfo si;
        int8 byteParam;
        float speedFloat;
};

inline WorldPacket& operator<< (WorldPacket& buf, MovementInfo const& mi)
{
    mi.Write(buf, buf.GetOpcode());
    return buf;
}

inline WorldPacket& operator>> (WorldPacket& buf, MovementInfo& mi)
{
    mi.Read(buf, buf.GetOpcode());
    return buf;
}

namespace Movement
{
    class MoveSpline;
}

/**
 * The different available diminishing return levels.
 * \see DiminishingReturn
 */
enum DiminishingLevels
{
    DIMINISHING_LEVEL_1             = 0,         ///<Won't make a difference to stun duration
    DIMINISHING_LEVEL_2             = 1,         ///<Reduces stun time by 50%
    DIMINISHING_LEVEL_3             = 2,         ///<Reduces stun time by 75%
    DIMINISHING_LEVEL_IMMUNE        = 3          ///<The target is immune to the DiminishingGrouop
};

/**
 * Structure to keep track of diminishing returns, for more information
 * about the idea behind diminishing returns, see: http://www.wowwiki.com/Diminishing_returns
 * \see Unit::GetDiminishing
 * \see Unit::IncrDiminishing
 * \see Unit::ApplyDiminishingToDuration
 * \see Unit::ApplyDiminishingAura
 */
struct DiminishingReturn
{
    DiminishingReturn(DiminishingGroup group, uint32 t, uint32 count)
        : DRGroup(group), stack(0), hitTime(t), hitCount(count)
    {}

    /**
     * Group that this diminishing return will affect
     */
    DiminishingGroup        DRGroup: 16;
    /**
     * Seems to be how many times this has been stacked, modified in
     * Unit::ApplyDiminishingAura
     */
    uint16                  stack: 16;
    /**
     * Records at what time the last hit with this DiminishingGroup was done, if it's
     * higher than 15 seconds (ie: 15 000 ms) the DiminishingReturn::hitCount will be reset
     * to DiminishingLevels::DIMINISHING_LEVEL_1, which will do no difference to the duration
     * of the stun etc.
     */
    uint32                  hitTime;
    /**
     * Records how many times a spell of this DiminishingGroup has hit, this in turn
     * decides how how long the duration of the stun etc is.
     */
    uint32                  hitCount;
};

/**
 * At least some values expected fixed and used in auras field, other custom
 */
enum MeleeHitOutcome
{
    MELEE_HIT_EVADE     = 0,
    MELEE_HIT_MISS      = 1,
    MELEE_HIT_DODGE     = 2,     ///< used as misc in SPELL_AURA_IGNORE_COMBAT_RESULT
    MELEE_HIT_BLOCK     = 3,     ///< used as misc in SPELL_AURA_IGNORE_COMBAT_RESULT
    MELEE_HIT_PARRY     = 4,     ///< used as misc in SPELL_AURA_IGNORE_COMBAT_RESULT
    MELEE_HIT_GLANCING  = 5,
    MELEE_HIT_CRIT      = 6,
    MELEE_HIT_CRUSHING  = 7,
    MELEE_HIT_NORMAL    = 8,
};

struct CleanDamage
{
    CleanDamage(uint32 _damage, WeaponAttackType _attackType, MeleeHitOutcome _hitOutCome) :
        damage(_damage), attackType(_attackType), hitOutCome(_hitOutCome) {}

    uint32 damage;
    WeaponAttackType attackType;
    MeleeHitOutcome hitOutCome;
};

/**
 * Struct for use in Unit::CalculateMeleeDamage
 * Need create structure like in SMSG_ATTACKERSTATEUPDATE opcode
 */
struct CalcDamageInfo
{
    /// Attacker
    Unit*  attacker;
    /// Target for damage
    Unit*  target;
    SpellSchoolMask damageSchoolMask;
    /// How much damage was actually done
    uint32 damage;
    /// How much damage that was absorbed
    uint32 absorb;
    /// How much of the damage that was resisted
    uint32 resist;
    /// How much of the damage that was blocked
    uint32 blocked_amount;
    /**
     * Bitmask of the possible HitInfo flags
     * \see HitInfo
     */
    uint32 HitInfo;
    /**
     * What state the target is in, ie: is he evading or deflecting the hit?
     * \see VictimState
     */
    uint32 TargetState;
    /**
     * Tells how the target was attacked
     */
    WeaponAttackType attackType;
    /**
     * Proc flags of the attacker that should have a chance to trigger, ie: successful
     * melee hit
     * \see ProcFlags
     */
    uint32 procAttacker;
    /**
     * Proc flags of the victim that should have a change to trigger, ie: successful
     * block
     * \see ProcFlags
     */
    uint32 procVictim;
    /**
     * Extra proc flags?
     * TODO: Used for what?
     */
    uint32 procEx;
    /// Used only for rage calculation
    uint32 cleanDamage;
    /// (Old comment) TODO: remove this field (need use TargetState)
    MeleeHitOutcome hitOutCome;
};

/**
 * Spell damage info structure based on structure sending in SMSG_SPELLNONMELEEDAMAGELOG opcode
 */
struct SpellNonMeleeDamage
{
    SpellNonMeleeDamage(Unit* _attacker, Unit* _target, uint32 _SpellID, SpellSchoolMask _schoolMask)
        : target(_target), attacker(_attacker), SpellID(_SpellID), damage(0), schoolMask(_schoolMask),
          absorb(0), resist(0), physicalLog(false), unused(false), blocked(0), HitInfo(0)
    {}

    Unit*   target;
    Unit*   attacker;
    uint32 SpellID;
    uint32 damage;
    SpellSchoolMask schoolMask;
    uint32 absorb;
    uint32 resist;
    bool   physicalLog;
    bool   unused;
    uint32 blocked;
    uint32 HitInfo;
};

/**
 * Used as a convenience struct for the \ref Unit::SendPeriodicAuraLog
 * \todo Is it used in more places? Check SpellAuras.cpp for some examples and document it
 */
struct SpellPeriodicAuraLogInfo
{
    SpellPeriodicAuraLogInfo(Aura* _aura, uint32 _damage, uint32 _overDamage, uint32 _absorb, uint32 _resist, float _multiplier, bool _critical = false)
        : aura(_aura), damage(_damage), overDamage(_overDamage), absorb(_absorb), resist(_resist), multiplier(_multiplier), critical(_critical) {}

    Aura*   aura;       ///< The \ref Aura in question
    uint32 damage;      ///< How much damage this does or how much it adds if it's a positive \ref Aura
    uint32 overDamage;                                      // overkill/overheal
    uint32 absorb;      ///< The amount that was absorbed
    uint32 resist;      ///< The amount that was resisted
    float  multiplier;  ///< The multiplier for gain, ie if it's higher you gain more probably
    bool   critical;
};

/**
 * Creates the extended proc mask for a spell damage event and miss result.
 */
uint32 createProcExtendMask(SpellNonMeleeDamage* damageInfo, SpellMissInfo missCondition);

enum SpellAuraProcResult
{
    SPELL_AURA_PROC_OK              = 0,                    // proc was processed, will remove charges
    SPELL_AURA_PROC_FAILED          = 1,                    // proc failed - if at least one aura failed the proc, charges won't be taken
    SPELL_AURA_PROC_CANT_TRIGGER    = 2                     // aura can't trigger - skip charges taking, move to next aura if exists
};

typedef SpellAuraProcResult(Unit::*pAuraProcHandler)(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
extern pAuraProcHandler AuraProcHandler[TOTAL_AURAS];

#define MAX_DECLINED_NAME_CASES 5

struct DeclinedName
{
    std::string name[MAX_DECLINED_NAME_CASES];
};

enum CurrentSpellTypes
{
    CURRENT_MELEE_SPELL             = 0,
    CURRENT_GENERIC_SPELL           = 1,
    CURRENT_AUTOREPEAT_SPELL        = 2,
    CURRENT_CHANNELED_SPELL         = 3
};

#define CURRENT_FIRST_NON_MELEE_SPELL 1
#define CURRENT_MAX_SPELL             4

struct GlobalCooldown
{
    explicit GlobalCooldown(uint32 _dur = 0, uint32 _time = 0) : duration(_dur), cast_time(_time) {}

    uint32 duration;
    uint32 cast_time;
};

typedef std::unordered_map < uint32 /*category*/, GlobalCooldown > GlobalCooldownList;

class GlobalCooldownMgr                                     // Shared by Player and CharmInfo
{
    public:
        GlobalCooldownMgr() {}

    public:
        bool HasGlobalCooldown(SpellEntry const* spellInfo) const;
        void AddGlobalCooldown(SpellEntry const* spellInfo, uint32 gcd);
        void CancelGlobalCooldown(SpellEntry const* spellInfo);

    private:
        GlobalCooldownList m_GlobalCooldowns;
};

enum ActiveStates
{
    ACT_PASSIVE  = 0x01,                                    // 0x01 - passive
    ACT_DISABLED = 0x81,                                    // 0x80 - castable
    ACT_ENABLED  = 0xC1,                                    // 0x40 | 0x80 - auto cast + castable
    ACT_COMMAND  = 0x07,                                    // 0x01 | 0x02 | 0x04
    ACT_REACTION = 0x06,                                    // 0x02 | 0x04
    ACT_DECIDE   = 0x00                                     // custom
};

enum ReactStates
{
    REACT_PASSIVE    = 0,
    REACT_DEFENSIVE  = 1,
    REACT_AGGRESSIVE = 2
};

enum CommandStates
{
    COMMAND_STAY    = 0,
    COMMAND_FOLLOW  = 1,
    COMMAND_ATTACK  = 2,
    COMMAND_ABANDON = 3,
    COMMAND_MOVE_TO = 4                                     // 18414, ground-targeted
};

#define UNIT_ACTION_BUTTON_ACTION(X) (uint32(X) & 0x00FFFFFF)
#define UNIT_ACTION_BUTTON_TYPE(X)   ((uint32(X) & 0xFF000000) >> 24)
#define MAX_UNIT_ACTION_BUTTON_ACTION_VALUE (0x00FFFFFF+1)
#define MAKE_UNIT_ACTION_BUTTON(A,T) (uint32(A) | (uint32(T) << 24))

struct UnitActionBarEntry
{
    UnitActionBarEntry() : packedData(uint32(ACT_DISABLED) << 24) {}

    uint32 packedData;

    // helper
    ActiveStates GetType() const { return ActiveStates(UNIT_ACTION_BUTTON_TYPE(packedData)); }
    uint32 GetAction() const { return UNIT_ACTION_BUTTON_ACTION(packedData); }
    bool IsActionBarForSpell() const
    {
        ActiveStates Type = GetType();
        return Type == ACT_DISABLED || Type == ACT_ENABLED || Type == ACT_PASSIVE;
    }

    void SetActionAndType(uint32 action, ActiveStates type)
    {
        packedData = MAKE_UNIT_ACTION_BUTTON(action, type);
    }

    void SetType(ActiveStates type)
    {
        packedData = MAKE_UNIT_ACTION_BUTTON(UNIT_ACTION_BUTTON_ACTION(packedData), type);
    }

    void SetAction(uint32 action)
    {
        packedData = (packedData & 0xFF000000) | UNIT_ACTION_BUTTON_ACTION(action);
    }
};

typedef UnitActionBarEntry CharmSpellEntry;

enum ActionBarIndex
{
    ACTION_BAR_INDEX_START = 0,
    ACTION_BAR_INDEX_PET_SPELL_START = 3,
    ACTION_BAR_INDEX_PET_SPELL_END = 7,
    ACTION_BAR_INDEX_END = 10,
};

#define MAX_UNIT_ACTION_BAR_INDEX (ACTION_BAR_INDEX_END-ACTION_BAR_INDEX_START)

/**
 * This structure/class is used when someone is charming (ie: mind control spell and the like)
 * someone else, to get the charmed ones action bar, the spells and such. It also takes care
 * of pets the charmed one has etc.
 */
struct CharmInfo
{
    public:
        explicit CharmInfo(Unit* unit);
        uint32 GetPetNumber() const { return m_petnumber; }
        void SetPetNumber(uint32 petnumber, bool statwindow);

        void SetCommandState(CommandStates st) { m_CommandState = st; }
        CommandStates GetCommandState() { return m_CommandState; }
        bool HasCommandState(CommandStates state) { return (m_CommandState == state); }
        void SetReactState(ReactStates st) { m_reactState = st; }
        ReactStates GetReactState() { return m_reactState; }
        bool HasReactState(ReactStates state) { return (m_reactState == state); }

        void InitVehicleCreateSpells();
        void InitPossessCreateSpells();
        void InitCharmCreateSpells();
        void InitPetActionBar();
        void InitEmptyActionBar();

        // return true if successful
        bool AddSpellToActionBar(uint32 spellid, ActiveStates newstate = ACT_DECIDE);
        bool RemoveSpellFromActionBar(uint32 spell_id);
        void LoadPetActionBar(const std::string& data);
        void BuildActionBar(WorldPacket* data);
        void SetSpellAutocast(uint32 spell_id, bool state);
        void SetActionBar(uint8 index, uint32 spellOrAction, ActiveStates type)
        {
            PetActionBar[index].SetActionAndType(spellOrAction, type);
        }
        UnitActionBarEntry const* GetActionBarEntry(uint8 index) const { return &(PetActionBar[index]); }

        bool CanToggleCreatureAutocast(uint32 spellId) const;
        void ToggleCreatureAutocast(uint32 spellid, bool apply);

        CharmSpellEntry* GetCharmSpell(uint8 index) { return &(m_charmspells[index]); }

        GlobalCooldownMgr& GetGlobalCooldownMgr() { return m_GlobalCooldownMgr; }

    private:
        Unit* m_unit;
        UnitActionBarEntry PetActionBar[MAX_UNIT_ACTION_BAR_INDEX];
        CharmSpellEntry m_charmspells[CREATURE_MAX_SPELLS];
        CommandStates   m_CommandState;
        ReactStates     m_reactState;
        uint32          m_petnumber;
        GlobalCooldownMgr m_GlobalCooldownMgr;
};

// used in CallForAllControlledUnits/CheckAllControlledUnits
enum ControlledUnitMask
{
    CONTROLLED_PET       = 0x01,
    CONTROLLED_MINIPET   = 0x02,
    CONTROLLED_GUARDIANS = 0x04,                            // including PROTECTOR_PET
    CONTROLLED_CHARM     = 0x08,
    CONTROLLED_TOTEMS    = 0x10,
};

// for clearing special attacks
#define REACTIVE_TIMER_START 4000

enum ReactiveType
{
    REACTIVE_DEFENSE      = 0,
    REACTIVE_HUNTER_PARRY = 1,
    REACTIVE_OVERPOWER    = 2
};

#define MAX_REACTIVE 3

// Used as MiscValue for SPELL_AURA_IGNORE_UNIT_STATE
enum IgnoreUnitState
{
    IGNORE_UNIT_TARGET_STATE      = 0,                      // target health, aura states, or combopoints
    IGNORE_UNIT_COMBAT_STATE      = 1,                      // ignore caster in combat state
    IGNORE_UNIT_TARGET_NON_FROZEN = 126,                    // ignore absent of frozen state
};

// delay time next attack to prevent client attack animation problems
#define ATTACK_DISPLAY_DELAY 200
#define MAX_PLAYER_STEALTH_DETECT_RANGE 45.0f               // max distance for detection targets by player
#define MAX_CREATURE_ATTACK_RADIUS 45.0f                    // max distance for creature aggro (use with CONFIG_FLOAT_RATE_CREATURE_AGGRO)

// Regeneration defines
#define REGEN_TIME_FULL         2000                        // This determines how often regen value is computed
#define REGEN_TIME_PRECISE      500                         // Used in Spell::CheckPower for precise regeneration in spell cast time
#define REGEN_TIME_HOLY_POWER   10000                       // This determines how often holy power regen is processed

// Power type values defines
enum PowerDefaults
{
    POWER_RAGE_DEFAULT              = 1000,
    POWER_FOCUS_DEFAULT             = 100,
    POWER_ENERGY_DEFAULT            = 100,
    POWER_RUNE_DEFAULT              = 8,
    POWER_RUNIC_POWER_DEFAULT       = 1000,
    POWER_HOLY_POWER_DEFAULT        = 3,
    POWER_SOUL_SHARDS_DEFAULT       = 3,
};

struct SpellProcEventEntry;                                 // used only privately

#define MAX_OBJECT_SLOT 5

class Unit : public WorldObject
{
    public:
        typedef std::set<Unit*> AttackerSet;
        /**
         * A multimap from spell ids to \ref SpellAuraHolder, multiple \ref SpellAuraHolder can have
         * the same id (ie: the same key)
         */
        typedef std::multimap < uint32 /*spellId*/, SpellAuraHolder* > SpellAuraHolderMap;
        /**
         * A pair of two iterators to a \ref SpellAuraHolderMap which is used in conjunction
         * with the std::multimap::equal_range which gives all \ref SpellAuraHolder that have the same
         * spellid in this case, the first member is the iterator to the beginning, and the
         * second member is the iterator to the end.
         */
        typedef std::pair<SpellAuraHolderMap::iterator, SpellAuraHolderMap::iterator> SpellAuraHolderBounds;
        /// Same thing as \ref SpellAuraHolderBounds but with const_iterator instead of iterator
        typedef std::pair<SpellAuraHolderMap::const_iterator, SpellAuraHolderMap::const_iterator> SpellAuraHolderConstBounds;
        typedef std::list<SpellAuraHolder*> SpellAuraHolderList;
        /**
         * List of \ref Aura used in \ref Unit::GetAurasByType and more and also in the members
         * \ref Unit::m_modAuras and \ref Unit::m_deletedAuras
         * \see Aura
         */
        typedef std::list<Aura*> AuraList;
        /**
         * List of \ref DiminishingReturn used for calculation of the same thing.
         * \see DiminishingReturn
         * \see DiminishingLevels
         * \see Unit::GetDiminishing
         * \see Unit::IncrDiminishing
         * \see Unit::ApplyDiminishingToDuration
         */
        typedef std::list<DiminishingReturn> Diminishing;
        typedef std::set < uint32 /*playerGuidLow*/ > ComboPointHolderSet;
        typedef std::map<uint8 /*slot*/, SpellAuraHolder* /*spellId*/> VisibleAuraMap;
        typedef std::map < SpellEntry const*, ObjectGuid /*targetGuid*/ > TrackedAuraTargetMap;

        virtual ~Unit();

        //These are probably interesting for learning how/when objects/units get added to the
        //world, take a look at them and write something tutorialishy about it to introduce
        //more of how the core works?
        void AddToWorld() override;
        void RemoveFromWorld() override;

        /**
         * Used in ~Creature/~Player (or before mass creature delete to remove
         * cross-references to already deleted units). (Taken from comment in source)
         * \todo Add more information here
         */
        void CleanupsBeforeDelete() override;

        float GetObjectBoundingRadius() const override      // overwrite WorldObject version
        {
            return m_floatValues[UNIT_FIELD_BOUNDINGRADIUS];
        }

        /**
         * Gets the current DiminishingLevels for the given group
         * @param group The group that you would like to know the current diminishing return level for
         * @return The current diminishing level, up to DiminishingLevels::DIMINISHING_LEVEL_IMMUNE
         */
        DiminishingLevels GetDiminishing(DiminishingGroup  group);
        /**
         * Increases the level of the DiminishingGroup by one level up until
         * DIMINISHING_LEVEL_IMMUNE where the target becomes immune to spells of
         * that DiminishingGroup
         * @param group The group to increase the level for by one
         */
        void IncrDiminishing(DiminishingGroup group);
        /**
         * Calculates how long the duration of a spell should be considering
         * diminishing returns, ie, if the Level passed in is DIMINISHING_LEVEL_IMMUNE
         * then the duration will be zeroed out. If it is DIMINISHING_LEVEL_1 then a full
         * duration will be used
         * @param group The group to affect
         * @param duration The duration to be changed, will be updated with the new duration
         * @param caster Who's casting the spell, used to decide whether anything should be calculated
         * @param Level The current level of diminishing returns for the group, decides the new duration
         * @param limitduration
         * @param isReflected Whether the spell was reflected or not, used to determine if we should do any calculations at all.
         */
        void ApplyDiminishingToDuration(DiminishingGroup  group, int32& duration, Unit* caster, DiminishingLevels Level, int32 limitduration, bool isReflected);
        /**
         * Applies a diminishing return to the given group if apply is true,
         * otherwise lowers the level by one (?)
         * @param group The group to affect
         * @param apply whether this aura is being added/removed
         */
        void ApplyDiminishingAura(DiminishingGroup  group, bool apply);
        /**
         * Clears all the current diminishing returns for this Unit.
         */
        void ClearDiminishings() { m_Diminishing.clear(); }

        void Update(uint32 update_diff, uint32 time) override;

        /**
         * Updates the attack time for the given WeaponAttackType
         * @param type The type of weapon that we want to update the time for
         * @param time the remaining time until we can attack with the WeaponAttackType again
         */
        void setAttackTimer(WeaponAttackType type, uint32 time) { m_attackTimer[type] = time; }
        /**
         * Resets the attack timer to the base value decided by Unit::m_modAttackSpeedPct and
         * Unit::GetAttackTime
         * @param type The weapon attack type to reset the attack timer for.
         */
        void resetAttackTimer(WeaponAttackType type = BASE_ATTACK);
        /**
         * Get's the remaining time until we can do an attack
         * @param type The weapon type to check the remaining time for
         * @return The remaining time until we can attack with this weapon type.
         */
        uint32 getAttackTimer(WeaponAttackType type) const { return m_attackTimer[type]; }
        /**
         * Checks whether the unit can do an attack. Does this by checking the attacktimer for the
         * WeaponAttackType, can probably be thought of as a cooldown for each swing/shot
         * @param type What weapon should we check for
         * @return true if the Unit::m_attackTimer is zero for the given WeaponAttackType
         */
        bool isAttackReady(WeaponAttackType type = BASE_ATTACK) const { return m_attackTimer[type] == 0; }
        /**
         * Checks if the current Unit has an offhand weapon
         * @return True if there is a offhand weapon.
         */
        bool haveOffhandWeapon() const;
        /**
         * Does an attack if any of the timers allow it and resets them, if the user
         * isn't in range or behind the target an error is sent to the client.
         * Also makes sure to not make and offhand and mainhand attack at the same
         * time. Only handles non-spells ie melee attacks.
         * @return True if an attack was made and no error happened, false otherwise
         */
        bool UpdateMeleeAttackingState();
        /**
         * Check is a given equipped weapon can be used, ie the mainhand, offhand etc.
         * @param attackType The attack type to check, ie: main/offhand/ranged
         * @return True if the weapon can be used, true except for shapeshifts and if disarmed.
         */
        bool CanUseEquippedWeapon(WeaponAttackType attackType) const
        {
            if (IsInFeralForm())
            {
                return false;
            }

            switch (attackType)
            {
                default:
                case BASE_ATTACK:
                    return !HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISARMED);
                case OFF_ATTACK:
                    return !HasFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_DISARM_OFFHAND);
                case RANGED_ATTACK:
                    return !HasFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_DISARM_RANGED);
            }
        }

        /**
         * Returns the combined combat reach of two mobs. Can be seen as a radius.
         * @param pVictim The other unit to add the range for
         * @param forMeleeRange Whether we should return the combined reach for melee or not
         * @param flat_mod Increases the returned reach by this value.
         * @return The combined values of UNIT_FIELD_COMBATREACH for both this unit and the pVictim.
         * \see EUnitFields
         * \see GetFloatValue
         */
        float GetCombatReach(Unit const* pVictim, bool forMeleeRange = true, float flat_mod = 0.0f) const;
        /**
         * Returns the remaining combat distance between two mobs (CombatReach substracted).
         * Does this by getting the radius of combat/aggro between them and then subtracting their
         * actual distance between them. Ie: dist between - radius for aggro. If this becomes less
         * than zero zero is returned and the mobs should probably aggro each other/the player
         * @param target The target to check against
         * @param forMeleeRange If we want to check melee range instead
         * @return The reach between them left until one of the creatures could/should aggro
         */
        float GetCombatDistance(Unit const* target, bool forMeleeRange) const;
        /**
         * Returns if the Unit can reach a victim with Melee Attack. Does so by using
         * Unit::GetCombatReach for melee and checking if the distance from the target is less than
         * the reach.
         * @param pVictim Who we want to reach with a melee attack.
         * @param flat_mod The same as sent to Unit::GetCombatReach
         * @return true if we can reach pVictim with a melee attack
         */
        bool CanReachWithMeleeAttack(Unit const* pVictim, float flat_mod = 0.0f) const;
        uint32 m_extraAttacks;

        /**
         * Internal function, must only be called from Unit::Attack(Unit*)
         * @param pAttacker The attacker to add to current attackers.
         */
        void _addAttacker(Unit* pAttacker)                  // must be called only from Unit::Attack(Unit*)
        {
            AttackerSet::const_iterator itr = m_attackers.find(pAttacker);
            if (itr == m_attackers.end())
            {
                m_attackers.insert(pAttacker);
            }
        }
        /**
         * Internal function, must only be called from Unit::AttackStop()
         * @param pAttacker
         */
        void _removeAttacker(Unit* pAttacker)               // must be called only from Unit::AttackStop()
        {
            m_attackers.erase(pAttacker);
        }
        /**
         * If another mob/unit want to help this mob this function will return a
         * possible Unit to attack.
         * @return A Unit to attack if this one is being attacked by anyone, NULL otherwise
         */
        Unit* getAttackerForHelper()                        // If someone wants to help, who to give them
        {
            if (getVictim() != NULL)
            {
                return getVictim();
            }

            if (!m_attackers.empty())
            {
                return *(m_attackers.begin());
            }

            return NULL;
        }
        /**
         * Tries to attack a Unit/Player, also makes sure to stop attacking the current target
         * if we're already attacking someone.
         * @param victim The Unit to attack
         * @param meleeAttack Whether we should attack with melee or ranged/magic
         * @return True if an attack was initiated, false otherwise
         */
        bool Attack(Unit* victim, bool meleeAttack);
        /**
         * Called when we are attack by someone in someway, might be when a fear runs out and
         * we want to notify AI to attack again or when a spell hits.
         * @param attacker Who's attacking us
         */
        void AttackedBy(Unit* attacker);
        /**
         * Stop all spells from casting except the one give by except_spellid
         * @param except_spellid This spell id will not be stopped from casting, defaults to 0
         * \see Unit::InterruptSpell
         */
        void CastStop(uint32 except_spellid = 0);
        /**
         * Stops attacking whatever we are attacking at the moment and tells the Unit we are attacking
         * that we are not doing that anymore, ie: removes it from the attacker list
         * @param targetSwitch if we are switching targets or not, defaults to false
         * @return false if we weren't attacking already, true otherwise
         * \see Unit::m_attacking
         */
        bool AttackStop(bool targetSwitch = false);
        /**
         * Removes all attackers from the Unit::m_attackers set and logs it if someone that
         * wasn't attacking it was in the list. Does this check by checking if Unit::AttackStop()
         * returned false.
         * \see Unit::AttackStop
         */
        void RemoveAllAttackers();
        /**
         * @return The Unit::m_attackers, ie. the units that are attacking you
         */
        AttackerSet const& getAttackers() const { return m_attackers; }
        /**
         * Checks if we are attacking a player, also, pets/minions etc attacking a player counts
         * towards you attacking a player.
         * @return true if you and/or your pets/minions etc are attacking a player.
         * \todo Rename to IsAttackingPlayer to follow naming conventions?
         */
        bool isAttackingPlayer() const;
        /**
         * Checks if a vehicle is allowed to attack other units by itself.
         * @return true if a vehicle can attack other units by itself (without any controller)
         */
        bool CanAttackByItself() const;
        /**
         * @return The victim that you are currently attacking
         */
        Unit* getVictim() const { return m_attacking; }
        /**
         * Stops a unit from combat, removes all attackers and stops attacking.
         * @param includingCast if we should stop the currently casting spell aswell
         */
        void CombatStop(bool includingCast = false);
        /**
         * Calls Unit::CombatStop to stop combat, also calls Unit::CombatStop for pets etc. by using
         * Unit::CallForAllControlledUnits
         * @param includingCast if we should stop the currently casting spell aswell
         * \see Unit::CallForAllControlledUnits
         * \see Unit::CheckForAllControlledUnits
         */
        void CombatStopWithPets(bool includingCast = false);
        /**
         * Stops attacking a certain faction. If we are attacking something and are a player we
         * are forcefully stopped from attacking the target aswell.
         * @param faction_id The faction to stop attacking
         * \see Unit::CallForAllControlledUnits
         * \see Unit::CheckForAllControlledUnits
         * \see Unit::getAttackers
         */
        void StopAttackFaction(uint32 faction_id);
        /**
         * Selects a random unfriendly target, takes care of LOS and such aswell
         * @param except select any target but this one, usually your current target
         * @param radius how big the radius for our search should be
         * @return The random unfriendly target found, NULL if no targets were found
         * \see MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck
         * \see Mangos::UnitListSearcher
         * \see Cell::VisitAllObjects
         */
        Unit* SelectRandomUnfriendlyTarget(Unit* except = NULL, float radius = ATTACK_DISTANCE) const;
        /**
         * Same as Unit::SelectRandomUnfriendlyTarget except it selects a friendly target
         * @param except select any target but this one, usually your current target
         * @param radius how big the radius for our search should be
         * @return The random friendly target found, NULL if no targets were found
         * \see MaNGOS::AnyFriendlyUnitInObjectRangeCheck
         * \see Mangos::UnitListSearcher
         * \see Cell::VisitAllObjects
         */
        Unit* SelectRandomFriendlyTarget(Unit* except = NULL, float radius = ATTACK_DISTANCE) const;
        /**
         * Checks if we have a negative aura with the given interrupt flag/s
         * @param flag The interrupt flag/s to check for, see SpellAuraInterruptFlags
         * @return true if we have a negative aura with the given flag, false otherwise
         * \see SpellAuraInterruptFlags
         */
        bool hasNegativeAuraWithInterruptFlag(uint32 flag);
        /**
         * Sends a packet to the client informing it that melee attacks are stopping
         * @param victim The unit we stopped attacking
         * \see OpcodesList
         */
        void SendMeleeAttackStop(Unit* victim);
        /**
         * Sends a packet to the client informing it that melee attacks are starting
         * @param pVictim the target that we attack with melee
         */
        void SendMeleeAttackStart(Unit* pVictim);

        /**
         * Adds a state to this unit
         * @param f the state to add, see UnitState for possible values
         * \see UnitState
         */
        void addUnitState(uint32 f) { m_state |= f; }
        /**
         * Checks if a certain unit state is set
         * @param f the state to check for
         * @return true if the state is set, false otherwise
         * \see UnitState
         */
        bool hasUnitState(uint32 f) const { return (m_state & f); }
        /**
         * Unsets a certain unit state
         * @param f the state to remove
         * \see UnitState
         */
        void clearUnitState(uint32 f) { m_state &= ~f; }
        /**
         * Checks if the client/mob is in control or no
         * @return true if the client can move by client control, false otherwise
         * \see UnitState
         */
        bool CanFreeMove() const
        {
            return !hasUnitState(UNIT_STAT_NO_FREE_MOVE) && !GetOwnerGuid();
        }

        /**
         * Gets the level for this unit
         * @return The current level for this unit
         * \see GetUInt32Value
         * \see EUnitFields
         */
        uint32 getLevel() const { return GetUInt32Value(UNIT_FIELD_LEVEL); }
        /**
         * TODO: What does it actually do? Is overwritten by others that derive from Unit?
         * @return The level it would seem
         */
        virtual uint32 GetLevelForTarget(Unit const* /*target*/) const { return getLevel(); }
        /**
         * Updates the level for the current Unit. Also updates the group to know about this.
         * @param lvl The level to change to
         * \see EUnitFields
         * \see SetUInt32Value
         */
        void SetLevel(uint32 lvl);
        /**
         * Gets the race of this Unit, not to be confused with the Creature type or such
         * @return returns the race of this Unit
         * \see CreatureTypeFlags
         * \see Races
         */
        virtual uint8 getRace() const { return GetByteValue(UNIT_FIELD_BYTES_0, 0); }
        /**
         * Returns a bitmask representation of the current race given by Races, not to be
         * confused with the Creature type or such
         * @return the racemask for the current race
         * \see CreatureTypeFlags
         * \see Races
         */
        uint32 getRaceMask() const { return getRace() ? 1 << (getRace() - 1) : 0; }
        /**
         * Returns the class of this Unit
         * @return the class of the Unit
         * \see Classes
         */
        uint8 getClass() const { return GetByteValue(UNIT_FIELD_BYTES_0, 1); }
        /**
         * Returns a bitmask representation of the current class given by Classes
         * @return the classmask for the class
         * \see Classes
         */
        uint32 getClassMask() const { return 1 << (getClass() - 1); }
        /**
         * Gives you the current gender of this Unit
         * @return The current gender
         * \see Gender
         */
        uint8 getGender() const { return GetByteValue(UNIT_FIELD_BYTES_0, 2); }

        /**
         * Gets a stat for the current Unit
         * @param stat The stat you want to get, ie: Stats::STAT_STRENGTH
         * @return the value the given stat has
         * \see Stats
         */
        float GetStat(Stats stat) const { return float(GetUInt32Value(UNIT_FIELD_STAT0 + stat)); }
        /**
         * Sets a stat for this Unit
         * @param stat the stat to change
         * @param val the value to change it to
         * \see Stats
         */
        void SetStat(Stats stat, int32 val) { SetStatInt32Value(UNIT_FIELD_STAT0 + stat, val); }
        /**
         * Gets the armor for this Unit
         * @return the current armor
         * \see SpellSchools
         */
        uint32 GetArmor() const { return GetResistance(SPELL_SCHOOL_NORMAL) ; }
        /**
         * Sets the armor for this Unit
         * @param val the value to set the armor to
         * \see SpellSchools
         */
        void SetArmor(int32 val) { SetResistance(SPELL_SCHOOL_NORMAL, val); }

        /**
         * Gets the resistance against a certain spell school, ie: fire, frost, nature etc
         * @param school the type of resistance you want to get
         * @return the current resistance against the given school
         */
        uint32 GetResistance(SpellSchools school) const { return GetUInt32Value(UNIT_FIELD_RESISTANCES + school); }
        /**
         * Sets a resistance for this Unit
         * @param school the type of resistance you want to set
         * @param val the value to set it to
         */
        void SetResistance(SpellSchools school, int32 val) { SetStatInt32Value(UNIT_FIELD_RESISTANCES + school, val); }

        /**
         * Gets the health of this Unit
         * @return the current health for this unit
         * \see EUnitFields
         * \see GetUInt32Value
         */
        uint32 GetHealth()    const { return GetUInt32Value(UNIT_FIELD_HEALTH); }
        /**
         * Gets the maximum health of this Unit
         * @return the max health this Unit can have
         * \see EUnitFields
         * \see GetUInt32Value
         */
        uint32 GetMaxHealth() const { return GetUInt32Value(UNIT_FIELD_MAXHEALTH); }
        /**
         * Gets the percent of the health. The formula: (GetHealth() * 100) / GetMaxHealth()
         * @return the current percent of the health
         * \see GetHealth
         * \see GetMaxHealth
         */
        float GetHealthPercent() const { return GetMaxHealth() ? (GetHealth() * 100.0f) / GetMaxHealth() : 0.0f; }
        uint32 CountPctFromMaxHealth(int32 pct) const { return (GetMaxHealth() * static_cast<float>(pct) / 100.0f); }
        uint32 CountPctFromCurHealth(int32 pct) const { return (GetHealth() * static_cast<float>(pct) / 100.0f); }
        /**
         * Sets the health to the given value, it cant be higher than Unit::GetMaxHealth though
         * @param val the value to set the health to
         */
        void SetHealth(uint32 val);
        /**
         * Sets the max health for this Unit, also makes sure to update the party with the new
         * value
         * @param val the new max value for the health
         * \see SetHealth
         * \see GetMaxHealth
         */
        void SetMaxHealth(uint32 val);
        /**
         * Sets the health to a certain percentage
         * @param percent the new percent to change it to, ie: 50.0f, not 0.5f for 50%
         */
        void SetHealthPercent(float percent);
        /**
         * Modifies the health by the difference given. If the character had 100 health and we sent in
         * -150 as the amount to decrease it would return -100 as that is how much it decreased since
         * we cant be under 0 health.
         * @param val the difference to apply to the health, ie: -100 would decrease the life by 100
         * @return how much the Unit gained/lost in health.
         */
        int32 ModifyHealth(int32 val);

        // Eluna-related health functions
        bool HealthAbovePctHealed(int32 pct, uint32 heal) const { return uint64(GetHealth()) + uint64(heal) > CountPctFromMaxHealth(pct); }
        bool IsFullHealth() const { return GetHealth() == GetMaxHealth(); }
        bool HealthBelowPct(int32 pct) const { return GetHealth() < CountPctFromMaxHealth(pct); }
        bool HealthBelowPctDamaged(int32 pct, uint32 damage) const { return int64(GetHealth()) - int64(damage) < int64(CountPctFromMaxHealth(pct)); }
        bool HealthAbovePct(int32 pct) const { return GetHealth() > CountPctFromMaxHealth(pct); }

        /**
         * Gets the power type for this Unit
         * @return The type of power this Unit uses
         */
        Powers GetPowerType() const { return Powers(GetByteValue(UNIT_FIELD_BYTES_0, 3)); }
        void SetPowerType(Powers power);
        uint32 GetPower(Powers power) const;
        uint32 GetPowerByIndex(uint32 index) const;
        uint32 GetMaxPower(Powers power) const;
        uint32 GetMaxPowerByIndex(uint32 index) const;
        void SetPowerByIndex(uint32 power, int32 val);
        void SetMaxPowerByIndex(uint32 power, int32 val);
        void SetPower(Powers power, int32 val);
        void SetMaxPower(Powers power, int32 val);
        int32 ModifyPower(Powers power, int32 val);
        /**
         * Mods a power by increasing or decreasing it's value
         * @param power which power to mod
         * @param val how much to increase/decrease the given power
         * @param apply whether to apply or remove the mod
         * \see ApplyModUInt32Value
         */
        void ApplyPowerMod(Powers power, uint32 val, bool apply);
        /**
         * Changes the possible max value of the given Powers power.
         * @param power increase max for this power
         * @param val what to add/remove to/from the current max
         * @param apply whether to apply it or remove it
         * \see ApplyModUInt32Value
         */
        void ApplyMaxPowerMod(Powers power, uint32 val, bool apply);
        void ResetHolyPowerRegenTimer() { m_holyPowerRegenTimer = REGEN_TIME_HOLY_POWER; }

        static uint32 GetPowerIndexByClass(Powers power, uint32 classId);
        static Powers GetPowerTypeByIndex(uint32 index, uint32 classId);
        uint32 GetPowerIndex(Powers power) const { return GetPowerIndexByClass(power, getClass()); }
        Powers getPowerType(uint32 index) const { return GetPowerTypeByIndex(index, getClass()); }

        /**
         * Gets the attack time until next attack for the given weapon type
         * @param att what attack type we want to get attacktime for
         * @return the current attack time, which takes mods of attack speed into account
         * \see Unit::m_modAttackSpeedPct
         * \see EUnitFields
         * \todo Is the time returned in seconds
         */
        uint32 GetAttackTime(WeaponAttackType att) const { return (uint32)(GetFloatValue(UNIT_FIELD_BASEATTACKTIME + att) / m_modAttackSpeedPct[att]); }
        /**
         * Changes the attack time for a certain weapon type.
         * @param att what attack type we want to change the time for
         * @param val what to set it to
         * \see Unit::m_modAttackSpeedPct
         * \see EUnitFields
         */
        void SetAttackTime(WeaponAttackType att, uint32 val) { SetFloatValue(UNIT_FIELD_BASEATTACKTIME + att, val * m_modAttackSpeedPct[att]); }
        /**
         * Applies a percentage change to a given attack type
         * @param att attack type to mod
         * @param val how many percent to add/remove, ie: 90.0f = 90%
         * @param apply whether to add or remove the effect/mod
         * \see ApplyPercentModFloatVar
         * \see ApplyPercentModFloatValue
         */
        void ApplyAttackTimePercentMod(WeaponAttackType att, float val, bool apply);
        /**
         * Same as ApplyAttackTimePercentMod but for the casting time of spells
         * instead.
         * @param val how many percent to add/remove, ie: 90.0f = 90%
         * @param apply whether to add or remove the effect/mod
         * \see Unit::ApplyAttackTimePercentMod
         */
        void ApplyCastTimePercentMod(float val, bool apply);

        /**
         * Gets the current sheath state, it is whether your main-weapon, ranged-weapon or no
         * weapon is being shown in your hands
         * @return The current sheath state
         */
        SheathState GetSheath() const { return SheathState(GetByteValue(UNIT_FIELD_BYTES_2, 0)); }
        /**
         * Changes the current sheath state.
         * @param sheathed The new weapon or none of them to show
         * \see Unit::GetSheath
         */
        virtual void SetSheath(SheathState sheathed) { SetByteValue(UNIT_FIELD_BYTES_2, 0, sheathed); }

        /**
         * Gets the faction that this unit currently belongs to, also
         * called faction template id it seems. More data probably to
         * be found in the DBC files.
         * @return The faction this unit belongs to
         * \see EUnitFields
         * TODO: Does this link correctly?
         * \see EUnitFields::UNIT_FIELD_FACTIONTEMPLATE
         * \see FactionTemplateEntry
         * \see FactionEntry
         */
        uint32 getFaction() const { return GetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE); }
        /**
         * Changes the faction a unit belongs to.
         * @param faction Faction to change to
         * \see EUnitFields
         * TODO: Does this link correctly?
         * \see EUnitFields::UNIT_FIELD_FACTIONTEMPLATE
         * \see FactionTemplateEntry
         * \see FactionEntry
         */
        void setFaction(uint32 faction) { SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE, faction); }
        FactionTemplateEntry const* getFactionTemplateEntry() const;
        void RestoreOriginalFaction();
        /**
         * Are we hostile towards the given Unit?
         * @param unit the unit we want to check against
         * @return true if the Unit is considered hostile, false otherwise
         */
        bool IsHostileTo(Unit const* unit) const override;
        /**
         * Is this Unit hostile towards players?
         * @return true if the Unit is hostile towards players, false otherwise
         */
        bool IsHostileToPlayers() const;
        /**
         * Is this Unit friendly towards the given Unit?
         * @param unit the Unit to check against
         * @return true if the Unit is considered friendly to us, false otherwise
         */
        bool IsFriendlyTo(Unit const* unit) const override;
        /**
         * Is this Unit neutral to everyone?
         * @return True if considered neutral to everyone, false otherwise.
         */
        bool IsNeutralToAll() const;
        /**
         * Check if this Unit is a guardian of a contested territory, this is
         * useful when we want to know if we should attack all players or only
         * players not belonging to our "side" ally/horde.
         * @return true if this Unit is a guard in a contested area, false otherwise
         */
        bool IsContestedGuard() const
        {
            if (FactionTemplateEntry const* entry = getFactionTemplateEntry())
            {
                return entry->IsContestedGuardFaction();
            }

            return false;
        }
        /**
         * Is PVP enabled?
         * @return true if this Unit is eligible for PVP fighting
         */
        bool IsPvP() const { return HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_PVP); }
        /**
         * Put the Unit into our out of PVP
         * @param state true if we want to set PVP on, false otherwise
         */
        void SetPvP(bool state);
        bool IsFFAPvP() const { return HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_FFA_PVP); }
        void SetFFAPvP(bool state);
        /**
         * Returns the CreatureType for this Unit. For players this most often is
         * CREATURE_TYPE_HUMANOID unless he/she has shapeshifted or something like that.
         * Ie: Bear form probably wouldn't yield the same return value.
         * For creatures though Creature::GetCreatureInfo() is called and the CreatureInfo::type
         * field is used.
         * @return the CreatureType for this Unit
         * \see CreatureType
         */
        uint32 GetCreatureType() const;
        /**
         * Returns a bitmask representation of CreatureType for this Unit.
         * @return A bitmask representation of GetCreatureType()
         */
        uint32 GetCreatureTypeMask() const
        {
            uint32 creatureType = GetCreatureType();
            return (creatureType >= 1) ? (1 << (creatureType - 1)) : 0;
        }

        /**
         * Gets the current stand state for this Unit as described by UnitStandStateType.
         * @return The current stand state
         * \see UnitStandStateType
         * \see MAX_UNIT_STAND_STATE
         */
        uint8 getStandState() const { return GetByteValue(UNIT_FIELD_BYTES_1, 0); }
        /**
         * Is this Unit sitting down in some way?
         * @return true if the Unit is sitting down, false otherwise
         */
        bool IsSitState() const;
        /**
         * Is this Unit just standing normally? This method will return false
         * even if you would consider the state as standing, ie: when the Unit
         * has the state UNIT_STAND_STATE_SLEEP it is considered not standing.
         * @return true if the Unit is standing normally, false otherwise
         */
        bool IsStandState() const;
        /**
         * Change the stand state for this Unit. For possible values check
         * UnitStandStateType.
         * @param state
         * \see UnitStandStateType
         */
        void SetStandState(uint8 state);

        void  SetStandFlags(uint8 flags) { SetByteFlag(UNIT_FIELD_BYTES_1, 2, flags); }
        void  RemoveStandFlags(uint8 flags) { RemoveByteFlag(UNIT_FIELD_BYTES_1, 2, flags); }

        /**
         * Is this Unit mounted?
         * @return true if it's mounted, false otherwise
         * \see EUnitFields
         */
        bool IsMounted() const { return HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_MOUNT); }
        /**
         * Gets the currently used mount id.
         * @return id of the currently used mount
         */
        uint32 GetMountID() const { return GetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID); }
        /**
         * Mounts this Unit by setting the UNIT_FIELD_MOUNTDISPLAYID to the given mount
         * id and setting the bitflag UNIT_FLAG_MOUNT in UNIT_FIELD_FLAGS. If this Unit
         * is a player pets and such are despawned or not depending on the config option
         * CONFIG_BOOL_PET_UNSUMMON_AT_MOUNT.
         * @param mount the id of the mount to mount
         * @param spellId id of the spell used to summon the mount, if 0 is passed in this is treated
         * as a GM command or the Taxi service mounting the Player.
         */
        void Mount(uint32 mount, uint32 spellId = 0);
        /**
         * Unmounts this Unit by sending the SMSG_DISMOUNT to the client if it was a dismount
         * not issued by a GM / the Taxi service. Also changes the UNIT_FIELD_MOUNTDISPLAYID
         * back to 0 and removes the flag UNIT_FLAG_MOUNT from UNIT_FIELD_FLAGS.
         * @param from_aura if this was true the Unit was probably interrupted by a spell
         * or something hitting it forcing a dismount.
         */
        void Unmount(bool from_aura = false);

        MountCapabilityEntry const* GetMountCapability(uint32 mountType) const;

        void PlayOneShotAnimKit(uint32 id);

        VehicleInfo* GetVehicleInfo() { return m_vehicleInfo; }
        bool IsVehicle() const { return m_vehicleInfo != NULL; }
        void SetVehicleId(uint32 entry, uint32 overwriteNpcEntry);

        /**
         * Returns the maximum skill value the given Unit can have. Ie: the sword skill can
         * be maxed to 300 at level 60. And when you start a level 1 character you maximum
         * skill with swords (given that you know them) is 5. The formula used is:
         * Current Level * 5
         * @param target target to get maximum skill value for, if this is NULL the
         * returned value is for ourselves.
         * @return the maximum skill level you can have at the your current level.
         * TODO: Check out the GetLevelForTarget as it seems it's not doing anything constructive
         * with it's arguments.
         */
        uint16 GetMaxSkillValueForLevel(Unit const* target = NULL) const { return (target ? GetLevelForTarget(target) : getLevel()) * 5; }
        /**
         * Deals damage mods to the given victim. If the victim is dead, flying or in evade
         * mode (for creatures) then the damage is absorbed into absorb and no damage
         * is done.
         * @param pVictim
         * @param damage how much damage we want to try to make, will be updated to how
         * much was actually made
         * @param absorb if this is != NULL it will be updated with how much more from
         * before of the damage that was absorbed. ie: absorb += damage not done
         * TODO: Does DamageDeal in the AI's do anything?
         * TODO: Fix this comment, doesn't really seem correct.
         */
        void DealDamageMods(Unit* pVictim, uint32& damage, uint32* absorb);
        /**
         * Generally deals damage to a Unit.
         * @param pVictim victim that will take damage
         * @param damage the damage to make
         * @param cleanDamage melee damage to make
         * @param damagetype the type of damage we'll be doing, ie: DOT, DIRECT_DAMAGE etc.
         * @param damageSchoolMask what school the damage has
         * @param spellProto prototype for the spell that was cast
         * @param durabilityLoss whether this damage should give a durability loss (10%) on death
         * or not
         * @return probably how much damage was actually dealt?
         * TODO: Cleanup this function and split into smaller functions for readability
         */
        uint32 DealDamage(Unit* pVictim, uint32 damage, CleanDamage const* cleanDamage, DamageEffectType damagetype, SpellSchoolMask damageSchoolMask, SpellEntry const* spellProto, bool durabilityLoss);
        /**
         * Generally heals a target for addhealth health
         * @param pVictim the victim to heal
         * @param addhealth how much health to add, modified by Unit::ModifyHealth
         * @param spellProto spell prototype for the spell that made this heal
         * @param critical whether or not this was a critical heal (true => crit)
         * @return how much the target actually gained in health
         */
        int32 DealHeal(Unit* pVictim, uint32 addhealth, SpellEntry const* spellProto, bool critical = false, uint32 absorb = 0);

        /**
         * Calls CallForAllControlledUnits with CONTROLLED_MINIPET and CONTROLLED_GUARDIAN
         * to make them do something if they should when their owner kills someone/thing
         * @param pVictim the target that was killed
         * \see CallForAllControlledUnits
         * \see ControlledUnitMask
         */
        void PetOwnerKilledUnit(Unit* pVictim);

        /**
         * Hard to figure out what this does, TODO: Document this.
         * @param pVictim possible victim of the proc
         * @param procAttacker
         * @param procVictim
         * @param procEx
         * @param amount
         * @param attType
         * @param procSpell
         * \see ProcFlagsEx
         */
        void ProcDamageAndSpell(Unit* pVictim, uint32 procAttacker, uint32 procVictim, uint32 procEx, uint32 amount, WeaponAttackType attType = BASE_ATTACK, SpellEntry const* procSpell = NULL);
        /**
         * Same as for Unit::ProcDamageAndSpell
         * @param isVictim whether the target is considered the victim or not
         * @param pTarget
         * @param procFlag
         * @param procExtra
         * @param attType
         * @param procSpell
         * @param damage
         * \see ProcFlagsEx
         */
        void ProcDamageAndSpellFor(bool isVictim, Unit* pTarget, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, SpellEntry const* procSpell, uint32 damage);

        /**
         * Handles an emote, for example /charge would write something
         * along the lines: "NAME begins to charge" in orange text. This
         * method checks if it's a command or state, a command usually doesn't
         * show anything while a state would show something, ie stand
         * the character up.
         * @param emote_id id of the emote to handle
         * \see EmotesEntry
         * TODO: Is this accurate?
         */
        void HandleEmote(uint32 emote_id);                  // auto-select command/state
        /**
         * Sends a packet to the client SMSG_CLIENT with the emote_id given
         * which in turn probably makes the client show some sort of animation
         * for the given emote_id
         * @param emote_id id of the emote to show
         */
        void HandleEmoteCommand(uint32 emote_id);
        /**
         * Just updates the UNIT_NPC_EMOTESTATE field to the given emote_id.
         * @param emote_id the emote to show
         */
        void HandleEmoteState(uint32 emote_id);
        /**
         * Seems to do some damage to pVictim and also does extra attacks if the Unit
         * has any by recursively calling itself up to Unit::m_extraAttacks times with
         * the extra parameter set to true instead of the default false.
         *
         * Also calculates melee damage using Unit::CalculateMeleeDamage, deals damage and
         * such using Unit::DealDamageMods and also procs any spell that might be interesting
         * (TODO: Is that actually what ProcDamageAndSpell does?) using Unit::ProcDamageAndSpell
         *
         * @param pVictim the victim to hit
         * @param attType what hand (main/off) we were using
         * @param extra whether this was called recursively as an extra attack (true) or not (false)
         */
        void AttackerStateUpdate(Unit* pVictim, WeaponAttackType attType = BASE_ATTACK, bool extra = false);

        /**
         * Calculates the chance that a melee attack will miss the given victim.
         * The cap for miss chance is 0-60%, ie: you can't have a higher miss chance
         * than 60% and not lower than 0%.
         * @param pVictim the victim that will be attacked
         * @param attType type of attack
         * @return percentage between 0-60, ie: 57.0f = 57%
         */
        float MeleeMissChanceCalc(const Unit* pVictim, WeaponAttackType attType) const;

        /**
         * Fills the CalcDamageInfo structure with data about how much damage was done, in what way,
         * how much was absorbed etc. Also checks for different procs and inserts these flags into
         * the structure. Also calculates bonus damage with Unit::MeleeDamageBonusDone and the damage
         * with Unit::CalculateDamage
         * @param pVictim the victim that was hit with damage
         * @param damageInfo this is filled with data about what kind of damage that was done
         * @param attackType type of attack, base/off/ranged
         */
        void CalculateMeleeDamage(Unit* pVictim, CalcDamageInfo* damageInfo, WeaponAttackType attackType = BASE_ATTACK);
        /**
         * Deals melee damage, if the attack was parried we reduce the victims time until next hit
         * instead of the weapons normal time by 20 or 60%.
         * Also, if this is a NPC behind a (usually fleeing) player we have a chance to daze the
         * target. Will update the Judgement aura duration too, and check if the victim given from
         * CalcDamageInfo has any shields up and do damage to them in that case.
         * @param damageInfo used to deal the damage
         * @param durabilityLoss whether or not durability loss should happen
         */
        void DealMeleeDamage(CalcDamageInfo* damageInfo, bool durabilityLoss);

        bool IsAllowedDamageInArea(Unit* pVictim) const;

        /**
         * Calculates how much damage a spell should do, it will do some bonus damage according
         * to which SpellNonMeleeDamage::DmgClass it belongs to, ie: SPELL_DAMAGE_CLASS_RANGED
         * or SPELL_DAMAGE_CLASS_MELEE does bonus melee damage while the others make bonus spell
         * damage. Also reduces the damage done based on armor.
         * After returning this function will have filled the SpellNoneMeleeDamage::damage with
         * how much damage was actually done.
         * @param damageInfo info about attacker, target etc
         * @param damage how much damage to try to do
         * @param spellInfo info about the spell, needed by the helper functions
         * @param attackType what we were attacking with
         * \see Unit::IsSpellCrit
         * \see Unit::CalcArmorReducedDamage
         * \see SpellDmgClass
         */
        void CalculateSpellDamage(SpellNonMeleeDamage* damageInfo, int32 damage, SpellEntry const* spellInfo, WeaponAttackType attackType = BASE_ATTACK);
        /**
         * Deals actual damage based on info given. Does some checking if the spell actually exists
         * and updates the Judgement aura duration if it's there. Then it calls the DealDamage with
         * a SPELL_DIRECT_DAMAGE instead of DIRECT_DAMAGE to indicate that it was caused by a spell
         * (might be more than just that though)
         * @param damageInfo contains info about what kind of damage we will do etc
         * @param durabilityLoss whether or not durability loss should happen
         */
        void DealSpellDamage(SpellNonMeleeDamage* damageInfo, bool durabilityLoss);

        // WotLK-era crit-specific resilience helper. Cata 4.0.1 collapsed
        // crit-damage resilience into the single damage-reduction path; this
        // method is retained for any legacy callers but should not be invoked
        // from new code on a 4.3.4 server.
        uint32 GetCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_RESILIENCE_DAMAGE_TAKEN, 2.2f, 33.0f, damage); }
        // Cata 4.0.1: linear rate of 1.0 against the DBC-driven rating bonus
        // (gtCombatRatings.dbc, ~95.79 rating per 1% at L85). The WotLK-era
        // 2.0f multiplier double-counted the conversion table.
        uint32 GetDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_RESILIENCE_DAMAGE_TAKEN, 1.0f, 100.0f, damage); }

        float  MeleeSpellMissChance(Unit* pVictim, WeaponAttackType attType, int32 skillDiff, SpellEntry const* spell);
        /**
         * Tells what happened with the spell that was cast, some spells can't miss and they
         * have the attribute SPELL_ATTR_EX3_CANT_MISS. Also, in PvP you can't dodge or parry
         * when the attacker is behind you, but this is possible in PvE.
         *
         * Creatures with the flag CREATURE_FLAG_EXTRA_NO_PARRY can't parry an attack
         * @param pVictim the victim that was hit
         * @param spell the spell that was cast
         * @return Whether or not the spell hit/was resisted/blocked etc. A successfull cast would result in SPELL_MISS_NONE being returned
         * \see SpellEntry::HasAttribute for checking the SPELL_ATTR_EX3_CANT_MISS
         * \see Creature::GetCreatureInfo for the flags_extra
         */
        SpellMissInfo MeleeSpellHitResult(Unit* pVictim, SpellEntry const* spell);
        /**
         * This works pretty much like MeleeSpellHitResult but for magic spells instead.
         * For AOE spells there's a \ref Modifier called \ref AuraType::SPELL_AURA_MOD_AOE_AVOIDANCE
         * that reduces the spells hit chance.
         * @param pVictim the victim that was hit
         * @param spell the spell that was cast
         * @return Whether or not the spell was resisted/blocked etc. Seems the only 2
         * possible values are \ref SpellMissInfo::SPELL_MISS_RESIST or
         * \ref SpellMissInfo::SPELL_MISS_NONE
         * \todo Need use unit spell resistance in calculations (Old comment)
         */
        SpellMissInfo MagicSpellHitResult(Unit* pVictim, SpellEntry const* spell);
        /**
         * This combined \ref Unit::MagicSpellHitResult and \ref Unit::MeleeSpellHitResult and also
         * does checks for if the victim is immune or if it is in evade mode etc. If it's a positive
         * spell it can't miss either. Also takes care of reflects via PROC_EX_REFLECT and removes
         * possible charges that could have been present for reflecting spells. Lastly calls one
         * of the earlier mentioned functions depending on the SpellEntry::DmgClass.
         * Calculate spell hit result can be:
         * Every spell can: Evade/Immune/Reflect/Sucesful hit
         * For melee based spells:
         *   Miss
         *   Dodge
         *   Parry
         * For spells
         *   Resist
         * @param pVictim the victim that was hit
         * @param spell the spell that was cast
         * @param canReflect whether or not this spell can be reflected
         * @return Whether or not the spell was resisted/blocked etc.
         */
        SpellMissInfo SpellHitResult(Unit* pVictim, SpellEntry const* spell, bool canReflect = false);

        /**
         * Returns the units dodge chance
         * @return Units dodge chance in percent as value between 0.0f - 100.0f representing 0% - 100%
         */
        float GetUnitDodgeChance()    const;
        /**
         * Returns the units parry chance
         * @return Units parry chance in percent as value between 0.0f - 100.0f representing 0% - 100%
         */
        float GetUnitParryChance()    const;
        /**
         * Returns the units block chance
         * @return Units block chance in percent as value between 0.0f - 100.0f representing 0% - 100%
         */
        float GetUnitBlockChance()    const;
        /**
         * Returns the units critical hit chance against the given target as a value between
         * 0.0f - 100.0f representing 0% - 100%. Aura modifiers named
         * SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_CHANCE and SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_CHANCE
         * can increase the critical hit chance. Also the skill level in defense for the target
         * may increase it by formula: (Our max skill - target defense skill) * 0.04
         * @param attackType weapon we will attack with
         * @param pVictim the victim we want to calculate against
         * @return Units critical hit chance in percent as value between 0.0f - 100.0f representing 0% - 100%
         */
        float GetUnitCriticalChance(WeaponAttackType attackType, const Unit* pVictim) const;

        /**
         * Gets how much the shield would block via \ref Unit::m_auraBaseMod and \ref Unit::GetStat
         * for \ref STAT_STRENGTH. Seems to be implemented only in Player.cpp
         * @return Currently equipped shield block value
         */
        virtual uint32 GetShieldBlockDamageValue() const = 0;
        /**
         * Returns the proc chance for one weapon, if the \ref BASE_ATTACK is ready then the
         * proc chance for that is returned, otherwise if the \ref OFF_ATTACK is ready and
         * there's a weapon equipped there that chance will be returned, otherwise 0.
         *
         * The formula used for calculation is rather interesting:
         * Mainhand: GetAttackTime(BASE_ATTACK) * 1.8f / 1000.0f
         * Offhand: GetAttackTime(OFF_ATTACK) * 1.6f / 1000.0f
         * @return first the main weapons proc chance, then the off weapons proc chance.
         * \see GetAttackTime
         * \see isAttackReady
         * \todo Add code tags to the formulas
         */
        float GetWeaponProcChance() const;
        /**
         * This returns the proc per minute chance as a percentage.
         * Comment from cpp file:
         * result is chance in percents (probability = Speed_in_sec * (PPM / 60))
         * @param WeaponSpeed the weapon speed, usually gotten with \ref GetAttackTime
         * @param PPM the proc per minute rate
         * @return the chance for a proc in percent (taken from cpp file)
         * \todo What does this actually do? How/Where is it used?
         */
        float GetPPMProcChance(uint32 WeaponSpeed, float PPM) const;

        /**
         * This acts as a wrapper for \ref Unit::RollMeleeOutcomeAgainst with more parameters,
         * these are initialised from the pVictim using
         *  - \ref Unit::MeleeMissChanceCalc
         *  - \ref Unit::GetUnitCriticalChance
         *  - \ref Unit::GetUnitDodgeChance
         *  - \ref Unit::GetUnitBlockChance
         *  - \ref Unit::GetUnitParryChance
         * @param pVictim the victim to target
         * @param attType with what "hand" you want to attack
         * @return what the hit resulted in, miss/hit etc.
         */
        MeleeHitOutcome RollMeleeOutcomeAgainst(const Unit* pVictim, WeaponAttackType attType) const;
        /**
         * Calculates what off a few possible things that can happen when a victim is attacked
         * with melee weapons. For a list of the things that could happen see \ref MeleeHitOutcome.
         * There's a few formulas involved here, for more info on them check the cpp file. But as
         * usual, if you're behind your victim they can't parry/block and players can't dodge while
         * mobs can.
         * @param pVictim the victim of the attack
         * @param attType the had to attack with
         * @param crit_chance crit chance against victim
         * @param miss_chance miss chance against victim
         * @param dodge_chance victims dodge chance
         * @param parry_chance victims parry chance
         * @param block_chance victims block chance
         * @return what the hit resulted in, miss/hit etc
         */
        MeleeHitOutcome RollMeleeOutcomeAgainst(const Unit* pVictim, WeaponAttackType attType, int32 crit_chance, int32 miss_chance, int32 dodge_chance, int32 parry_chance, int32 block_chance) const;

        /**
         * @return true if this unit is a vendor, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsVendor()       const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR); }
        /**
         * @return true if this unit is a trainer, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsTrainer()      const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_TRAINER); }
        /**
         * @return true if this unit is a QuestGiver, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsQuestGiver()   const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER); }
        /**
         * @return true if this unit is a gossip, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsGossip()       const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP); }
        /**
         * @return true if this unit is a taxi, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsTaxi()         const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_FLIGHTMASTER); }
        /**
         * @return true if this unit is a GuildMaster, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsGuildMaster()  const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_PETITIONER); }
        /**
         * @return true if this unit is a BattleMaster, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsBattleMaster() const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_BATTLEMASTER); }
        /**
         * @return true if this unit is a banker, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsBanker()       const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_BANKER); }
        /**
         * @return true if this unit is a innkeeper, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsInnkeeper()    const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_INNKEEPER); }
        /**
         * @return true if this unit is a SpiritHealer, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsSpiritHealer() const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER); }
        /**
         * @return true if this unit is a SpiritGuide, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsSpiritGuide()  const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITGUIDE); }
        /**
         * @return true if this unit is a TabardDesigner, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsTabardDesigner()const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_TABARDDESIGNER); }
        /**
         * @return true if this unit is a Auctioneer, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool isAuctioner()    const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_AUCTIONEER); }
        /**
         * @return true if this unit is a armorer, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsArmorer()      const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR); }
        /**
         * Returns if this is a service provider or not, a service provider has one of the
         * following flags:
         * - \ref UNIT_NPC_FLAG_VENDOR
         * - \ref UNIT_NPC_FLAG_TRAINER
         * - \ref UNIT_NPC_FLAG_FLIGHTMASTER
         * - \ref UNIT_NPC_FLAG_PETITIONER
         * - \ref UNIT_NPC_FLAG_BATTLEMASTER
         * - \ref UNIT_NPC_FLAG_BANKER
         * - \ref UNIT_NPC_FLAG_INNKEEPER
         * - \ref UNIT_NPC_FLAG_SPIRITHEALER
         * - \ref UNIT_NPC_FLAG_SPIRITGUIDE
         * - \ref UNIT_NPC_FLAG_TABARDDESIGNER
         * - \ref UNIT_NPC_FLAG_AUCTIONEER
         *
         * @return true if this unit is a ServiceProvider, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsServiceProvider() const
        {
            return HasFlag(UNIT_NPC_FLAGS,
                           UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_FLIGHTMASTER |
                           UNIT_NPC_FLAG_PETITIONER | UNIT_NPC_FLAG_BATTLEMASTER | UNIT_NPC_FLAG_BANKER |
                           UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_SPIRITHEALER |
                           UNIT_NPC_FLAG_SPIRITGUIDE | UNIT_NPC_FLAG_TABARDDESIGNER | UNIT_NPC_FLAG_AUCTIONEER);
        }
        /**
         * Returns if this is a spirit service or not, a spirit service has one of the
         * following flags:
         * - \ref UNIT_NPC_FLAG_SPIRITHEALER
         * - \ref UNIT_NPC_FLAG_SPIRITGUIDE
         * @return true if this unit is a spirit service, false otherwise
         * \see Object::HasFlag
         * \see EUnitFields
         * \see NPCFlags
         */
        bool IsSpiritService() const { return HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER | UNIT_NPC_FLAG_SPIRITGUIDE); }

        /**
         * Is this unit flying in taxi?
         * @return true if the Unit has the state \ref UNIT_STAT_TAXI_FLIGHT (is flying in taxi), false otherwise
         * \see hasUnitState
         */
        bool IsTaxiFlying()  const { return hasUnitState(UNIT_STAT_TAXI_FLIGHT); }

        /**
         * Checks to see if a creature, whilst moving along a path, has reached a specific waypoint, or near to
         * @param currentPositionX is the creature's current X ordinate in the game world
         * @param currentPositionY is the creature's current Y ordinate in the game world
         * @param currentPositionZ is the creature's current Z ordinate in the game world
         * @param destinationPositionX is the in game ordinate that we wish to check against the creature's current X ordinate (are they the same, or very close?)
         * @param destinationPositionY is the in game ordinate that we wish to check against the creature's current Y ordinate (are they the same, or very close?)
         * @param destinationPositionZ is the in game ordinate that we wish to check against the creature's current Z ordinate (are they the same, or very close?)
         * @param distanceX is the distance from the creature's current X ordinate to the destination X ordinate
         * @param distanceY is the distance from the creature's current Y ordinate to the destination Y ordinate
         * @param distanceZ is the distance from the creature's current Z ordinate to the destination Z ordinate
         *
         */
        bool IsNearWaypoint(float currentPositionX, float currentPositionY, float currentPositionZ, float destinationPositionX, float destinationPositionY, float destinationPositionZ, float distanceX, float distanceY, float distanceZ);


        /**
         * Is this unit in combat?
         * @return true if the Unit has the flag \ref UNIT_FLAG_IN_COMBAT (is in combat), false otherwise
         * \see EUnitFields
         * \see UnitFlags
         */
        bool IsInCombat()  const { return HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT); }
        /**
         * Sets this \ref Unit into combat, if it already was this has no bigger meaning if the
         * PvP flag hasn't changed since last time it was applied.
         * @param PvP whether this was a PvP combat or not, this is important for how quick the combat flag will wear away and possibly more
         * @param enemy Our target that we are attacking, only does something if the attacked one is a creature it seems
         * \see ClearInCombat
         * \see SetInCombatWith
         */
        void SetInCombatState(bool PvP, Unit* enemy = NULL);
        /**
         * Sets us in combat with the given enemy, this in turn just does a few small checks for if
         * it's a duel or PvP and then calls \ref Unit::SetInCombatState with the correct value for
         * PvP and enemy
         * @param enemy the enemy that we are attacking/engaging
         * \see ClearInCombat
         * \see SetInCombatState
         */
        void SetInCombatWith(Unit* enemy);
        /**
         * Clears the combat flag for this unit using \ref Object::RemoveFlag and clears
         * the Unit state \ref UnitState::UNIT_STAT_ATTACK_PLAYER. This is not where a
         * \ref Player s PvP flags are cleared, that is handled in \ref Player::UpdateContestedPvP
         * \see EUnitFields
         */
        void ClearInCombat();
        /**
         * Probably returns how long it is until this Unit should get out of PvP combat again,
         * although not used in that sense.
         * @return the time we have left from our start of PvP combat
         * \todo Find out what this actually does/means
         */
        uint32 GetCombatTimer() const { return m_CombatTimer; }

        /**
         * Gets all \ref SpellAuraHolder s that have the same given spell_id
         * @param spell_id the spell_id to search for
         * @return 2 iterators to the range of \ref SpellAuraHolder s that were found
         */
        SpellAuraHolderBounds GetSpellAuraHolderBounds(uint32 spell_id)
        {
            return m_spellAuraHolders.equal_range(spell_id);
        }
        /**
         * Same as \ref Unit::GetSpellAuraHolderBounds
         */
        SpellAuraHolderConstBounds GetSpellAuraHolderBounds(uint32 spell_id) const
        {
            return m_spellAuraHolders.equal_range(spell_id);
        }

        /**
         * Checks if this \ref Unit has the given AuraType
         * @param auraType the type of aura to look for
         * @return true if this \ref Unit is affected by the given \ref AuraType, false otherwise
         */
        bool HasAuraType(AuraType auraType) const;
        /**
         * Checks if the given \ref SpellEntry affects the \ref AuraType in some way, this is done
         * by calling \ref Aura::isAffectedOnSpell which in turn seems to check if the spellProto
         * and the \ref Aura s own \ref SpellEntry have the same \ref SpellEntry::SpellFamilyName.
         * (The \ref Aura get's it's \ref SpellEntry by calling \ref Aura::GetSpellProto)
         * @param auraType the type of aura we want to check
         * @param spellProto the spell we want to check for affecting the aura type
         * @return true if the spell affects the given aura type, false otherwise
         * \todo Is this actually correct, also, make it more clear what it actually checks
         */
        bool HasAffectedAura(AuraType auraType, SpellEntry const* spellProto) const;
        /**
         * Checks if we have at least one \ref Aura that is associated with the given spell id
         * and \ref SpellEffectIndex.
         * @param spellId the spell id to look for
         * @param effIndex the effect index the \ref Aura should have
         * @return true if there was at least one \ref Aura associated with the id that belonged to
         * the correct \ref SpellEffectIndex, false otherwise
         */
        bool HasAura(uint32 spellId, SpellEffectIndex effIndex) const;
        /**
         * Checks if we have at least one \ref Aura that is associated with the given spell id via
         * the \ref Unit::m_spellAuraHolders multimap. Generalized version of the other
         * \ref Unit::HasAura
         * @param spellId the spell id to look for
         * @return true if there was at least one \ref Aura associated with the id, false otherwise
         */
        bool HasAura(uint32 spellId) const
        {
            return m_spellAuraHolders.find(spellId) != m_spellAuraHolders.end();
        }
        bool HasAuraOfDifficulty(uint32 spellId) const;

        /**
         * This is overridden in \ref Player::HasSpell, \ref Creature::HasSpell and \ref Pet::HasSpell
         * @return false in this implementation
         */
        virtual bool HasSpell(uint32 /*spellID*/) const { return false; }

        /**
         * Check is this \ref Unit has a stealth modified applied
         * @return true if this \ref Unit has the \ref AuraType \ref AuraType::SPELL_AURA_MOD_STEALTH
         * applied, false otherwise
         * \see Modifier
         * \see Unit::HasAuraType
         * \see AuraType
         */
        bool HasStealthAura()      const { return HasAuraType(SPELL_AURA_MOD_STEALTH); }
        /**
         * Check if this \ref Unit has a invisibility \ref Aura modifier applied.
         * @return true if this \ref Unit has the \ref AuraType
         * \ref AuraType::SPELL_AURA_MOD_INVISIBILITY applied, false otherwise
         * \see Modifier
         * \see Unit::HasAuraType
         * \see AuraType
         */
        bool HasInvisibilityAura() const { return HasAuraType(SPELL_AURA_MOD_INVISIBILITY); }
        /**
         * Check if this \ref Unit has a fear \ref Aura modifier applied. Ie, is it feared?
         * @return true if this \ref Unit has the \ref AuraType \ref AuraType::SPELL_AURA_MOD_FEAR
         * applied, false otherwise
         * \see Modifier
         * \see Unit::HasAuraType
         * \see AuraType
         */
        bool isFeared()  const { return HasAuraType(SPELL_AURA_MOD_FEAR); }
        /**
         * Check if this \ref Unit has a rooting \ref Aura modifier applied. Ie, is it stuck in
         * some way?
         * @return true if this \ref Unit has the \ref AuraType \ref AuraType::SPELL_AURA_MOD_ROOT
         * applied, false otherwise
         * \see Modifier
         * \see Unit::HasAuraType
         * \see AuraType
         */
        bool IsInRoots() const { return HasAuraType(SPELL_AURA_MOD_ROOT); }
        /**
         * Is this \ref Unit polymorphed?
         * @return true if this \ref Unit is polymorphed, false otherwise
         * \see GetSpellSpecific
         * \see Unit::GetTransform()
         * \todo Move the implementation to .h file exactly as the earlier ones?
         */
        bool IsPolymorphed() const;

        /**
         * Check if this \ref Unit has  freezing \ref Aura modifier applied. Ie, is it
         * frozen in ground?
         * @return true if this \ref Unit has the \ref AuraType \ref AuraType::AURA_STATE_FROZEN,
         * false otherwise
         * \see Modifier
         * \see Unit::HasAuraType
         * \see AuraType
         * \todo Move the implementation to .h file exactly as the earlier ones?
         */
        bool IsFrozen() const;
        bool IsIgnoreUnitState(SpellEntry const* spell, IgnoreUnitState ignoreState);

        /**
         * Checks if this \ref Unit could be targeted with an attack, things that make that
         * impossible are:
         * - The \ref Unit / \ref Player is a GM
         * - The \ref Unit has the flags (\ref Object::HasFlag)
         * \ref UnitFlags::UNIT_FLAG_NON_ATTACKABLE
         * and \ref UnitFlags::UNIT_FLAG_NOT_SELECTABLE
         * - The \ref Unit has the flag (\ref Object::HasFlag)
         * \ref UnitFlags::UNIT_FLAG_OOC_NOT_ATTACKABLE, this seems to vary some though, since this
         * flag will be removed when the creature for some reason enters combat
         * - \ref Unit::IsAlive is equal to inverseAlive
         * - \ref Unit::IsInWorld is false
         * - the \ref Unit has the state (\ref Unit::hasUnitState) \ref  UnitState::UNIT_STAT_DIED
         * - the \ref Unit is flying in a taxi (\ref Unit::IsTaxiFlying)
         * @param inverseAlive This is needed for some spells which need
         * to be casted at dead targets (aoe) (Taken from source comment)
         * @return true if the target can be attacked, false otherwise
         * \see UnitState
         */
        bool IsTargetableForAttack(bool inverseAlive = false) const;
        /**
         * Simply checks if this \ref Unit has the flag (\ref Unit::HasFlag)
         * \ref UnitFlags::UNIT_FLAG_PASSIVE in \ref EUnitFields::UNIT_FIELD_FLAGS
         * @return true if the target is passive to hostile actions, false otherwise
         */
        bool isPassiveToHostile() const { return HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PASSIVE); }

        /**
         * Is this \ref Unit in water?
         * @return true if the \ref Unit is in water, false otherwise
         * \see Object::GetTerrain
         * \see TerrainInfo::IsInWater
         */
        virtual bool IsInWater() const;
        /**
         * Is this \ref Unit under water?
         * @return true if the \ref Unit is under water, false otherwise
         * \see Object::GetTerrain
         * \see TerrainInfo::IsUnderWater
         */
        virtual bool IsUnderWater() const;
        /**
         * Can the given \ref Creature access this \ref Unit in some way? If this \ref Unit is in
         * water we check if the \ref Creature can swim, if so it's accessible, otherwise it's not.
         * If we're not in water the \ref Creature should be able to walk or fly and then we're
         * accessible.
         * @param c The \ref Creature to check accessibility for
         * @return true if this \ref Unit is accessible to the \ref Creature given, false otherwise
         * \todo Rename to IsInAccessablePlaceFor to follow standards?
         */
        bool isInAccessablePlaceFor(Creature const* c) const;

        /**
         * Sends a packet to the client with \ref OpcodesList::SMSG_SPELLHEALLOG which presumably
         * updates the combat log of this \ref Unit and shows some healing done and to who in it.
         *
         * For a description of the packets look see \ref OpcodesList::SMSG_SPELLHEALLOG
         * @param pVictim the victim of the healing spell
         * @param SpellID what spell id that was used
         * @param Damage how much "damage" we did (healing in this case)
         * @param critical whether it was a critical hit or not.
         * \see WorldPacket
         */
        void SendHealSpellLog(Unit* pVictim, uint32 SpellID, uint32 Damage, uint32 OverHeal, bool critical = false, uint32 absorb = 0);
        /**
         * Sends a packet to the client with \ref OpcodesList::SMSG_SPELLENERGIZELOG which presumably
         * updates the combat log of this \ref Unit and shows some enery regen and to who it was.
         *
         * For a description of the packets look see \ref OpcodesList::SMSG_SPELLENERGIZELOG
         * @param pVictim the victim of the regen
         * @param SpellID the spell id that caused it
         * @param Damage how much was regenerated
         * @param powertype the power that was regenerated
         */
        void SendEnergizeSpellLog(Unit* pVictim, uint32 SpellID, uint32 Damage, Powers powertype);
        /**
         * Regenerates/degenerates the given amount of "damage" for the given power and sends a
         * update to the combat log.
         * @param pVictim the victim to add/remove power from
         * @param SpellID the spell id that caused it
         * @param Damage how much was increased/decreased, negative values decrease, positive increase
         * @param powertype the power that was increased/decreased
         * \see Unit::ModifyPower
         */
        void EnergizeBySpell(Unit* pVictim, uint32 SpellID, uint32 Damage, Powers powertype);
        /**
         * Will do damage to the victim calculating how much should be done with the help of
         * \ref SpellNonMeleeDamage, \ref Unit::CalculateSpellDamage, \ref Unit::DealDamageMods and
         * \ref Unit::SendSpellNonMeleeDamageLog to update the combat log
         * @param pVictim the victim that should take damage
         * @param spellID id of the spell that will cause the damage
         * @param damage the initial damage to do
         * @return how much damage was actually done
         */
        uint32 SpellNonMeleeDamageLog(Unit* pVictim, uint32 spellID, uint32 damage);
        /**
         * This function only checks if the spell id is accurate, if it is then the other
         * \ref Unit::CastSpell is called which does the actual cast.
         * @param Victim victim that should be hit by the spell
         * @param spellId id of the spell to cast
         * @param triggered whether this was triggered by some outside circumstance or used as a button
         * press on you action bar, true means triggered by outside circumstance
         * @param castItem the item that cast the spell if any, usually NULL
         * @param triggeredByAura the \ref Aura that triggered the spell if any
         * @param originalCaster usually just \ref ObjectGuid constructor
         * @param triggeredBy the \ref SpellEntry that triggered this spell if any
         * \see Spell
         * \see SpellCastTargets
         * \todo What's the original caster?
         */
        void CastSpell(Unit* Victim, uint32 spellId, bool triggered, Item* castItem = NULL, Aura* triggeredByAura = NULL, ObjectGuid originalCaster = ObjectGuid(), SpellEntry const* triggeredBy = NULL);
        /**
         * Casts a spell simple and square, outputs some debugging info for some reasons, ie: if the
         * spellInfo is NULL it is logged and the function won't do anything. If the spell is triggered
         * by an \ref Aura and there's no originalCaster it is updated to be the cast of the \ref Aura
         * and the triggeredBy is set to be the \ref Aura s \ref SpellEntry. Some work is done to
         * work out if we should set a destination and/or source for the spell and it is then cast.
         *
         * Also, linked spells seem to be taken care of in here, but only the ones that should be
         * removed on cast, ie: \ref SpellLinkedType::SPELL_LINKED_TYPE_REMOVEONCAST. See
         * \ref SpellLinkedEntry for more info and \ref SpellLinkedType
         *
         * Finally calls \ref Spell::prepare on the \ref Spell and that's where it continues the
         * execution.
         *
         * @param Victim victim that should be hit by the spell
         * @param spellInfo info about the spell to cast
         * @param triggered whether this was triggered by some outside circumstance or used as a button
         * press on your action bar, true means triggered by outside circumstance
         * @param castItem the \ref Item that cast the spell if any, usually NULL
         * @param triggeredByAura the \ref Aura that triggered the spell if any
         * @param originalCaster usually just \ref ObjectGuid constructor
         * @param triggeredBy the \ref SpellEntry that triggered this spell if any
         * \see Spell
         * \see SpellCastTargets
         * \todo What's the original caster?
         * \todo Document the spell linked
         */
        void CastSpell(Unit* Victim, SpellEntry const* spellInfo, bool triggered, Item* castItem = NULL, Aura* triggeredByAura = NULL, ObjectGuid originalCaster = ObjectGuid(), SpellEntry const* triggeredBy = NULL);
        /**
         * Does pretty much the same thing as \ref Unit::CastSpell but uses the three bp0-bp2 variables
         * to change the \ref Spell s \ref Spell::m_currentBasePoints for the different
         * \ref SpellEffectIndexes. This also works as the first version of \ref Unit::CastSpell which
         * just does some checks and then calls the other one.
         *
         * @param Victim victim that should be hit by the spell
         * @param spellId id of the spell to be cast
         * @param bp0 this will change the \ref Spell s member \ref Spell::m_currentBasePoints for
         * \ref SpellEffectIndex::EFFECT_INDEX_0 to this value if it's not NULL.
         * @param bp1 this will change the \ref Spell s member \ref Spell::m_currentBasePoints for
         * \ref SpellEffectIndex::EFFECT_INDEX_1 to this value if it's not NULL.
         * @param bp2 this will change the \ref Spell s member \ref Spell::m_currentBasePoints for
         * \ref SpellEffectIndex::EFFECT_INDEX_2 to this value if it's not NULL.
         * @param triggered whether this was triggered by some outside circumstance or used as a button
         * press on your action bar, true means triggered by outside circumstance
         * @param castItem the \ref Item that cast this if any
         * @param triggeredByAura the \ref Aura that triggered this
         * @param originalCaster the original caster if any
         * @param triggeredBy the \ref SpellEntry that triggered this cast, if any
         * \todo What's the original caster?
         */
        void CastCustomSpell(Unit* Victim, uint32 spellId, int32 const* bp0, int32 const* bp1, int32 const* bp2, bool triggered, Item* castItem = NULL, Aura* triggeredByAura = NULL, ObjectGuid originalCaster = ObjectGuid(), SpellEntry const* triggeredBy = NULL);
        /**
         * Same idea for this one as for \ref Unit::CastCustomSpell with a change to the spellid being
         * exchanged for a \ref SpellEntry instead
         *
         * @param Victim victim that should be hit by the spell
         * @param spellInfo info about the spell to cast
         * @param bp0 this will change the \ref Spell s member \ref Spell::m_currentBasePoints for
         * \ref SpellEffectIndex::EFFECT_INDEX_0 to this value if it's not NULL.
         * @param bp1 this will change the \ref Spell s member \ref Spell::m_currentBasePoints for
         * \ref SpellEffectIndex::EFFECT_INDEX_1 to this value if it's not NULL.
         * @param bp2 this will change the \ref Spell s member \ref Spell::m_currentBasePoints for
         * \ref SpellEffectIndex::EFFECT_INDEX_2 to this value if it's not NULL.
         * @param triggered whether this was triggered by some outside circumstance or used as a button
         * press on your action bar, true means triggered by outside circumstance
         * @param castItem the \ref Item that cast this if any
         * @param triggeredByAura the \ref Aura that triggered this
         * @param originalCaster the original caster if any
         * @param triggeredBy the \ref SpellEntry that triggered this cast, if any
         * \todo What's the original caster?
         */
        void CastCustomSpell(Unit* Victim, SpellEntry const* spellInfo, int32 const* bp0, int32 const* bp1, int32 const* bp2, bool triggered, Item* castItem = NULL, Aura* triggeredByAura = NULL, ObjectGuid originalCaster = ObjectGuid(), SpellEntry const* triggeredBy = NULL);
        /**
         * Same idea as for \ref Unit::CastSpell, but with the parameters x, y, z telling the
         * destination and source of the \ref SpellCastTargets depending on if the
         * \ref SpellEntry::Targets bitflags have the
         * \ref SpellCastTargetFlags::TARGET_FLAG_DEST_LOCATION  set for destination and
         * \ref SpellCastTargetFlags::TARGET_FLAG_SOURCE_LOCATION set for location
         *
         * @param x coord for source/dest
         * @param y coord for source/dest
         * @param z coord for source/dest
         * @param spellId id of the spell that was cast
         * @param triggered whether this was triggered by some outside circumstance or used as a button
         * press on your action bar, true means triggered by outside circumstance
         * @param castItem the \ref Item that cast this if any
         * @param triggeredByAura the \ref Aura that triggered this
         * @param originalCaster the original caster if any
         * @param triggeredBy the \ref SpellEntry that triggered this cast, if any
         */
        void CastSpell(float x, float y, float z, uint32 spellId, bool triggered, Item* castItem = NULL, Aura* triggeredByAura = NULL, ObjectGuid originalCaster = ObjectGuid(), SpellEntry const* triggeredBy = NULL);
        /**
         * Same idea as for \ref Unit::CastSpell, but with the parameters x, y, z telling the
         * destination and source of the \ref SpellCastTargets depending on if the
         * \ref SpellEntry::Targets bitflags have the
         * \ref SpellCastTargetFlags::TARGET_FLAG_DEST_LOCATION  set for destination and
         * \ref SpellCastTargetFlags::TARGET_FLAG_SOURCE_LOCATION set for location
         *
         * @param x coord for source/dest
         * @param y coord for source/dest
         * @param z coord for source/dest
         * @param spellInfo info about the spell to cast
         * @param triggered whether this was triggered by some outside circumstance or used as a button
         * press on your action bar, true means triggered by outside circumstance
         * @param castItem the \ref Item that cast this if any
         * @param triggeredByAura the \ref Aura that triggered this
         * @param originalCaster the original caster if any
         * @param triggeredBy the \ref SpellEntry that triggered this cast, if any
         */
        void CastSpell(float x, float y, float z, SpellEntry const* spellInfo, bool triggered, Item* castItem = NULL, Aura* triggeredByAura = NULL, ObjectGuid originalCaster = ObjectGuid(), SpellEntry const* triggeredBy = NULL);

        /**
         * Changes the display id for this \ref Unit to the native one that it usually has.
         * This is done by calling \ref Unit::SetDisplayId and \ref Unit::GetNativeDisplayId
         * like so:
         * \code{.cpp}
         * SetDisplayId(GetNativeDisplayId());
         * \endcode
         */
        void DeMorph();

        /**
         * This sends an AttackStateUpdate, some info about damage that you've done etc.
         * @param damageInfo the damage info used for knowing what to send
         * \see OpcodesList::SMSG_ATTACKERSTATEUPDATE
         * \todo Find out when and why this is sent
         */
        void SendAttackStateUpdate(CalcDamageInfo* damageInfo);
        /**
         * The same thing as \ref Unit::SendAttackStateUpdate but you send along all the parameters
         * that are needed instead of giving them through \ref CalcDamageInfo
         * @param HitInfo hit information as in the \ref CalcDamageInfo::HitInfo
         * @param target the target of the attack
         * @param SwingType the swingtype, need to know what this is
         * @param damageSchoolMask the damageschoolmask as the one from:
         * \ref CalcDamageInfo::damageSchoolMask
         * @param Damage the damage that was done
         * @param AbsorbDamage how much of the damage that was absorbed
         * @param Resist how much of the damage that was resisted
         * @param TargetState the \ref VictimState of the target
         * @param BlockedAmount how much of the damage that was blocked
         * \todo What's the swingtype for?
         */
        void SendAttackStateUpdate(uint32 HitInfo, Unit* target, uint8 SwingType, SpellSchoolMask damageSchoolMask, uint32 Damage, uint32 AbsorbDamage, uint32 Resist, VictimState TargetState, uint32 BlockedAmount);
        void SendAttackStateUpdate(uint32 HitInfo, Unit* target, SpellSchoolMask damageSchoolMask, uint32 Damage, uint32 AbsorbDamage, uint32 Resist, VictimState TargetState, uint32 BlockedAmount);
        /**
         * Used to send a update to the combat log for all \ref Player/\ref Unit s in the vicinity.
         * @param log Info about who/what did damage to who and how etc, data needed for the packet
         * \see OpcodesList::SMSG_SPELLNONMELEEDAMAGELOG
         * \todo Is this actually for the combat log?
         */
        void SendSpellNonMeleeDamageLog(SpellNonMeleeDamage* log);
        /**
         * Same idea as for \ref Unit::SendSpellNonMeleeDamageLog but without the helping
         * \ref SpellNonMeleeDamage. This will set the \ref SpellNonMeleeDamage::HitInfo member to
         * the following before sending the packet:
         * \code {.cpp}
         * \ref HitInfo::SPELL_HIT_TYPE_UNK1 | \ref HitInfo::SPELL_HIT_TYPE_UNK3 | \ref HitInfo::SPELL_HIT_TYPE_UNK6
         * \endcode
         *
         * And if the \a CriticalHit parameter is true then it will add the flag
         * \ref HitInfo::SPELL_HIT_TYPE_CRIT
         *
         * @param target the target of the spell
         * @param SpellID id of the spell that was used
         * @param Damage the damage done including the damage that was resisted/absorbed/blocked etc.
         * Ie: damage + absorbed + resisted. This will be subtracted to the bare damage when inserted
         * into the \ref SpellNonMeleeDamage struct
         * @param damageSchoolMask mask for which kind of damage this is, see \ref SpellSchools for
         * possible values, the first set bit out of this mask will be used a the \ref SpellSchools
         * @param AbsorbedDamage how much of the damage that was absorbed
         * @param Resist how much of the damage that was resisted
         * @param PhysicalDamage whether or not this was physical damage
         * @param Blocked how much of the damage that was blocked
         * @param CriticalHit whether it was a critical hit or not
         * \see HitInfo
         * \see OpcodesList::SMSG_SPELLNONMELEEDAMAGELOG
         * \todo Is this actually for the combat log?
         */
        void SendSpellNonMeleeDamageLog(Unit* target, uint32 SpellID, uint32 Damage, SpellSchoolMask damageSchoolMask, uint32 AbsorbedDamage, uint32 Resist, bool PhysicalDamage, uint32 Blocked, bool CriticalHit = false);
        /**
         * Sends some data to the combat log about the periodic effects of an \ref Aura, it might be
         * periodic healing/damage etc. Perhaps it increases the amount of power you have as a rogue
         * and such. For more info on what exactly is sent etc see
         * \ref OpcodesList::SMSG_PERIODICAURALOG.
         * @param pInfo Info about the periodic effect of the \ref Aura
         * \see OpcodesList::SMSG_PERIODICAURALOG
         * \todo Is this actually for the combat log?
         */
        void SendPeriodicAuraLog(SpellPeriodicAuraLogInfo* pInfo);
        /**
         * Sends some data to the combat log about a spell that missed someone else. For more info
         * on what's sent see \ref OpcodesList::SMSG_SPELLLOGMISS
         * @param target the target of the \ref Spell that missed
         * @param spellID id of the spell that missed
         * @param missInfo info about how the spell actually missed ie: resisted/blocked/reflected etc.
         */
        void SendSpellMiss(Unit* target, uint32 spellID, SpellMissInfo missInfo);

        /**
         * Teleports a \ref Creature or \ref Player to some coordinates within the same \ref Map,
         * hence the name. If it's a \ref Creature that's being teleported it needs to have it's
         * \ref MovementGenerator interrupted before the teleport and then reset afterwards. See
         * \ref MovementGenerator::Reset and \ref MovementGenerator::Interrupt. Also, after moving
         * a \ref Creature a hearbeat needs to be sent to inform the clients about the new location,
         * this is done using \ref Unit::SendHeartBeat and the actual move of the \ref Creature is
         * done with \ref Map::CreatureRelocation
         * @param x the new x coord
         * @param y the new y coord
         * @param z the new z coord
         * @param orientation the orientation after teleport, 0 is north and it's measured in radians
         * @param casting used to decide whether a spell cast should be interrupted if a \ref Player
         * is going to be teleported, see \ref TeleportToOptions::TELE_TO_SPELL, the idea is that
         * you don't interrupt spellcasting at teleport if the spell is meant to teleport you as
         * that would defeat the purpose of trying to teleport one self :)
         */
        void NearTeleportTo(float x, float y, float z, float orientation, bool casting = false);
        /**
         * Moves this \ref Unit to the given position in x,y,z coordinates. we can choose whether or
         * not we want to generate a path to the target or just move straight there and if we would
         * like to force the destination when creating the \ref PathFinder, only interesting
         * if we have set generatePath to true.
         *
         * Taken from comments: recommend use \ref Unit::MonsterMove / \ref Unit::MonsterMoveWithSpeed
         * for most case that correctly work with movegens, mmaps
         * @param x the x coord to move to
         * @param y the y coord to move to
         * @param z the z coord to move to
         * @param speed at which speed the \ref Unit should move
         * @param generatePath whether a real path should be generated using a \ref PathFinder which
         * will try to create a smooth path. if false we just go from current pos to given pos
         * @param forceDestination if this is true it seems that we will try to get to our target
         * even if there's something in the way. Otherwise we stop before we get there if there
         * are obstacles
         * \todo In what is the speed expressed? What is normal walking/running speed?
         * \todo Is the dox about forceDestination correct?
         */
        void MonsterMoveWithSpeed(float x, float y, float z, float speed, bool generatePath = false, bool forceDestination = false);
        // recommend use MonsterMove/MonsterMoveWithSpeed for most case that correctly work with movegens
        // if used additional args in ... part then floats must explicitly casted to double
        /**
         * Tells nearby \ref Unit s and such that this \ref Unit has moved to a new position using
         * \ref OpcodesList::SMSG_PLAYER_MOVE which will send the new position to all clients etc
         * in the same \ref Cell
         */
        void SendHeartBeat();

        /**
         * Checks if this \ref Unit has the movement flag \ref MovementFlags::MOVEFLAG_LEVITATING
         * @return true if the \ref Unit is levitating, ie: it has the flag MOVEFLAG_LEVITATING, false
         * otherwise
         * \see MovementInfo::HasMovementFlag
         */
        bool IsLevitating() const { return m_movementInfo.HasMovementFlag(MOVEFLAG_LEVITATING); }
        /**
         * Checks if this \ref Unit has the movement flag \ref MovementFlags::MOVEFLAG_WALK_MODE
         * @return true if the \ref Unit is walking, ie: it has the flag MOVEFLAG_WALK_MODE, false
         * otherwise
         * \see MovementInfo::HasMovementFlag
         */
        bool IsWalking() const { return m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE); }
        /**
         * Check if this \ref Unit has the movement flag \ref MovementFlags::MOVEFLAG_ROOT
         * @return true if the \ref Unit is rooted to the ground (can't move), ie: has the flag
         * MOVEFLAG_ROOT, false otherwise
         * \see MovementInfo::HasMovementFlag
         */
        bool IsRooted() const { return m_movementInfo.HasMovementFlag(MOVEFLAG_ROOT); }

        virtual void SetLevitate(bool /*enabled*/) {}
        virtual void SetSwim(bool /*enabled*/) {}
        virtual void SetCanFly(bool /*enabled*/) {}
        virtual void SetFeatherFall(bool /*enabled*/) {}
        virtual void SetHover(bool /*enabled*/) {}
        /**
         * Roots or unroots this \ref Unit depending on the enabled parameter.
         * @param enabled whether we should root (true) or unroot (false) this \ref Unit
         * \see Player::SetRoot
         */
        virtual void SetRoot(bool /*enabled*/) {}
        /**
         * Changes this \ref Unit s ability to walk on water.
         * @param enabled whether this \ref Unit should be able to walk on water (true) or not
         * be able to (false)
         * \see Player::SetWaterWalk
         */
        virtual void SetWaterWalk(bool /*enabled*/) {}

        /**
         * Turns this \ref Unit towards the given one.
         * @param target the \ref Unit we want to be turned towards
         * \see WorldObject::SetOrientation
         * \see WorldObejct::GetAngle
         */
        void SetInFront(Unit const* target);
        /**
         * Sets this \ref Unit to face a certain angle.
         * @param ori where we should start facing, measured in radians, 0 = north pi/2 = east etc.
         * \todo is pi/2 = east or west? Logic says east?
         */
        void SetFacingTo(float ori);
        /**
         * Does pretty much the same thing as \ref Unit::SetInFront but calls \ref Unit::SetFacingTo
         * instead, which uses a \ref MoveSplineInit instead of just changing the angle.
         * @param pObject the \ref WorldObject we should be facing
         * \todo What difference does it make to use \ref MoveSplineInit instead of just directly
         * changing the angle? Is it smoother?
         */
        void SetFacingToObject(WorldObject* pObject);

        void SendHighestThreatUpdate(HostileReference* pHostileReference);
        void SendThreatClear();
        void SendThreatRemove(HostileReference* pHostileReference);
        void SendThreatUpdate();

        /**
         * Checks whether or not this \ref Unit is alive by checking the \ref Unit::m_deathState member
         * for the value \ref DeathState::ALIVE
         * @return true if this \ref Unit is alive, false otherwise
         */
        bool IsAlive() const { return (m_deathState == ALIVE); };
        bool IsDying() const { return (m_deathState == JUST_DIED); }
        /**
         * Checks whether or not this \ref Unit is dead by checking the \ref Unit::m_deathState member
         * for the value \ref DeathState::DEAD or \ref DeathState::CORPSE
         * @return true if this \ref Unit is dead or a corpse (also dead), false otherwise
         */
        bool IsDead() const { return (m_deathState == DEAD || m_deathState == CORPSE); };
        /**
         * Returns the current \ref DeathState for this \ref Unit.
         * @return the value of the member \ref Unit::m_deathState
         */
        DeathState GetDeathState() const { return m_deathState; };
        /**
         * Changes the \ref DeathState for this \ref Unit and making sure that some things that should
         * happen when that changes happen, ie: you just died, then you're auras should be removed,
         * any combopoints that you had should be removed etc.
         *
         * This is overwritten to do different things in at least \ref Player, \ref Creature, \ref Pet
         * @param s the new \ref DeathState this \ref Unit should get
         */
        virtual void SetDeathState(DeathState s);           // overwritten in Creature/Player/Pet

        ObjectGuid const& GetOwnerGuid() const { return  GetGuidValue(UNIT_FIELD_SUMMONEDBY); }
        void SetOwnerGuid(ObjectGuid owner) { SetGuidValue(UNIT_FIELD_SUMMONEDBY, owner); }
        ObjectGuid const& GetCreatorGuid() const { return GetGuidValue(UNIT_FIELD_CREATEDBY); }
        void SetCreatorGuid(ObjectGuid creator) { SetGuidValue(UNIT_FIELD_CREATEDBY, creator); }
        ObjectGuid const& GetPetGuid() const { return GetGuidValue(UNIT_FIELD_SUMMON); }
        void SetPetGuid(ObjectGuid pet) { SetGuidValue(UNIT_FIELD_SUMMON, pet); }
        ObjectGuid const& GetCharmerGuid() const { return GetGuidValue(UNIT_FIELD_CHARMEDBY); }
        void SetCharmerGuid(ObjectGuid owner) { SetGuidValue(UNIT_FIELD_CHARMEDBY, owner); }
        ObjectGuid const& GetCharmGuid() const { return GetGuidValue(UNIT_FIELD_CHARM); }
        void SetCharmGuid(ObjectGuid charm) { SetGuidValue(UNIT_FIELD_CHARM, charm); }
        ObjectGuid const& GetTargetGuid() const { return GetGuidValue(UNIT_FIELD_TARGET); }
        void SetTargetGuid(ObjectGuid targetGuid) { SetGuidValue(UNIT_FIELD_TARGET, targetGuid); }
        ObjectGuid const& GetChannelObjectGuid() const { return GetGuidValue(UNIT_FIELD_CHANNEL_OBJECT); }
        void SetChannelObjectGuid(ObjectGuid targetGuid) { SetGuidValue(UNIT_FIELD_CHANNEL_OBJECT, targetGuid); }

        void SetCritterGuid(ObjectGuid critterGuid) { SetGuidValue(UNIT_FIELD_CRITTER, critterGuid); }
        ObjectGuid const& GetCritterGuid() const { return GetGuidValue(UNIT_FIELD_CRITTER); }

        void RemoveMiniPet();
        Pet* GetMiniPet() const;
        void SetMiniPet(Unit* pet) { SetCritterGuid(pet ? pet->GetObjectGuid() : ObjectGuid()); }

        /**
         * Gets either the current charmer (ie mind control) or the owner of this \ref Unit
         * @return the \ref ObjectGuid of either the charmer of this \ref Unit or the owner of it
         */
        ObjectGuid const& GetCharmerOrOwnerGuid() const { return GetCharmerGuid() ? GetCharmerGuid() : GetOwnerGuid(); }
        /**
         * Same thing as \ref Unit::GetCharmerOrOwnerGuid but with the exception that it returns
         * it's own \ref ObjectGuid if it has no owner or charmer.
         * @return either the charmers, owners or it's own \ref ObjectGuid
         */
        ObjectGuid const& GetCharmerOrOwnerOrOwnGuid() const
        {
            if (ObjectGuid const& guid = GetCharmerOrOwnerGuid())
            {
                return guid;
            }
            return GetObjectGuid();
        }
        /**
         * Checks if the charmer or owner is a \ref Player
         * @return true if the charmer or owner is a \ref Player, false otherwise
         * \see ObjectGuid::IsPlayer
         */
        bool isCharmedOwnedByPlayerOrPlayer() const { return GetCharmerOrOwnerOrOwnGuid().IsPlayer(); }

        /**
         * Get's the \ref Player that owns the \ref SpellModifier for this \ref Unit, if this
         * \ref Unit is a \ref Player it's the owner, but if it's a \ref Pet och \ref Totem then
         * then owner of the totem is returned if it's a \ref Player
         * @return The \ref SpellModifier owner for this \ref Unit
         */
        Player* GetSpellModOwner() const;

        /**
         * Returns the \ref Unit that owns this \ref Unit if any
         * @return the \ref Unit that owns this one, NULL if there is no owner
         * \see Unit::GetOwnerGuid
         */
        Unit* GetOwner() const;
        /**
         * Returns the \ref Pet for this \ref Unit if any
         * @return the \ref Pet that is associated with this \ref Unit if any, NULL if there is none
         * \see Unit::GetPetGuid
         */
        Pet* GetPet() const;
        /**
         * Returns the \ref Unit that's currently charming this one if any.
         * @return the \ref Unit that's charming this one, NULL if there is none
         */
        Unit* GetCharmer() const;
        /**
         * Returns the \ref Unit that this one is currently charming
         * @return the \ref Unit that this one is charming, NULL if there is none
         */
        Unit* GetCharm() const;
        /**
         * Removes all \ref Aura s causing this \ref Unit to be charmed/possessed, the \ref Aura s
         * that cause this are:
         * - \ref AuraType::SPELL_AURA_MOD_CHARM
         * - \ref AuraType::SPELL_AURA_MOD_POSSESS
         * - \ref AuraType::SPELL_AURA_MOD_POSSESS_PET
         */
        virtual void Uncharm();
        /**
         * Does the same as \ref Unit::GetCharmerOrOwnerGuid but returns the \ref Unit for that instead
         * @return the \ref Unit that's charming this one or owning it, NULL if there is none
         */
        Unit* GetCharmerOrOwner() const { return GetCharmerGuid() ? GetCharmer() : GetOwner(); }
        /**
         * Does the same a \ref Unit::GetCharmerOrOwner but if there is none of those it returns itself
         * @return a \ref Unit that's either owning or charming this one or just itself.
         */
        Unit* GetCharmerOrOwnerOrSelf()
        {
            if (Unit* u = GetCharmerOrOwner())
            {
                return u;
            }

            return this;
        }
        bool IsCharmerOrOwnerPlayerOrPlayerItself() const;
        Player* GetCharmerOrOwnerPlayerOrPlayerItself();
        Player const* GetCharmerOrOwnerPlayerOrPlayerItself() const;

        /**
         * Set's the current \ref Pet for this \ref Unit
         * @param pet The \ref Pet to add to this \ref Unit
         */
        void SetPet(Pet* pet);
        /**
         * Set's who we're currently charming
         * @param pet The \ref Unit to set as charmed by us
         */
        void SetCharm(Unit* pet);

        /**
         * Adds a guardian to this \ref Unit which will generally defend this \ref Unit when on a
         * threat list.
         * @param pet the guardian to add
         * \see Unit::m_guardianPets
         */
        void AddGuardian(Pet* pet);
        /**
         * Removes a guardian from this \ref Unit
         * @param pet the guardian to remove
         * \see Unit::m_guardianPets
         */
        void RemoveGuardian(Pet* pet);
        /**
         * Removes all current guardians from this \ref Unit
         */
        void RemoveGuardians();
        /**
         * Finds a guardian by it's entry, this is the entry in character.character_pet
         * @param entry the entry to find
         * @return the guardian/\ref Pet found or NULL if there's no such entry in the db
         * \todo Is it the correct entry
         */
        Pet* FindGuardianWithEntry(uint32 entry);
        Pet* GetProtectorPet();                             // expected single case in guardian list

        /**
         * Is this \ref Unit charmed?
         * @return true if the \ref Unit has a charmer, false otherwise
         * \see Unit::GetCharmerGuid
         */
        bool IsCharmed() const { return !GetCharmerGuid().IsEmpty(); }

        /**
         * There's only \ref CharmInfo available if this \ref Unit is in fact charmed by someone
         * @return The \ref CharmInfo for this \ref Unit if any, NULL otherwise
         */
        CharmInfo* GetCharmInfo() { return m_charmInfo; }
        /**
         * Init the \ref CharmInfo struct with data about the \ref Unit that will be charmed
         * @param charm the \ref Unit that is to be charmed
         * @return the created \ref CharmInfo
         * \todo Is the charm param really the unit to be charmed?
         */
        CharmInfo* InitCharmInfo(Unit* charm);

        /**
         * Get's the \ref ObjectGuid for a certain totem type that this \ref Unit has spawned
         * @param slot the slot to get the \ref ObjectGuid for
         * @return the \ref ObjectGuid for the given totem slot
         */
        ObjectGuid const& GetTotemGuid(TotemSlot slot) const { return m_TotemSlot[slot]; }
        /**
         * Gets a certain \ref Totem that this \ref Unit has spawned
         * @param slot the slot to get the \ref Totem for
         * @return The requested totem if there is any spawned, NULL otherwise
         */
        Totem* GetTotem(TotemSlot slot) const;
        /**
         * @return True if all totems slots are used (spawned), false otherwise
         */
        bool IsAllTotemSlotsUsed() const;

        /**
         * This is internal code that should only be called from the \ref Totem summon code
         * @param slot
         * @param totem
         * \internal
         */
        void _AddTotem(TotemSlot slot, Totem* totem);       // only for call from Totem summon code
        /**
         * This is internal code that should only be called from the \ref Totem class.
         * @param totem
         * \internal
         */
        void _RemoveTotem(Totem* totem);                    // only for call from Totem class

        /**
         * This will call the given function for all controlled \ref Unit s, for an example of
         * how one such function could look please have a look at \ref CallForAllControlledUnitsExample
         *
         * The functors operator() should have the following signature:
         * \code{.cpp}
         * void operator()(Unit* unit) { ... };
         * \endcode
         * @param func the functor object used to call for each \ref Unit that we will find matching
         * the mask
         * @param controlledMask a mask telling which of the controlled \ref Unit s we want to call
         * the functor for
         */
        template<typename Func>
        void CallForAllControlledUnits(Func const& func, uint32 controlledMask);
        /**
         * Works pretty much the same way as \ref Unit::CallForAllControlledUnits but instead
         * the functors operator() should have the following signature:
         * \code{.cpp}
         * bool operator()(Unit* unit) { ... };
         * \endcode
         * @param func a functor object used to call for each \ref Unit that we will find matching
         * the mask
         * @param controlledMask a mask telling which of the controlled \ref Unit s we want to call
         * the functor for
         * @return true if the functor returned true for one of the \ref Unit s included by the
         * controlledMask, false if none of them returned true
         * \see Unit::isAttackingPlayer
         */
        template<typename Func>
        bool CheckAllControlledUnits(Func const& func, uint32 controlledMask) const;

        /**
         * Adds a \ref SpellAuraHolder
         * @param holder the holder to add
         * @return true if the holder was added, false otherwise
         */
        bool AddSpellAuraHolder(SpellAuraHolder* holder);
        /**
         * Adds a \ref Aura to \ref Unit::m_modAuras
         * @param aura the \ref Aura to add
         */
        void AddAuraToModList(Aura* aura);


        /**
         * Removes an \ref Aura and sets the reason for removal inside the \ref Aura.
         *
         * removing specific aura stack (From old comment)
         * @param aura the \ref Aura to remove
         * @param mode the reason why it is being removed
         * \see Aura::SetRemoveMode
         */
        void RemoveAura(Aura* aura, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        /**
         * Removes an \ref Aura by spell id and the effect index for that spell to find out
         * which \ref Aura to remove.
         * @param spellId id of the spell which has the sought \ref Aura somewhere
         * @param effindex the effect index for the spell to find the right \ref Aura
         * @param except if != NULL we will not remove this \ref Aura if found
         */
        void RemoveAura(uint32 spellId, SpellEffectIndex effindex, Aura* except = NULL);
        /**
         * Removes a \ref SpellAuraHolder from this \ref Unit. This will remove all the effects that
         * are currently stored in the \ref SpellAuraHolder.
         * @param holder holder to be removed
         * @param mode reason for removal
         */
        void RemoveSpellAuraHolder(SpellAuraHolder* holder, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        /**
         * Removes a single \ref Aura from a \ref SpellAuraHolder to cancel out just one effect of a
         * \ref Spell.
         * @param holder the holder to remove the \ref Aura from
         * @param index the effect index to tell which \ref Aura we want to remove
         * @param mode the reason for removing it
         */
        void RemoveSingleAuraFromSpellAuraHolder(SpellAuraHolder* holder, SpellEffectIndex index, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        /**
         * Does the same thing as \ref Unit::RemoveSingleAuraFromSpellAuraHolder but with spell id
         * instead of a \ref SpellAuraHolder
         * @param id id of the spell to find the \ref Aura in
         * @param index the effect index to tell which \ref Aura we want to remove
         * @param casterGuid guid of the caster to filter it out to just one \ref Aura to remove
         * @param mode reason for removal
         */
        void RemoveSingleAuraFromSpellAuraHolder(uint32 id, SpellEffectIndex index, ObjectGuid casterGuid, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);

        /**
         * Removes all \ref Aura s that a certain spell would cause via it's effects (up to 3 of them
         * per \ref Aura).
         *
         * From old doc: removing specific aura stacka by diff reasons and selections
         * @param spellId id of the spell causing the \ref Aura s you would like to remove
         * @param except a spell that shouldn't be included in the removal
         * @param mode reason for removal
         * \see SpellEntry::Effect
         */
        void RemoveAurasDueToSpell(uint32 spellId, SpellAuraHolder* except = NULL, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        /**
         * Removes all \ref Aura s that a certain spell cast by a certain \ref Item would cause via
         * it's effects (up to 3 of them per \ref Aura).
         * @param castItem the \ref Item that cast the spell
         * @param spellId id of the spell causing the \ref Aura s you would like to remove
         */
        void RemoveAurasDueToItemSpell(Item* castItem, uint32 spellId);
        /**
         * Removes all \ref Aura s that a certain spell cast by a certain \ref Player / \ref Unit
         * would cause via it's effects (up to 3 of them per \ref Aura)
         * @param spellId id of the \ref Spell causing the \ref Aura s you would like to remove
         * @param casterGuid \ref ObjectGuid of the caster
         * @param mode reason for removal
         */
        void RemoveAurasByCasterSpell(uint32 spellId, ObjectGuid casterGuid, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAurasDueToSpellBySteal(uint32 spellId, ObjectGuid casterGuid, Unit* stealer);
        /**
         * Removes all \ref Aura s caused by a certain spell because it was canceled.
         * @param spellId id of the \ref Spell causing the \ref Aura s you would like to remove
         */
        void RemoveAurasDueToSpellByCancel(uint32 spellId);

        // removing unknown aura stacks by diff reasons and selections
        /**
         * From old doc: removing unknown aura stacks by diff reasons and selections
         * \todo Document and find out what it does
         */
        void RemoveNotOwnTrackedTargetAuras(uint32 newPhase = 0x0);
        /**
         * Removes all \ref SpellAuraHolder s that have the given \ref Mechanics mask which is created
         * by doing something like the following if we want a mask for \ref Mechanics::MECHANIC_SAPPED:
         * \code{.cpp}
         * uint32 mask = 1 << (MECHANIC_SAPPED - 1);
         * \endcode
         * @param mechMask a mask of \ref Mechanics, see \ref MECHANIC_NOT_REMOVED_BY_SHAPESHIFT,
         * \ref IMMUNE_TO_ROOT_AND_SNARE_MASK for examples
         * @param exceptSpellId id of a \ref Spell that shouldn't be removed
         * @param non_positive if we should remove non positive \ref Aura s or not, defaults to false
         */
        void RemoveAurasAtMechanicImmunity(uint32 mechMask, uint32 exceptSpellId, bool non_positive = false);
        /**
         * Removes all \ref Spell s that cause the given \ref AuraType
         * @param auraType the type of auras we would like to remove spells for
         */
        void RemoveSpellsCausingAura(AuraType auraType);
        /**
         * Same as \ref Unit::RemoveSpellsCausingAura but with an exception
         * for a \ref SpellAuraHolder that shouldn't be removed
         * @param auraType the type of auras we would like to remove spells for
         * @param except this will be excepted from removal
         */
        void RemoveSpellsCausingAura(AuraType auraType, SpellAuraHolder* except);
        /**
         * Same as \ref Unit::RemoveSpellsCausingAura but for a matching caster aswell.
         * @param auraType the type of auras we would like to remove spells for
         * @param casterGuid remove the aura only if the caster is equal to this guid
         */
        void RemoveSpellsCausingAura(AuraType auraType, ObjectGuid casterGuid);
        /**
         * Removes all ranks of the given \ref Spell, ie: if the spellid of rank 1 inner fire is
         * given all the ranks of it will be removed.
         * @param spellId id of the spell we want to remove all ranks for
         */
        void RemoveRankAurasDueToSpell(uint32 spellId);
        /**
         *
         * @param holder
         * @return true if we could remove something (and did), false otherwise
         * \todo Document what this does and break into smaller functions!
         */
        bool RemoveNoStackAurasDueToAuraHolder(SpellAuraHolder* holder);
        /**
         * Removes all \ref Aura s that have the given interrupt flags
         * @param flags see \ref AuraInterruptFlags for possible flags
         */
        void RemoveAurasWithInterruptFlags(uint32 flags);
        /**
         * Removes all \ref Aura s that have the given attributes
         * @param flags see \ref SpellAttributes for possible values
         */
        void RemoveAurasWithAttribute(uint32 flags);
        /**
         * Removes all \ref Aura s which can be dispelled by the given \ref DispelType
         * @param type the given type that you want to remove all \ref Aura s for
         * @param casterGuid if this isn't 0 it will be checked that the caster of the \ref Spell is
         * the same as the given guid before removal.
         */
        void RemoveAurasWithDispelType(DispelType type, ObjectGuid casterGuid = ObjectGuid());
        /**
         * Removes all \ref Aura s.
         * @param mode the reason for removal
         */
        void RemoveAllAuras(AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveArenaAuras(bool onleave = false);
        /**
         * Removes all \ref Aura s on this \ref Unit s death. Removes all visible \ref Aura s and
         * disabled the mods for the passive ones (taken from old docs). The reason used is
         * \ref AuraRemoveMode::AURA_REMOVE_BY_DEATH
         * \todo Where does it remove the passive ones?
         */
        void RemoveAllAurasOnDeath();
        /**
         * used when evading to remove all auras except some special auras. Linked and flying
         * \ref Aura s shouldn't be removed on evade.
         * \todo Are linked and flying auras really not removed on evade?
         */
        void RemoveAllAurasOnEvade();

        // remove specific aura on cast
        void RemoveAurasOnCast(SpellEntry const* castedSpellEntry);

        // removing specific aura FROM stack by diff reasons and selections
        void RemoveAuraHolderFromStack(uint32 spellId, uint32 stackAmount = 1, ObjectGuid casterGuid = ObjectGuid(), AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAuraHolderDueToSpellByDispel(uint32 spellId, uint32 stackAmount, ObjectGuid casterGuid, Unit* dispeller);

        void DelaySpellAuraHolder(uint32 spellId, int32 delaytime, ObjectGuid casterGuid);

        float GetResistanceBuffMods(SpellSchools school, bool positive) const { return GetFloatValue(positive ? UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE + school : UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE + school); }
        void SetResistanceBuffMods(SpellSchools school, bool positive, float val) { SetFloatValue(positive ? UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE + school : UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE + school, val); }
        void ApplyResistanceBuffModsMod(SpellSchools school, bool positive, float val, bool apply) { ApplyModSignedFloatValue(positive ? UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE + school : UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE + school, val, apply); }
        void ApplyResistanceBuffModsPercentMod(SpellSchools school, bool positive, float val, bool apply) { ApplyPercentModFloatValue(positive ? UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE + school : UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE + school, val, apply); }
        void InitStatBuffMods()
        {
            for (int i = STAT_STRENGTH; i < MAX_STATS; ++i) SetFloatValue(UNIT_FIELD_POSSTAT0 + i, 0);
            for (int i = STAT_STRENGTH; i < MAX_STATS; ++i) SetFloatValue(UNIT_FIELD_NEGSTAT0 + i, 0);
        }
        void ApplyStatBuffMod(Stats stat, float val, bool apply) { ApplyModSignedFloatValue((val > 0 ? UNIT_FIELD_POSSTAT0 + stat : UNIT_FIELD_NEGSTAT0 + stat), val, apply); }
        void ApplyStatPercentBuffMod(Stats stat, float val, bool apply)
        {
            ApplyPercentModFloatValue(UNIT_FIELD_POSSTAT0 + stat, val, apply);
            ApplyPercentModFloatValue(UNIT_FIELD_NEGSTAT0 + stat, val, apply);
        }
        void SetCreateStat(Stats stat, float val) { m_createStats[stat] = val; }
        void SetCreateHealth(uint32 val) { SetUInt32Value(UNIT_FIELD_BASE_HEALTH, val); }
        uint32 GetCreateHealth() const { return GetUInt32Value(UNIT_FIELD_BASE_HEALTH); }
        void SetCreateMana(uint32 val) { SetUInt32Value(UNIT_FIELD_BASE_MANA, val); }
        uint32 GetCreateMana() const { return GetUInt32Value(UNIT_FIELD_BASE_MANA); }
        uint32 GetCreatePowers(Powers power) const;
        uint32 GetCreateMaxPowers(Powers power) const;
        float GetPosStat(Stats stat) const { return GetFloatValue(UNIT_FIELD_POSSTAT0 + stat); }
        float GetNegStat(Stats stat) const { return GetFloatValue(UNIT_FIELD_NEGSTAT0 + stat); }
        float GetCreateStat(Stats stat) const { return m_createStats[stat]; }

        void SetCurrentCastedSpell(Spell* pSpell);
        virtual void ProhibitSpellSchool(SpellSchoolMask /*idSchoolMask*/, uint32 /*unTimeMs*/) { }
        void InterruptSpell(CurrentSpellTypes spellType, bool withDelayed = true, bool sendAutoRepeatCancelToClient = true);
        void FinishSpell(CurrentSpellTypes spellType, bool ok = true);

        // set withDelayed to true to account delayed spells as casted
        // delayed+channeled spells are always accounted as casted
        // we can skip channeled or delayed checks using flags
        bool IsNonMeleeSpellCasted(bool withDelayed, bool skipChanneled = false, bool skipAutorepeat = false) const;

        // set withDelayed to true to interrupt delayed spells too
        // delayed+channeled spells are always interrupted
        void InterruptNonMeleeSpells(bool withDelayed, uint32 spellid = 0);

        Spell* GetCurrentSpell(CurrentSpellTypes spellType) const { return m_currentSpells[spellType]; }
        Spell* GetCurrentSpell(uint32 spellType) const { return m_currentSpells[spellType]; }
        Spell* FindCurrentSpellBySpellId(uint32 spell_id) const;

        bool CheckAndIncreaseCastCounter();
        void DecreaseCastCounter() { if (m_castCounter) --m_castCounter; }

        ObjectGuid m_ObjectSlotGuid[MAX_OBJECT_SLOT];
        uint32 m_detectInvisibilityMask;
        uint32 m_invisibilityMask;

        ShapeshiftForm GetShapeshiftForm() const { return ShapeshiftForm(GetByteValue(UNIT_FIELD_BYTES_2, 3)); }
        void  SetShapeshiftForm(ShapeshiftForm form) { SetByteValue(UNIT_FIELD_BYTES_2, 3, form); }

        bool IsInFeralForm() const
        {
            ShapeshiftForm form = GetShapeshiftForm();
            return form == FORM_CAT || form == FORM_BEAR;
        }

        bool IsInDisallowedMountForm() const
        {
            ShapeshiftForm form = GetShapeshiftForm();
            return form != FORM_NONE && form != FORM_BATTLESTANCE && form != FORM_BERSERKERSTANCE && form != FORM_DEFENSIVESTANCE &&
                   form != FORM_SHADOW;
        }

        float m_modMeleeHitChance;
        float m_modRangedHitChance;
        float m_modSpellHitChance;
        int32 m_baseSpellCritChance;

        float m_threatModifier[MAX_SPELL_SCHOOL];
        float m_modAttackSpeedPct[3];

        // Event handler
        EventProcessor m_Events;

        // stat system
        bool HandleStatModifier(UnitMods unitMod, UnitModifierType modifierType, float amount, bool apply);
        void SetModifierValue(UnitMods unitMod, UnitModifierType modifierType, float value) { m_auraModifiersGroup[unitMod][modifierType] = value; }
        float GetModifierValue(UnitMods unitMod, UnitModifierType modifierType) const;
        float GetTotalStatValue(Stats stat) const;
        float GetTotalAuraModValue(UnitMods unitMod) const;
        SpellSchools GetSpellSchoolByAuraGroup(UnitMods unitMod) const;
        Stats GetStatByAuraGroup(UnitMods unitMod) const;
        Powers GetPowerTypeByAuraGroup(UnitMods unitMod) const;
        bool CanModifyStats() const { return m_canModifyStats; }
        void SetCanModifyStats(bool modifyStats) { m_canModifyStats = modifyStats; }
        virtual bool UpdateStats(Stats stat) = 0;
        virtual bool UpdateAllStats() = 0;
        virtual void UpdateResistances(uint32 school) = 0;
        virtual void UpdateArmor() = 0;
        virtual void UpdateMaxHealth() = 0;
        virtual void UpdateMaxPower(Powers power) = 0;
        virtual void UpdateAttackPowerAndDamage(bool ranged = false) = 0;
        virtual void UpdateDamagePhysical(WeaponAttackType attType) = 0;
        float GetTotalAttackPowerValue(WeaponAttackType attType) const;
        float GetWeaponDamageRange(WeaponAttackType attType , WeaponDamageRange type) const;
        void SetBaseWeaponDamage(WeaponAttackType attType , WeaponDamageRange damageRange, float value) { m_weaponDamage[attType][damageRange] = value; }

        // Visibility system
        UnitVisibility GetVisibility() const { return m_Visibility; }
        void SetVisibility(UnitVisibility x);
        void UpdateVisibilityAndView() override;            // overwrite WorldObject::UpdateVisibilityAndView()

        // common function for visibility checks for player/creatures with detection code
        bool IsVisibleForOrDetect(Unit const* u, WorldObject const* viewPoint, bool detect, bool inVisibleList = false, bool is3dDistance = true) const;
        bool canDetectInvisibilityOf(Unit const* u) const;
        void SetPhaseMask(uint32 newPhaseMask, bool update) override;// overwrite WorldObject::SetPhaseMask

        // virtual functions for all world objects types
        bool IsVisibleForInState(Player const* u, WorldObject const* viewPoint, bool inVisibleList) const override;
        // function for low level grid visibility checks in player/creature cases
        virtual bool IsVisibleInGridForPlayer(Player* pl) const = 0;
        bool IsInvisibleForAlive() const;

        TrackedAuraTargetMap&       GetTrackedAuraTargets(TrackedAuraType type)       { return m_trackedAuraTargets[type]; }
        TrackedAuraTargetMap const& GetTrackedAuraTargets(TrackedAuraType type) const { return m_trackedAuraTargets[type]; }
        SpellImmuneList m_spellImmune[MAX_SPELL_IMMUNITY];

        // Threat related methods
        bool CanHaveThreatList(bool ignoreAliveState = false) const;
        void AddThreat(Unit* pVictim, float threat = 0.0f, bool crit = false, SpellSchoolMask schoolMask = SPELL_SCHOOL_MASK_NONE, SpellEntry const* threatSpell = NULL);
        float ApplyTotalThreatModifier(float threat, SpellSchoolMask schoolMask = SPELL_SCHOOL_MASK_NORMAL);
        void DeleteThreatList();
        bool IsSecondChoiceTarget(Unit* pTarget, bool checkThreatArea) const;
        bool SelectHostileTarget();
        void TauntApply(Unit* pVictim);
        void TauntFadeOut(Unit* taunter);
        void FixateTarget(Unit* pVictim);
        ObjectGuid GetFixateTargetGuid() const { return m_fixateTargetGuid; }
        ThreatManager& GetThreatManager() { return m_ThreatManager; }
        ThreatManager const& GetThreatManager() const { return m_ThreatManager; }
        void addHatedBy(HostileReference* pHostileReference) { m_HostileRefManager.insertFirst(pHostileReference); };
        void removeHatedBy(HostileReference* /*pHostileReference*/) { /* nothing to do yet */ }
        HostileRefManager& GetHostileRefManager() { return m_HostileRefManager; }

        SpellAuraHolder* GetVisibleAura(uint8 slot) const
        {
            VisibleAuraMap::const_iterator itr = m_visibleAuras.find(slot);
            if (itr != m_visibleAuras.end())
            {
                return itr->second;
            }
            return NULL;
        }
        void SetVisibleAura(uint8 slot, SpellAuraHolder* holder)
        {
            if (!holder)
            {
                m_visibleAuras.erase(slot);
            }
            else
            {
                m_visibleAuras[slot] = holder;
            }
        }
        VisibleAuraMap const& GetVisibleAuras() const { return m_visibleAuras; }
        uint8 GetVisibleAurasCount() const { return m_visibleAuras.size(); }

        Aura* GetAura(uint32 spellId, SpellEffectIndex effindex);
        Aura* GetAura(AuraType type, SpellFamily family, uint64 familyFlag, uint32 familyFlag2 = 0, ObjectGuid casterGuid = ObjectGuid());
        Aura* GetTriggeredByClientAura(uint32 spellId) const;
        SpellAuraHolder* GetSpellAuraHolder(uint32 spellid) const;
        SpellAuraHolder* GetSpellAuraHolder(uint32 spellid, ObjectGuid casterGUID) const;

        SpellAuraHolderMap&       GetSpellAuraHolderMap()       { return m_spellAuraHolders; }
        SpellAuraHolderMap const& GetSpellAuraHolderMap() const { return m_spellAuraHolders; }
        /**
         * Get's a list of all the \ref Aura s of the given \ref AuraType that are currently
         * affecting this \ref Unit.
         * @param type the aura type we want to find
         * @return A list of the auras currently applied to the \ref Unit with the given \ref AuraType
         * \see Unit::m_modAuras
         */
        AuraList const& GetAurasByType(AuraType type) const { return m_modAuras[type]; }
        void ApplyAuraProcTriggerDamage(Aura* aura, bool apply);

        int32 GetTotalAuraModifier(AuraType auratype) const;
        float GetTotalAuraMultiplier(AuraType auratype) const;
        int32 GetMaxPositiveAuraModifier(AuraType auratype) const;
        int32 GetMaxNegativeAuraModifier(AuraType auratype) const;

        int32 GetTotalAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const;
        float GetTotalAuraMultiplierByMiscMask(AuraType auratype, uint32 misc_mask) const;
        int32 GetMaxPositiveAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const;
        int32 GetMaxNegativeAuraModifierByMiscMask(AuraType auratype, uint32 misc_mask) const;

        int32 GetTotalAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;
        float GetTotalAuraMultiplierByMiscValue(AuraType auratype, int32 misc_value) const;
        int32 GetMaxPositiveAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;
        int32 GetMaxNegativeAuraModifierByMiscValue(AuraType auratype, int32 misc_value) const;

        // misc have plain value but we check it fit to provided values mask (mask & (1 << (misc-1)))
        float GetTotalAuraMultiplierByMiscValueForMask(AuraType auratype, uint32 mask) const;

        Aura* GetDummyAura(uint32 spell_id) const;

        uint32 m_AuraFlags;

        uint32 GetDisplayId() const { return GetUInt32Value(UNIT_FIELD_DISPLAYID); }
        void SetDisplayId(uint32 modelId);
        uint32 GetNativeDisplayId() const { return GetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID); }
        void SetNativeDisplayId(uint32 modelId) { SetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID, modelId); }
        void SetTransform(uint32 spellid) { m_transform = spellid;}
        uint32 getTransForm() const { return m_transform;}

        // at any changes to scale and/or displayId
        void UpdateModelData();
        void SendCollisionHeightUpdate(float height);

        DynamicObject* GetDynObject(uint32 spellId, SpellEffectIndex effIndex);
        DynamicObject* GetDynObject(uint32 spellId);
        void AddDynObject(DynamicObject* dynObj);
        void RemoveDynObject(uint32 spellid);
        void RemoveDynObjectWithGUID(ObjectGuid guid) { m_dynObjGUIDs.remove(guid); }
        void RemoveAllDynObjects();

        GameObject* GetGameObject(uint32 spellId) const;
        void AddGameObject(GameObject* gameObj);
        void AddWildGameObject(GameObject* gameObj);
        void RemoveGameObject(GameObject* gameObj, bool del);
        void RemoveGameObject(uint32 spellid, bool del);
        void RemoveAllGameObjects();

        uint32 CalculateDamage(WeaponAttackType attType, bool normalized);
        float GetAPMultiplier(WeaponAttackType attType, bool normalized);
        void ModifyAuraState(AuraState flag, bool apply);
        bool HasAuraState(AuraState flag) const { return HasFlag(UNIT_FIELD_AURASTATE, 1 << (flag - 1)); }
        bool HasAuraStateForCaster(AuraState flag, ObjectGuid casterGuid) const;
        void UnsummonAllTotems();
        Unit* SelectMagnetTarget(Unit* victim, Spell* spell = NULL, SpellEffectIndex eff = EFFECT_INDEX_0);

        int32 SpellBonusWithCoeffs(SpellEntry const* spellProto, int32 total, int32 benefit, int32 ap_benefit, DamageEffectType damagetype, bool donePart, float defCoeffMod = 1.0f);
        int32 SpellBaseDamageBonusDone(SpellSchoolMask schoolMask);
        int32 SpellBaseDamageBonusTaken(SpellSchoolMask schoolMask);
        uint32 SpellDamageBonusDone(Unit* pVictim, SpellEntry const* spellProto, uint32 pdamage, DamageEffectType damagetype, uint32 stack = 1);
        uint32 SpellDamageBonusTaken(Unit* pCaster, SpellEntry const* spellProto, uint32 pdamage, DamageEffectType damagetype, uint32 stack = 1);
        int32 SpellBaseHealingBonusDone(SpellSchoolMask schoolMask);
        int32 SpellBaseHealingBonusTaken(SpellSchoolMask schoolMask);
        uint32 SpellHealingBonusDone(Unit* pVictim, SpellEntry const* spellProto, int32 healamount, DamageEffectType damagetype, uint32 stack = 1);
        uint32 SpellHealingBonusTaken(Unit* pCaster, SpellEntry const* spellProto, int32 healamount, DamageEffectType damagetype, uint32 stack = 1);
        uint32 MeleeDamageBonusDone(Unit* pVictim, uint32 damage, WeaponAttackType attType, SpellEntry const* spellProto = NULL, DamageEffectType damagetype = DIRECT_DAMAGE, uint32 stack = 1);
        uint32 MeleeDamageBonusTaken(Unit* pCaster, uint32 pdamage, WeaponAttackType attType, SpellEntry const* spellProto = NULL, DamageEffectType damagetype = DIRECT_DAMAGE, uint32 stack = 1);

        bool   IsSpellBlocked(Unit* pCaster, SpellEntry const* spellProto, WeaponAttackType attackType = BASE_ATTACK);
        bool   IsSpellCrit(Unit* pVictim, SpellEntry const* spellProto, SpellSchoolMask schoolMask, WeaponAttackType attackType = BASE_ATTACK);
        uint32 SpellCriticalDamageBonus(SpellEntry const* spellProto, uint32 damage, Unit* pVictim);
        uint32 SpellCriticalHealingBonus(SpellEntry const* spellProto, uint32 damage, Unit* pVictim);

        bool IsTriggeredAtSpellProcEvent(Unit* pVictim, SpellAuraHolder* holder, SpellEntry const* procSpell, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, bool isVictim, SpellProcEventEntry const*& spellProcEvent);
        // Aura proc handlers
        SpellAuraProcResult HandleDummyAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleHasteAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleSpellCritChanceAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleProcTriggerSpellAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleProcTriggerDamageAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleOverrideClassScriptAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleMendingAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleModCastingSpeedNotStackAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleReflectSpellsSchoolAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleModPowerCostSchoolAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleMechanicImmuneResistanceAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleModDamageFromCasterAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleAddFlatModifierAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleAddPctModifierAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleModDamagePercentDoneAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleModRating(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleSpellMagnetAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleManaShieldAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleModResistanceAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleRemoveByDamageChanceProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleInvisibilityAuraProc(Unit* pVictim, uint32 damage, Aura* triggeredByAura, SpellEntry const* procSpell, uint32 procFlag, uint32 procEx, uint32 cooldown);
        SpellAuraProcResult HandleNULLProc(Unit* /*pVictim*/, uint32 /*damage*/, Aura* /*triggeredByAura*/, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/)
        {
            // no proc handler for this aura type
            return SPELL_AURA_PROC_OK;
        }
        SpellAuraProcResult HandleCantTrigger(Unit* /*pVictim*/, uint32 /*damage*/, Aura* /*triggeredByAura*/, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/)
        {
            // this aura type can't proc
            return SPELL_AURA_PROC_CANT_TRIGGER;
        }

        uint32 GetRegenTimer() const { return m_regenTimer; }

        void SetContestedPvP(Player* attackedPlayer = NULL);

        void ApplySpellImmune(uint32 spellId, uint32 op, uint32 type, bool apply);
        void ApplySpellDispelImmunity(const SpellEntry* spellProto, DispelType type, bool apply);
        virtual bool IsImmuneToSpell(SpellEntry const* spellInfo, bool castOnSelf);
        bool IsImmunedToDamage(SpellSchoolMask meleeSchoolMask);
        virtual bool IsImmuneToSpellEffect(SpellEntry const* spellInfo, SpellEffectIndex index, bool castOnSelf) const;

        uint32 CalcArmorReducedDamage(Unit* pVictim, const uint32 damage);
        void CalculateDamageAbsorbAndResist(Unit* pCaster, SpellSchoolMask schoolMask, DamageEffectType damagetype, const uint32 damage, uint32* absorb, uint32* resist, bool canReflect = false);
        void CalculateAbsorbResistBlock(Unit* pCaster, SpellNonMeleeDamage* damageInfo, SpellEntry const* spellProto, WeaponAttackType attType = BASE_ATTACK);
        void CalculateHealAbsorb(uint32 heal, uint32* absorb);

        void UpdateSpeed(UnitMoveType mtype, bool forced, float ratio = 1.0f, bool ignoreChange = false);
        float GetSpeed(UnitMoveType mtype) const;
        float GetSpeedRate(UnitMoveType mtype) const { return m_speed_rate[mtype]; }
        void SetSpeedRate(UnitMoveType mtype, float rate, bool forced = false, bool ignoreChange = false);

        void KnockBackFrom(Unit* target, float horizontalSpeed, float verticalSpeed);
        void KnockBackWithAngle(float angle, float horizontalSpeed, float verticalSpeed);

        void _RemoveAllAuraMods();
        void _ApplyAllAuraMods();

        int32 CalculateSpellDamage(Unit const* target, SpellEntry const* spellProto, SpellEffectIndex effect_index, int32 const* basePoints = NULL);

        uint32 CalcNotIgnoreAbsorbDamage(uint32 damage, SpellSchoolMask damageSchoolMask, SpellEntry const* spellInfo = NULL);
        uint32 CalcNotIgnoreDamageReduction(uint32 damage, SpellSchoolMask damageSchoolMask);
        int32 CalculateAuraDuration(SpellEntry const* spellProto, uint32 effectMask, int32 duration, Unit const* caster, Spell const* spell = NULL);

        float CalculateLevelPenalty(SpellEntry const* spellProto) const;

        void AddFollower(FollowerReference* pRef) { m_FollowingRefManager.insertFirst(pRef); }
        void RemoveFollower(FollowerReference* /*pRef*/) { /* nothing to do yet */ }

        MotionMaster* GetMotionMaster() { return &i_motionMaster; }

        bool IsStopped() const { return !(hasUnitState(UNIT_STAT_MOVING)); }
        void StopMoving(bool forceSendStop = false);
        void InterruptMoving(bool forceSendStop = false);

        void SetFeared(bool apply, ObjectGuid casterGuid = ObjectGuid(), uint32 spellID = 0, uint32 time = 0);
        void SetConfused(bool apply, ObjectGuid casterGuid = ObjectGuid(), uint32 spellID = 0);
        void SetFeignDeath(bool apply, ObjectGuid casterGuid = ObjectGuid(), uint32 spellID = 0);

        void AddComboPointHolder(uint32 lowguid) { m_ComboPointHolders.insert(lowguid); }
        void RemoveComboPointHolder(uint32 lowguid) { m_ComboPointHolders.erase(lowguid); }
        void ClearComboPointHolders();

        ///----------Pet responses methods-----------------
        void SendPetActionFeedback(uint8 msg, uint32 spellId = 0);
        void SendPetTalk(uint32 pettalk);
        void SendPetAIReaction();
        ///----------End of Pet responses methods----------

        void PropagateSpeedChange() { GetMotionMaster()->PropagateSpeedChange(); }

        // reactive attacks
        void ClearAllReactives();
        void StartReactiveTimer(ReactiveType reactive) { m_reactiveTimer[reactive] = REACTIVE_TIMER_START;}
        void UpdateReactives(uint32 p_time);

        // group updates
        void UpdateAuraForGroup(uint8 slot);

        // pet auras
        typedef std::set<PetAura const*> PetAuraSet;
        PetAuraSet m_petAuras;
        void AddPetAura(PetAura const* petSpell);
        void RemovePetAura(PetAura const* petSpell);

        // Movement info
        MovementInfo m_movementInfo;
        Movement::MoveSpline* movespline;

        void ScheduleAINotify(uint32 delay);
        bool IsAINotifyScheduled() const { return m_AINotifyScheduled;}
        void _SetAINotifyScheduled(bool on) { m_AINotifyScheduled = on;}       // only for call from RelocationNotifyEvent code
        void OnRelocated();

        bool IsLinkingEventTrigger() const { return m_isCreatureLinkingTrigger; }

        virtual bool CanSwim() const = 0;
        virtual bool CanFly() const = 0;

        bool IsSplineEnabled() const;

        /**
         * @brief Re-sends the unit's in-flight movement spline to one viewer.
         *
         * Used when the unit becomes visible to a player mid-move: the create
         * block is a stationary snapshot, so without this the unit would stand
         * still until its next spline launch.
         *
         * @param viewer The player who just gained visibility of this unit.
         */
        void SendCurrentSplineTo(Player* viewer);

        bool IsInWorgenForm(bool inPermanent = false) const;
        bool HasWorgenForm() const;

        /**
         * @brief Next sequence number for a movement-state packet.
         *
         * Every movement-state change carrying a uint32 -- can-fly, water walk,
         * fall, hover, gravity, root -- plus every forced speed change, the
         * knockback and the collision-height update stamp one of these. The
         * fifteen state and speed senders used to send a literal 0; knockback
         * sent 0 and collision height sent the wall-clock game time.
         *
         * What the client actually does with it, from the 18414 handler (which
         * lives in an undefined-code bank, so it is visible in Wow.exe.lst and
         * not in the decompile): it loads the value once and enqueues it. The
         * only conditional in the whole handler is whether the mover GUID
         * resolved. There is NO validation -- a repeated or zero counter is
         * accepted, and the queue is ordered by timestamp, never by counter.
         *
         * So this does not fix any client-visible behaviour, and it was wrong to
         * suppose it might: a repeated counter was the leading explanation for
         * ".gm fly works once then stops", and that explanation is dead.
         *
         * It is still worth doing for two reasons that are not cosmetic. The
         * client echoes the value verbatim at offset 4 of the matching
         * acknowledgement, so it is the only handle the server has for matching
         * an ack to the command that caused it. And 0 is not a neutral value: the
         * client uses counter == 0 to CLASSIFY a record as client-originated,
         * because its own local entry points pass 0. A server sending 0 mislabels
         * its own change as one the client made.
         *
         * Hence counting from 1. Corroborated independently: no captured body
         * carries 0, the lowest observed is 4, and in the movement block a zero
         * counter is encoded as absent.
         *
         * Per-Unit, not per-session or per-family: the record lives on the
         * mover's own movement component, and retail shows one monotonic
         * sequence per mover shared across all forced-movement families. That is
         * why observed sequences skip values (66, 84, 108, 229, 249...) -- every
         * kind of movement packet for that mover draws from the one series.
         */
        uint32 NextMovementCounter() { return ++m_movementCounter; }

        // Packet builders
        void BuildForceMoveRootPacket(WorldPacket* data, bool apply, uint32 value);
        void BuildMoveWaterWalkPacket(WorldPacket* data, bool apply, uint32 value);
        void BuildSendPlayVisualPacket(WorldPacket* data, uint32 value, bool impact);
        void BuildMoveSetCanFlyPacket(WorldPacket* data, bool apply, uint32 value);
        void BuildMoveFeatherFallPacket(WorldPacket* data, bool apply, uint32 value);
        void BuildMoveHoverPacket(WorldPacket* data, bool apply, uint32 value);
        void BuildMoveLevitatePacket(WorldPacket* data, bool apply, uint32 value);

        // Take possession of an unit (pet, creature, ...)
        bool TakePossessOf(Unit* possessed);

        // Take possession of a new spawned unit
        Unit* TakePossessOf(SpellEntry const* spellEntry, SummonPropertiesEntry const* summonProp, SpellEffectEntry const* spellEffect, float x, float y, float z, float ang);

        // Reset control to player
        void ResetControlState(bool attackCharmer = true);

    protected:
        explicit Unit();

        void _UpdateSpells(uint32 time);
        void _UpdateAutoRepeatSpell();

        uint32 m_attackTimer[MAX_ATTACK];

        float m_createStats[MAX_STATS];

        AttackerSet m_attackers;
        Unit* m_attacking;

        DeathState m_deathState; ///< The current state of life/death for this \ref Unit

        SpellAuraHolderMap m_spellAuraHolders;
        SpellAuraHolderMap::iterator m_spellAuraHoldersUpdateIterator; // != end() in Unit::m_spellAuraHolders update and point to next element
        AuraList m_deletedAuras;                            // auras removed while in ApplyModifier and waiting deleted
        SpellAuraHolderList m_deletedHolders;

        // Store Auras for which the target must be tracked
        TrackedAuraTargetMap m_trackedAuraTargets[MAX_TRACKED_AURA_TYPES];

        GuidList m_dynObjGUIDs;

        typedef std::list<GameObject*> GameObjectList;
        GameObjectList m_gameObj;
        typedef std::map<uint32, ObjectGuid> WildGameObjectMap;
        WildGameObjectMap m_wildGameObjs;
        bool m_isSorted;
        uint32 m_transform;

        AuraList m_modAuras[TOTAL_AURAS];
        float m_auraModifiersGroup[UNIT_MOD_END][MODIFIER_TYPE_END];
        float m_weaponDamage[MAX_ATTACK][2];
        bool m_canModifyStats;
        // std::list< spellEffectPair > AuraSpells[TOTAL_AURAS];  // TODO: use this if ok for mem
        VisibleAuraMap m_visibleAuras;

        float m_speed_rate[MAX_MOVE_TYPE];

        CharmInfo* m_charmInfo;

        virtual SpellSchoolMask GetMeleeDamageSchoolMask() const;

        MotionMaster i_motionMaster;

        uint32 m_reactiveTimer[MAX_REACTIVE];
        uint32 m_regenTimer;
        uint32 m_holyPowerRegenTimer;

        VehicleInfo* m_vehicleInfo;
        void DisableSpline();
        bool m_isCreatureLinkingTrigger;
        bool m_isSpawningLinked;

    private:
        void CleanupDeletedAuras();
        void UpdateSplineMovement(uint32 t_diff);

        // player or player's pet
        float GetCombatRatingReduction(CombatRating cr) const;
        uint32 GetCombatRatingDamageReduction(CombatRating cr, float rate, float cap, uint32 damage) const;

        Unit* _GetTotem(TotemSlot slot) const;              // for templated function without include need
        Pet* _GetPet(ObjectGuid guid) const;                // for templated function without include need

        // Wrapper called by DealDamage when a creature is killed
        void JustKilledCreature(Creature* victim, Player* responsiblePlayer);

        uint32 m_state;                                     // Even derived shouldn't modify
        uint32 m_CombatTimer;

        // Sequence number stamped into every movement-state packet that carries
        // one -- can-fly, water walk, fall, hover, gravity, root. See
        // NextMovementCounter().
        uint32 m_movementCounter;

        Spell* m_currentSpells[CURRENT_MAX_SPELL];
        uint32 m_castCounter;                               // count casts chain of triggered spells for prevent infinity cast crashes

        UnitVisibility m_Visibility;
        Position m_last_notified_position;
        bool m_AINotifyScheduled;
        TimeTracker m_movesplineTimer;

        Diminishing m_Diminishing;
        // Manage all Units threatening us
        ThreatManager m_ThreatManager;
        // Manage all Units that are threatened by us
        HostileRefManager m_HostileRefManager;

        FollowerRefManager m_FollowingRefManager;

        ComboPointHolderSet m_ComboPointHolders;

        GuidSet m_guardianPets;

        ObjectGuid m_TotemSlot[MAX_TOTEM_SLOT];

        ObjectGuid m_fixateTargetGuid;                      //< Stores the Guid of a fixated target

    private:                                                // Error traps for some wrong args using
        // this will catch and prevent build for any cases when all optional args skipped and instead triggered used non boolean type
        // no bodies expected for this declarations
        template <typename TR>
        void CastSpell(Unit* Victim, uint32 spell, TR triggered);
        template <typename TR>
        void CastSpell(Unit* Victim, SpellEntry const* spell, TR triggered);
        template <typename TR>
        void CastCustomSpell(Unit* Victim, uint32 spell, int32 const* bp0, int32 const* bp1, int32 const* bp2, TR triggered);
        template <typename SP, typename TR>
        void CastCustomSpell(Unit* Victim, SpellEntry const* spell, int32 const* bp0, int32 const* bp1, int32 const* bp2, TR triggered);
        template <typename TR>
        void CastSpell(float x, float y, float z, uint32 spell, TR triggered);
        template <typename TR>
        void CastSpell(float x, float y, float z, SpellEntry const* spell, TR triggered);
};

template<typename Func>
void Unit::CallForAllControlledUnits(Func const& func, uint32 controlledMask)
{
    if (controlledMask & CONTROLLED_PET)
        if (Pet* pet = GetPet())
        {
            func(pet);
        }

    if (controlledMask & CONTROLLED_MINIPET)
        if (Pet* mini = GetMiniPet())
        {
            func(mini);
        }

    if (controlledMask & CONTROLLED_GUARDIANS)
    {
        for (GuidSet::const_iterator itr = m_guardianPets.begin(); itr != m_guardianPets.end();)
            if (Pet* guardian = _GetPet(*(itr++)))
            {
                func(guardian);
            }
    }

    if (controlledMask & CONTROLLED_TOTEMS)
    {
        for (int i = 0; i < MAX_TOTEM_SLOT; ++i)
            if (Unit* totem = _GetTotem(TotemSlot(i)))
            {
                func(totem);
            }
    }

    if (controlledMask & CONTROLLED_CHARM)
        if (Unit* charm = GetCharm())
        {
            func(charm);
        }
}


template<typename Func>
bool Unit::CheckAllControlledUnits(Func const& func, uint32 controlledMask) const
{
    if (controlledMask & CONTROLLED_PET)
        if (Pet const* pet = GetPet())
            if (func(pet))
            {
                return true;
            }

    if (controlledMask & CONTROLLED_MINIPET)
        if (Pet const* mini = GetMiniPet())
            if (func(mini))
            {
                return true;
            }

    if (controlledMask & CONTROLLED_GUARDIANS)
    {
        for (GuidSet::const_iterator itr = m_guardianPets.begin(); itr != m_guardianPets.end();)
            if (Pet const* guardian = _GetPet(*(itr++)))
                if (func(guardian))
                {
                    return true;
                }
    }

    if (controlledMask & CONTROLLED_TOTEMS)
    {
        for (int i = 0; i < MAX_TOTEM_SLOT; ++i)
            if (Unit const* totem = _GetTotem(TotemSlot(i)))
                if (func(totem))
                {
                    return true;
                }
    }

    if (controlledMask & CONTROLLED_CHARM)
        if (Unit const* charm = GetCharm())
            if (func(charm))
            {
                return true;
            }

    return false;
}

/** @} */

#endif
