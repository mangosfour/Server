/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2026 MaNGOS <https://www.getmangos.eu>
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
 * Byte-exact tests for compact 5.4.8 server packet bodies recovered from the
 * client readers at 0x6F568B, 0xC8CBBE, 0x6D18F6, 0x94E111, 0xCCDD26,
 * and 0x6D9F28.
 */

#include "Player.h"
#include "InstanceData.h"
#include "Opcodes.h"
#include "WorldPacket.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// InstanceData is exported on Windows, so merely including its owning header emits
// its vtable in this standalone fixture. These two policy hooks are unrelated to the
// inline packet builder under test; provide inert fixture definitions instead of
// linking the complete game library (and its database process globals).
bool InstanceData::CheckAchievementCriteriaMeet(uint32, Player const*, Unit const*, uint32) const
{
    return false;
}

bool InstanceData::CheckConditionCriteriaMeet(Player const*, uint32, WorldObject const*, uint32) const
{
    return false;
}

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static bool BytesEqual(WorldPacket const& packet, std::vector<uint8_t> const& expected)
{
    if (packet.size() != expected.size())
    {
        std::fprintf(stderr, "  size %u, wanted %u\n", unsigned(packet.size()), unsigned(expected.size()));
        return false;
    }

    for (size_t i = 0; i < expected.size(); ++i)
    {
        if (packet.contents()[i] != expected[i])
        {
            std::fprintf(stderr, "  byte %u = 0x%02X, wanted 0x%02X\n",
                         unsigned(i), packet.contents()[i], expected[i]);
            return false;
        }
    }
    return true;
}

static WorldPacket InputPacket(uint32_t opcode, std::vector<uint8_t> const& body)
{
    WorldPacket packet(opcode, body.size());
    if (!body.empty())
    {
        packet.append(body.data(), body.size());
    }
    return packet;
}

static void test_pet_set_action_matches_retail_and_rejects_malformed_bodies()
{
    struct Fixture
    {
        std::vector<uint8_t> body;
        uint32_t position;
        uint32_t actionData;
        uint64_t guid;
    };

    Fixture const retail[] = {
        { { 0x05,0,0,0, 0x59,0x0A,0,0x81, 0xF7,0x62,0x40,0xF0,0x69,0x03,0x8F,0x95 }, 5, 0x81000A59, UINT64_C(0xF141638E68000294) },
        { { 0x04,0,0,0, 0x44,0xC2,0x01,0xC1, 0xF7,0x0C,0x43,0xF0,0x81,0x33,0x53,0x73 }, 4, 0xC101C244, UINT64_C(0xF1420D5280003272) },
        { { 0x04,0,0,0, 0x44,0xC2,0x01,0x81, 0x77,0x0C,0x43,0xF0,0x81,0x53,0x0C }, 4, 0x8101C244, UINT64_C(0xF1420D528000000D) },
        { { 0x05,0,0,0, 0x59,0x0A,0,0x81, 0xF7,0x62,0x40,0xF0,0x69,0x00,0x8F,0xCD }, 5, 0x81000A59, UINT64_C(0xF141638E680001CC) },
        { { 0x05,0,0,0, 0x59,0x0A,0,0xC1, 0xF7,0x62,0x40,0xF0,0x69,0x00,0x8F,0xCD }, 5, 0xC1000A59, UINT64_C(0xF141638E680001CC) },
        { { 0x05,0,0,0, 0x59,0x0A,0,0x81, 0xF7,0x62,0x40,0xF0,0x69,0x00,0x8F,0xCD }, 5, 0x81000A59, UINT64_C(0xF141638E680001CC) },
        { { 0x03,0,0,0, 0xBE,0x1E,0,0x81, 0xFF,0x0C,0x43,0xF0,0x81,0x03,0xA6,0x53,0x44 }, 3, 0x81001EBE, UINT64_C(0xF1420D528002A745) },
        { { 0x03,0,0,0, 0xBE,0x1E,0,0xC1, 0xFF,0x0C,0x43,0xF0,0x81,0x03,0xA6,0x53,0x44 }, 3, 0xC1001EBE, UINT64_C(0xF1420D528002A745) }
    };

    for (Fixture const& fixture : retail)
    {
        WorldPacket packet = InputPacket(CMSG_PET_SET_ACTION, fixture.body);
        uint32 position = 0xAAAAAAAA;
        uint32 actionData = 0xBBBBBBBB;
        ObjectGuid guid(UINT64_C(0xCCCCCCCCCCCCCCCC));
        CHECK(MopCompactPackets::ReadPetSetAction(packet, position, actionData, guid));
        CHECK(position == fixture.position);
        CHECK(actionData == fixture.actionData);
        CHECK(guid.GetRawValue() == fixture.guid);
        CHECK(packet.rpos() == packet.size());
    }

    std::vector<uint8_t> const dense = {
        0x09,0,0,0, 0x56,0x34,0x12,0xC1, 0xFF,
        0x64,0x77,0x86,0x42,0x33,0x20,0x55,0x11
    };
    {
        WorldPacket packet = InputPacket(CMSG_PET_SET_ACTION, dense);
        uint32 position = 0, actionData = 0;
        ObjectGuid guid;
        CHECK(MopCompactPackets::ReadPetSetAction(packet, position, actionData, guid));
        CHECK(position == 9);
        CHECK(actionData == 0xC1123456);
        CHECK(guid.GetRawValue() == UINT64_C(0x8776655443322110));
    }

    struct OneHot { uint8_t mask; uint8_t wire; uint64_t expected; };
    OneHot const oneHot[] = {
        { 0x80,0x20,UINT64_C(0x0000000000002100) },
        { 0x40,0x11,UINT64_C(0x0000000000000010) },
        { 0x20,0x64,UINT64_C(0x0000650000000000) },
        { 0x10,0x42,UINT64_C(0x0000000043000000) },
        { 0x08,0x33,UINT64_C(0x0000000000320000) },
        { 0x04,0x86,UINT64_C(0x8700000000000000) },
        { 0x02,0x77,UINT64_C(0x0076000000000000) },
        { 0x01,0x55,UINT64_C(0x0000005400000000) }
    };
    for (OneHot const& fixture : oneHot)
    {
        std::vector<uint8_t> body = { 0x0A,0,0,0, 0x59,0x0A,0,0x81, fixture.mask, fixture.wire };
        WorldPacket packet = InputPacket(CMSG_PET_SET_ACTION, body);
        uint32 position = 0, actionData = 0;
        ObjectGuid guid;
        CHECK(MopCompactPackets::ReadPetSetAction(packet, position, actionData, guid));
        CHECK(position == 10); // structurally valid; handler policy owns this bound
        CHECK(guid.GetRawValue() == fixture.expected);
    }

    std::vector<std::vector<uint8_t>> malformed;
    for (size_t length = 0; length < dense.size(); ++length)
        malformed.emplace_back(dense.begin(), dense.begin() + length);
    std::vector<uint8_t> trailing = dense;
    trailing.push_back(0x00);
    malformed.push_back(trailing);
    std::vector<uint8_t> doubled = dense;
    doubled.insert(doubled.end(), dense.begin(), dense.end());
    malformed.push_back(doubled);
    malformed.push_back({ 0,0,0,0, 0,0,0,0, 0x00 });
    for (size_t i = 9; i < dense.size(); ++i)
    {
        std::vector<uint8_t> nonCanonical = dense;
        nonCanonical[i] = 0x01;
        malformed.push_back(nonCanonical);
    }

    for (std::vector<uint8_t> const& body : malformed)
    {
        WorldPacket packet = InputPacket(CMSG_PET_SET_ACTION, body);
        uint32 position = 0xAAAAAAAA;
        uint32 actionData = 0xBBBBBBBB;
        ObjectGuid guid(UINT64_C(0xCCCCCCCCCCCCCCCC));
        CHECK(!MopCompactPackets::ReadPetSetAction(packet, position, actionData, guid));
        CHECK(packet.rpos() == packet.size());
        CHECK(position == 0xAAAAAAAA);
        CHECK(actionData == 0xBBBBBBBB);
        CHECK(guid.GetRawValue() == UINT64_C(0xCCCCCCCCCCCCCCCC));
    }
}


static void test_attack_packets()
{
    uint64_t const attacker = UINT64_C(0x0002030005060008);
    uint64_t const victim = UINT64_C(0x1100334400667700);

    WorldPacket start;
    MopCompactPackets::BuildAttackStart(start, attacker, victim);
    CHECK(start.GetOpcode() == SMSG_ATTACKSTART);
    CHECK(BytesEqual(start, {
        0xA9, 0xBE,
        0x02, 0x09, 0x32, 0x03, 0x76,
        0x45, 0x07, 0x10, 0x67, 0x04
    }));

    WorldPacket stop;
    MopCompactPackets::BuildAttackStop(stop, attacker, victim, true);
    CHECK(stop.GetOpcode() == SMSG_ATTACKSTOP);
    CHECK(BytesEqual(stop, {
        0xCE, 0xF6, 0x00,
        0x09, 0x04, 0x02, 0x07, 0x76,
        0x45, 0x03, 0x32, 0x10, 0x67
    }));

    WorldPacket rejected;
    MopCompactPackets::BuildAttackStop(rejected, attacker, victim, false);
    CHECK(BytesEqual(rejected, {
        0xCE, 0x76, 0x00,
        0x09, 0x04, 0x02, 0x07, 0x76,
        0x45, 0x03, 0x32, 0x10, 0x67
    }));

    uint8_t const swingBody[] = { 0x67, 0x10, 0x76, 0x67, 0x45, 0x32 };
    WorldPacket swing(CMSG_ATTACKSWING, sizeof(swingBody));
    swing.append(swingBody, sizeof(swingBody));
    CHECK(MopCompactPackets::ReadAttackSwingTarget(swing).GetRawValue() == victim);

    WorldPacket denseCancel;
    MopCompactPackets::BuildCancelAutoRepeat(
        denseCancel, UINT64_C(0x0807060504030201));
    CHECK(denseCancel.GetOpcode() == SMSG_CANCEL_AUTO_REPEAT);
    CHECK(BytesEqual(denseCancel, {
        0xFF,
        0x09, 0x06, 0x02, 0x07, 0x00, 0x04, 0x03, 0x05
    }));

    WorldPacket sparseCancel;
    MopCompactPackets::BuildCancelAutoRepeat(
        sparseCancel, UINT64_C(0x000000BB0000AA00));
    CHECK(BytesEqual(sparseCancel, { 0x90, 0xBA, 0xAB }));
}

static void test_attacker_state_update()
{
    MopCompactPackets::AttackStateUpdateData update;
    update.hitInfo = 0x00000200u;
    update.attacker = ObjectGuid(UINT64_C(0x0807060504030201));
    update.target = ObjectGuid(UINT64_C(0x100F0E0D0C0B0A09));
    update.damage = 1000;
    update.overkill = 100;
    update.schoolMask = 1;
    update.victimState = 1;

    WorldPacket normal;
    MopCompactPackets::BuildAttackerStateUpdate(normal, update);
    CHECK(normal.GetOpcode() == SMSG_ATTACKERSTATEUPDATE);
    CHECK(BytesEqual(normal, {
        0x00, 0x34, 0x00, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00,
        0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0xFF, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
        0xE8, 0x03, 0x00, 0x00,
        0x64, 0x00, 0x00, 0x00,
        0x01,
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x7A, 0x44,
        0xE8, 0x03, 0x00, 0x00,
        0x01,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    }));

    update = MopCompactPackets::AttackStateUpdateData();
    update.hitInfo = 0x000020A0u;
    update.damage = 50;
    update.schoolMask = 1;
    update.absorb = 10;
    update.resist = 5;
    update.victimState = 5;
    update.blocked = 3;

    WorldPacket mitigated;
    MopCompactPackets::BuildAttackerStateUpdate(mitigated, update);
    CHECK(BytesEqual(mitigated, {
        0x00, 0x34, 0x00, 0x00, 0x00,
        0xA0, 0x20, 0x00, 0x00,
        0x00, 0x00,
        0x32, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01,
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x48, 0x42,
        0x32, 0x00, 0x00, 0x00,
        0x0A, 0x00, 0x00, 0x00,
        0x05, 0x00, 0x00, 0x00,
        0x05,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    }));

    update = MopCompactPackets::AttackStateUpdateData();
    update.hitInfo = 0x00000001u;
    WorldPacket extended;
    MopCompactPackets::BuildAttackerStateUpdate(extended, update);
    std::vector<uint8_t> expectedExtended(97, 0);
    expectedExtended[1] = 0x5C;
    expectedExtended[5] = 0x01;
    expectedExtended[19] = 0x01;
    CHECK(BytesEqual(extended, expectedExtended));
}

/// SMSG_MOVE_SET_RUN_SPEED, recovered from the client reader sub_C8B928 and
/// pinned here against a REAL retail body rather than a synthetic one.
///
/// capture-000004 seq 579, build 18414, catalogue 2BE10C89. Decoding it under
/// the reader's sequence yields guid 0x04000000053CC8E8, counter 65, speed 7.7 --
/// and 0x0400 is the same high pair the creatures in that capture's name
/// queries carry, so the GUID is corroborated independently of this packet.
static void test_run_speed_matches_retail_body()
{
    // The captured speed is 0x40F66667, one ULP above what the literal 7.7f
    // compiles to (0x40F66666), so the exact bits are reconstructed here. These
    // bits are taken verbatim from the wire; the provenance of retail's own
    // arithmetic is not established -- 7.0f * 1.1f in float lands on ...67 while
    // the same product evaluated in double and narrowed lands on ...66, so the
    // capture is the authority rather than any reconstruction of it. Using the
    // literal fails on byte 9 alone, which is a fair demonstration that this
    // fixture is byte-exact and not merely shape-exact.
    float speed;
    uint32 const speedBits = 0x40F66667u;
    std::memcpy(&speed, &speedBits, sizeof(speed));

    WorldPacket packet(SMSG_MOVE_SET_RUN_SPEED, 17);
    MopCompactPackets::BuildMoveSetRunSpeed(packet, 0x04000000053CC8E8ull, 65u, speed);
    CHECK(BytesEqual(packet, {
        0xD5,                                           // mask, guid order 1,7,4,2,5,3,6,0
        0xC9,                                           // guid[1] ^ 1
        0x41, 0x00, 0x00, 0x00,                         // counter 65
        0x05, 0x04, 0xE9,                               // guid[7], guid[3], guid[0] ^ 1
        0x67, 0x66, 0xF6, 0x40,                         // 7.7f
        0x3D                                            // guid[2] ^ 1; 4,6,5 are zero
    }));
}

/// SMSG_MOVE_SET_WALK_SPEED, from client reader sub_C8F849, pinned against
/// capture-000004 seq 23263 (build 18414, catalogue 2BE10C89). The mover is the
/// same creature as the run-speed body above -- guid 0x04000000053CC8E8 -- which
/// cross-checks that these really are distinct per-opcode interleaves.

/// SMSG_SPLINE_MOVE_SET_RUN_SPEED, from client reader sub_C8C923, pinned against
/// capture-000004 seq 2506. The observer broadcast carries NO counter, only the
/// mover and the speed, which is what distinguishes it from every direct packet.
static void test_spline_run_speed_matches_retail_body()
{
    float speed;
    uint32 const speedBits = 0x409B3333u;                   // 4.85f
    std::memcpy(&speed, &speedBits, sizeof(speed));

    WorldPacket packet(SMSG_SPLINE_MOVE_SET_RUN_SPEED, 13);
    MopCompactPackets::BuildSplineMoveSetRunSpeed(packet, 0xF1308319002275D5ull, speed);
    CHECK(BytesEqual(packet, {
        0x7F,                                               // mask, guid order 3,0,1,4,7,5,6,2
        0x18,                                               // guid[4] ^ 1
        0x33, 0x33, 0x9B, 0x40,                             // 4.85f
        0x74, 0x82, 0xF0, 0x31, 0x23, 0xD4                  // guid[1,5,3,7,6,2,0] ^ 1, minus the absent one
    }));
}

/// SMSG_MOVE_SET_RUN_BACK_SPEED, reader sub_C8977A, pinned to capture-000004
/// seq 23260. Same mover as the run, walk and flight fixtures.

/// SMSG_MOVE_SET_FLIGHT_SPEED, reader sub_C8A820, pinned to capture-000004
/// seq 582. This one writes the float and counter BEFORE the mask byte, which no
/// sibling does -- the fixture exists mainly to pin that.

/// SMSG_MOVE_SET_SWIM_BACK_SPEED, reader sub_C8AF44. NOT a retail fixture --
/// this opcode has zero observations at 18414, so there is no captured body to
/// pin it against and it stays outside the send gate. This test is structural:
/// an all-nonzero GUID forces every byte to be emitted, which catches the defect
/// the old serializer had (it wrote guid byte 0 twice and byte 2 never) and pins
/// the counter and float being adjacent.

/// Reader-derived structural cases, NOT retail fixtures.
///
/// The retail bodies available for this family all come from one mover whose
/// GUID has three zero bytes, so those byte positions are never exercised. These
/// use an all-nonzero GUID so every position is emitted, which pins the full
/// interleave rather than the subset a sparse GUID happens to reach.
///
/// TURN_RATE, FLIGHT_BACK and PITCH_RATE have ZERO observations at 18414 and so
/// have no retail body at all; these are their only serializer tests, and all
/// three remain outside the send gate.

/// The four spline speed broadcasts, each pinned to a retail body. Sequences
/// 2510, 2507, 2508 and 2509 of capture-000004 are consecutive packets for one
/// mover, 0xF1308319002275D5, and every speed is exactly half its base for that
/// movement type -- one creature uniformly slowed, decoded under four different
/// interleaves.

/// Every spline speed builder under an all-nonzero GUID.
///
/// The retail bodies above all come from mover 0xF1308319002275D5, exactly ONE
/// of whose GUID bytes is zero -- byte 3 -- and so is never emitted. That is why
/// those bodies are 12 bytes: one mask, seven emitted bytes and the float. Two
/// reviews of the previous commit both caught this described as three; the
/// commit message for it says three and is wrong.
///
/// So the retail fixtures pin byte 3's mask position and the interleave of the
/// other seven, but cannot catch byte 3 omitted from or misplaced within the
/// byte interleave. A GUID with no zero byte forces all eight through every
/// builder and closes exactly that gap.
///
/// The mask is 0xFF in every case here, so this test pins no mask position at
/// all. Mask order is covered separately, below.
///
/// The last four have no observed body at all and are reader-derived only, so
/// this and the mask-order test are their whole coverage. They are admitted
/// nonetheless, on binary proof, as are their four direct counterparts.

/// Mask ORDER, which neither the retail fixtures nor the all-nonzero test pins.
///
/// A GUID with exactly one zero byte emits exactly one CLEAR mask bit, and the
/// position of that clear bit is where the byte sits in the mask order. Probing
/// all eight slots therefore recovers the whole permutation, so no transposition
/// survives. Both reviews of the previous commit raised this independently:
/// without it, any of the 8! = 40320 orders would satisfy the suite for the four
/// builders that have no retail body to constrain them, and promotion is one
/// case label away.

/// The same probe for the DIRECT speed builders, which carry a counter and so
/// need their own signature. Four of these were held back from the send gate
/// while their spline counterparts were admitted, which left observers told of a
/// speed change that the mover's own client never heard about. Pinning mask
/// order here is what makes closing that gap safe.



/// The interleave is what distinguishes run from swim: run writes one GUID byte
/// before the counter, swim writes none. Reusing the swim builder would produce
/// a body the client cannot parse, so pin that they differ.


static void test_random_roll_guid_layouts()
{
    {
        WorldPacket packet(SMSG_RANDOM_ROLL, 21);
        MopCompactPackets::BuildRandomRoll(packet, 0x0123456789ABCDEFull, 1, 100, 42);
        CHECK(BytesEqual(packet, {
            0x2A, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x64, 0x00, 0x00, 0x00,
            0xFF,
            0x44, 0x66, 0xAA, 0xEE, 0x88, 0xCC, 0x22, 0x00
        }));
    }
    {
        WorldPacket packet(SMSG_RANDOM_ROLL, 14);
        MopCompactPackets::BuildRandomRoll(packet, 0xFFull, 5, 9, 7);
        CHECK(BytesEqual(packet, {
            0x07, 0x00, 0x00, 0x00,
            0x05, 0x00, 0x00, 0x00,
            0x09, 0x00, 0x00, 0x00,
            0x80, 0xFE
        }));
    }
    {
        WorldPacket packet(SMSG_RANDOM_ROLL, 13);
        MopCompactPackets::BuildRandomRoll(packet, 0, 0, 0, 0);
        CHECK(BytesEqual(packet, {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00
        }));
    }
}

static void test_instance_encounter_variants()
{
    {
        WorldPacket packet(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, 5);
        CHECK(MopCompactPackets::BuildInstanceEncounter(packet, 0, 0, 0xA5, 0x5A));
        CHECK(BytesEqual(packet, { 0x00, 0x00, 0x00, 0x00, 0xA5 }));
    }
    {
        WorldPacket packet(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, 4);
        CHECK(MopCompactPackets::BuildInstanceEncounter(packet, 1, 0, 0xA5, 0x5A));
        CHECK(BytesEqual(packet, { 0x01, 0x00, 0x00, 0x00 }));
    }
    {
        WorldPacket packet(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, 14);
        CHECK(MopCompactPackets::BuildInstanceEncounter(packet, 2, 0x0123456789ABCDEFull, 0xA5, 0x5A));
        CHECK(BytesEqual(packet, {
            0x02, 0x00, 0x00, 0x00,
            0xFF, 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
            0xA5
        }));
    }
    {
        WorldPacket packet(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, 7);
        CHECK(MopCompactPackets::BuildInstanceEncounter(packet, 3, 0xFFull, 0xA5, 0x5A));
        CHECK(BytesEqual(packet, {
            0x03, 0x00, 0x00, 0x00,
            0x01, 0xFF,
            0xA5
        }));
    }
    {
        WorldPacket packet(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, 6);
        CHECK(MopCompactPackets::BuildInstanceEncounter(packet, 7, 0, 0xA5, 0x5A));
        CHECK(BytesEqual(packet, { 0x07, 0x00, 0x00, 0x00, 0xA5, 0x5A }));
    }

    for (uint32_t type : { 4u, 5u, 6u, 8u, 9u, 10u })
    {
        WorldPacket packet(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, 14);
        CHECK(MopCompactPackets::BuildInstanceEncounter(packet, type, 0xFFull, 0xA5, 0x5A));
        if (type == 4)
            CHECK(BytesEqual(packet, { 0x04, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xA5 }));
        else if (type == 5 || type == 6 || type == 8)
            CHECK(BytesEqual(packet, { uint8_t(type), 0x00, 0x00, 0x00, 0xA5 }));
        else
            CHECK(BytesEqual(packet, { uint8_t(type), 0x00, 0x00, 0x00 }));
    }

    WorldPacket invalid(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, 4);
    CHECK(!MopCompactPackets::BuildInstanceEncounter(invalid, 11, 0, 0, 0));
    CHECK(invalid.empty());
}

static void test_raid_difficulty()
{
    WorldPacket packet(SMSG_SET_RAID_DIFFICULTY, 4);
    MopCompactPackets::BuildSetRaidDifficulty(packet, 3);
    CHECK(BytesEqual(packet, { 0x03, 0x00, 0x00, 0x00 }));
}

static void test_dungeon_difficulty()
{
    WorldPacket packet(SMSG_SET_DUNGEON_DIFFICULTY, 4);
    MopCompactPackets::BuildSetDungeonDifficulty(packet, 2);
    CHECK(BytesEqual(packet, { 0x02, 0x00, 0x00, 0x00 }));
}

static void test_cancel_combat()
{
    WorldPacket packet(SMSG_CANCEL_COMBAT, 0);
    CHECK(packet.empty());
    CHECK(uint32_t(packet.GetOpcode()) == 0x0E8Bu);
}

// Decodes SMSG_PARTYKILLLOG the way the 18414 client does, so the assertions below can be about
// WHICH GUID ended up in which slot rather than about bytes the builder produced.
//
// The previous version of this test pinned exact bytes for one killer/victim pair. That locks the
// wire layout, but the expected bytes had themselves been generated from the builder, so it was
// asserting the builder against itself: it could not distinguish killer from victim and stayed
// green while the two were transposed. A combat log the wrong way round in live play on 18414 is
// what prompted the look; the roles themselves are settled from the client binary, cited in
// BuildPartyKillLog. No particular client string is being claimed here -- an earlier version of
// this comment quoted one, which was not substantiated. Decoding restores the property the byte
// fixture was missing.
//
// Both tables are read out of reader sub_6F2FE4 itself, NOT out of the builder -- otherwise this
// would be circular again and a transposition would still pass. Taking slot A as this+16..23 and
// slot B as this+24..31, the reader's mask order is
//   B7 B2 A1 B4 A2 A5 B3 B1 B0 A3 A0 A4 B6 A7 B5 A6
// and its byte order, from the sequence of "*(this + N) ^=" sites, is offsets
//   24 29 16 18 31 30 25 28 20 17 26 22 19 21 23 27
// which is
//   B0 B5 A0 A2 B7 B6 B1 B4 A4 A1 B2 A6 A3 A5 A7 B3
// Each present byte is XOR 1 on the wire.
static void DecodePartyKillLog(WorldPacket const& packet, uint64_t& slotA, uint64_t& slotB)
{
    static const int kMaskOrder[16][2] = {
        {1, 7}, {1, 2}, {0, 1}, {1, 4}, {0, 2}, {0, 5}, {1, 3}, {1, 1},
        {1, 0}, {0, 3}, {0, 0}, {0, 4}, {1, 6}, {0, 7}, {1, 5}, {0, 6},
    };
    static const int kByteOrder[16][2] = {
        {1, 0}, {1, 5}, {0, 0}, {0, 2}, {1, 7}, {1, 6}, {1, 1}, {1, 4},
        {0, 4}, {0, 1}, {1, 2}, {0, 6}, {0, 3}, {0, 5}, {0, 7}, {1, 3},
    };

    uint8_t bytes[8][2] = {{0}};
    bool present[8][2] = {{false}};

    uint8_t const* p = packet.contents();
    // 16 mask bits, MSB-first, in the reader's order
    for (int i = 0; i < 16; ++i)
    {
        uint8_t const bit = (p[i / 8] >> (7 - (i % 8))) & 1;
        present[kMaskOrder[i][1]][kMaskOrder[i][0]] = (bit != 0);
    }

    size_t at = 2;
    for (int i = 0; i < 16; ++i)
    {
        int const slot = kByteOrder[i][0];
        int const idx  = kByteOrder[i][1];
        if (present[idx][slot])
        {
            bytes[idx][slot] = uint8_t(p[at++] ^ 1);
        }
    }

    slotA = slotB = 0;
    for (int i = 0; i < 8; ++i)
    {
        slotA |= uint64_t(bytes[i][0]) << (8 * i);
        slotB |= uint64_t(bytes[i][1]) << (8 * i);
    }
}

static void test_party_kill_log()
{
    uint64_t const killerGuid = UINT64_C(0x8877665544332211);
    uint64_t const victimGuid = UINT64_C(0xFFEEDDCCBBAA9901);

    WorldPacket packet;
    MopCompactPackets::BuildPartyKillLog(packet, ObjectGuid(killerGuid), ObjectGuid(victimGuid));
    CHECK(packet.GetOpcode() == SMSG_PARTYKILLLOG);
    CHECK(packet.size() == 18);

    uint64_t slotA = 0;
    uint64_t slotB = 0;
    DecodePartyKillLog(packet, slotA, slotB);

    // The client shows slot B as the killer: the handler at .text:00841B83 routes slot B to
    // COMBAT_LOG_EVENT sourceGUID and slot A to destGUID -- see BuildPartyKillLog for the chain.
    // These two assertions are the whole point of the test: swapping the roles in the builder
    // fails them, whereas the old byte fixture could not tell.
    CHECK(slotB == killerGuid);
    CHECK(slotA == victimGuid);

    // Distinct GUIDs, so a builder that wrote one of them into both slots cannot pass.
    CHECK(slotA != slotB);

    // Sparse GUIDs, and the reason is specific: every byte of the two GUIDs above is nonzero, so
    // all 16 mask bits are 1 and the mask is FF FF whatever order it is written in. The case above
    // therefore constrains the BYTE order and the slot roles, but says nothing at all about
    // kMaskOrder -- any permutation of the mask writes passes it.
    //
    // These two have disjoint nonzero positions (killer at byte 0, 2, 5; victim at 1, 4, 6), so the
    // mask is 6 set bits in 16 specific places. Permute the mask order and presence lands on the
    // wrong byte of the wrong slot, and neither GUID reconstructs.
    {
        uint64_t const sparseKiller = UINT64_C(0x00005C0000FF00A1);   // bytes 0, 2, 5 present
        uint64_t const sparseVictim = UINT64_C(0x00B2003E00000C00);   // bytes 1, 4, 6 present

        WorldPacket sparse;
        MopCompactPackets::BuildPartyKillLog(sparse, ObjectGuid(sparseKiller), ObjectGuid(sparseVictim));
        CHECK(sparse.GetOpcode() == SMSG_PARTYKILLLOG);
        CHECK(sparse.size() == 8);                          // 2 mask bytes + 6 present bytes

        uint64_t sparseA = 0;
        uint64_t sparseB = 0;
        DecodePartyKillLog(sparse, sparseA, sparseB);
        CHECK(sparseB == sparseKiller);
        CHECK(sparseA == sparseVictim);
    }

    // The two cases above still do not pin kMaskOrder UNIQUELY. Two positions that are absent in
    // the sparse case and present in the dense one have identical presence in both, so exchanging
    // them in the mask order is invisible -- 60 such pairs exist.
    //
    // These four cases fix that by giving each of the 16 (slot, byte) positions its own 4-bit
    // presence SIGNATURE: a position's id is slot * 8 + byteIndex, and in case k it is present iff
    // bit k of that id is set. No two positions share a signature, so exchanging any two of them
    // changes the decode in at least one of the four -- presence lands on the wrong position, the
    // absent side reconstructs as zero, and the GUID no longer matches.
    //
    // Byte values are 0x10 + id, so every present byte is nonzero (required, or the builder would
    // treat it as absent) and distinct, which keeps kByteOrder constrained at the same time.
    for (int k = 0; k < 4; ++k)
    {
        uint64_t killerBits = 0;
        uint64_t victimBits = 0;
        size_t   present    = 0;

        for (int i = 0; i < 8; ++i)
        {
            if (((8 + i) >> k) & 1)                         // killer occupies ids 8..15
            {
                killerBits |= uint64_t(0x18 + i) << (8 * i);
                ++present;
            }

            if ((i >> k) & 1)                               // victim occupies ids 0..7
            {
                victimBits |= uint64_t(0x10 + i) << (8 * i);
                ++present;
            }
        }

        WorldPacket signature;
        MopCompactPackets::BuildPartyKillLog(signature, ObjectGuid(killerBits), ObjectGuid(victimBits));
        CHECK(signature.GetOpcode() == SMSG_PARTYKILLLOG);
        CHECK(signature.size() == 2 + present);

        uint64_t sigA = 0;
        uint64_t sigB = 0;
        DecodePartyKillLog(signature, sigA, sigB);
        CHECK(sigB == killerBits);
        CHECK(sigA == victimBits);
    }
}

static void test_duel_state_packets()
{
    WorldPacket outOfBounds;
    MopDuelPackets::BuildOutOfBounds(outOfBounds);
    CHECK(outOfBounds.GetOpcode() == SMSG_DUEL_OUTOFBOUNDS);
    CHECK(outOfBounds.empty());

    WorldPacket inBounds;
    MopDuelPackets::BuildInBounds(inBounds);
    CHECK(inBounds.GetOpcode() == SMSG_DUEL_INBOUNDS);
    CHECK(inBounds.empty());

    WorldPacket completed;
    MopDuelPackets::BuildComplete(completed, true);
    CHECK(completed.GetOpcode() == SMSG_DUEL_COMPLETE);
    CHECK(BytesEqual(completed, { 0x80 }));

    WorldPacket interrupted;
    MopDuelPackets::BuildComplete(interrupted, false);
    CHECK(BytesEqual(interrupted, { 0x00 }));

    WorldPacket countdown;
    MopDuelPackets::BuildCountdown(countdown, 0x12345678u);
    CHECK(countdown.GetOpcode() == SMSG_DUEL_COUNTDOWN);
    CHECK(BytesEqual(countdown, { 0x78, 0x56, 0x34, 0x12 }));
}

static void test_duel_request_and_winner_packets()
{
    WorldPacket requested;
    MopDuelPackets::BuildRequested(
        requested,
        ObjectGuid(UINT64_C(0x0807060504030201)),
        ObjectGuid(UINT64_C(0x100F0E0D0C0B0A09)));
    CHECK(requested.GetOpcode() == SMSG_DUEL_REQUESTED);
    CHECK(BytesEqual(requested, {
        0xFF, 0xFF,
        0x07, 0x05, 0x11, 0x0C,
        0x09, 0x0D, 0x0E, 0x08,
        0x04, 0x0A, 0x0B, 0x00,
        0x02, 0x06, 0x03, 0x0F,
    }));

    WorldPacket winner;
    CHECK(MopDuelPackets::BuildWinner(
        winner, false, "Winner", 0x10203040u, "Loser", 0xA1B2C3D4u));
    CHECK(winner.GetOpcode() == SMSG_DUEL_WINNER);
    CHECK(BytesEqual(winner, {
        0x0C, 0x28,
        0xD4, 0xC3, 0xB2, 0xA1,
        'W', 'i', 'n', 'n', 'e', 'r',
        0x40, 0x30, 0x20, 0x10,
        'L', 'o', 's', 'e', 'r',
    }));

    WorldPacket retreat;
    CHECK(MopDuelPackets::BuildWinner(
        retreat, true, "Winner", 0x10203040u, "Loser", 0xA1B2C3D4u));
    CHECK(retreat[0] == 0x8C);

    WorldPacket maximum;
    CHECK(MopDuelPackets::BuildWinner(
        maximum, false, std::string(63, 'W'), 1, std::string(63, 'L'), 2));
    WorldPacket tooLong;
    CHECK(!MopDuelPackets::BuildWinner(
        tooLong, false, std::string(64, 'W'), 1, "L", 2));
    CHECK(tooLong.empty());
}

static void test_mirror_timer_packets()
{
    // capture-000004 / sequence 10281: a full breath timer at 180 seconds.
    // This protects the client-visible type/current ordering that drives
    // MIRROR_TIMER_START and MirrorTimerColors.
    WorldPacket started;
    MopMirrorTimerPackets::BuildStart(
        started, 1, 180000, 180000, -1, 0, false);
    CHECK(started.GetOpcode() == SMSG_START_MIRROR_TIMER);
    CHECK(BytesEqual(started, {
        0x20, 0xBF, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x20, 0xBF, 0x02, 0x00,
        0x00,
    }));

    WorldPacket stopped;
    MopMirrorTimerPackets::BuildStop(stopped, 0x12345678u);
    CHECK(stopped.GetOpcode() == SMSG_STOP_MIRROR_TIMER);
    CHECK(BytesEqual(stopped, { 0x78, 0x56, 0x34, 0x12 }));
}

static void test_rune_packets()
{
    std::array<MopRunePackets::RuneState, MAX_RUNES> const runes = {{
        { RUNE_BLOOD, 0x10 },
        { RUNE_BLOOD, 0x20 },
        { RUNE_UNHOLY, 0x30 },
        { RUNE_UNHOLY, 0x40 },
        { RUNE_FROST, 0x50 },
        { RUNE_DEATH, 0x60 },
    }};

    WorldPacket resync;
    MopRunePackets::BuildResync(resync, runes);
    CHECK(resync.GetOpcode() == SMSG_RESYNC_RUNES);
    CHECK(BytesEqual(resync, {
        0x00, 0x00, 0x0C,
        0x10, 0x00, 0x20, 0x00,
        0x30, 0x01, 0x40, 0x01,
        0x50, 0x02, 0x60, 0x03,
    }));

    WorldPacket power;
    MopRunePackets::BuildAddPower(power, 0x20u);
    CHECK(power.GetOpcode() == SMSG_ADD_RUNE_POWER);
    CHECK(BytesEqual(power, { 0x20, 0x00, 0x00, 0x00 }));

    WorldPacket converted;
    MopRunePackets::BuildConvert(converted, RUNE_DEATH, 4);
    CHECK(converted.GetOpcode() == SMSG_CONVERT_RUNE);
    CHECK(BytesEqual(converted, { 0x03, 0x04 }));
}

static void test_threat_packets()
{
    ObjectGuid const owner(UINT64_C(0x0807060504030201));
    ObjectGuid const selected(UINT64_C(0x100F0E0D0C0B0A09));
    MopThreatPackets::ThreatEntries const entries = {{
        ObjectGuid(UINT64_C(0x1817161514131211)), 0xA1B2C3D4u
    }};

    WorldPacket update;
    MopThreatPackets::BuildUpdate(update, owner, entries);
    CHECK(update.GetOpcode() == SMSG_THREAT_UPDATE);
    CHECK(BytesEqual(update, {
        0xFE, 0x00, 0x00, 0x1F, 0xF8,
        0x16, 0x19, 0x10, 0x13, 0x12, 0x17, 0x15, 0x14,
        0xD4, 0xC3, 0xB2, 0xA1,
        0x03, 0x04, 0x02, 0x05, 0x07, 0x06, 0x00, 0x09,
    }));

    WorldPacket highest;
    MopThreatPackets::BuildHighest(highest, owner, selected, entries);
    CHECK(highest.GetOpcode() == SMSG_HIGHEST_THREAT_UPDATE);
    CHECK(BytesEqual(highest, {
        0xFF, 0xF8, 0x00, 0x00, 0x7F, 0xF8,
        0x04, 0x16, 0xD4, 0xC3, 0xB2, 0xA1,
        0x14, 0x10, 0x15, 0x17, 0x12, 0x13, 0x19,
        0x0D, 0x07, 0x0A, 0x03, 0x00, 0x02, 0x0E, 0x0B,
        0x09, 0x08, 0x0C, 0x11, 0x05, 0x06, 0x0F,
    }));

    WorldPacket clear;
    MopThreatPackets::BuildClear(clear, owner);
    CHECK(clear.GetOpcode() == SMSG_THREAT_CLEAR);
    CHECK(BytesEqual(clear, {
        0xFF, 0x09, 0x00, 0x04, 0x05, 0x02, 0x03, 0x06, 0x07,
    }));

    WorldPacket remove;
    MopThreatPackets::BuildRemove(remove, owner, selected);
    CHECK(remove.GetOpcode() == SMSG_THREAT_REMOVE);
    CHECK(BytesEqual(remove, {
        0xFF, 0xFF, 0x0D, 0x08, 0x0A, 0x07, 0x04, 0x09, 0x05,
        0x00, 0x0C, 0x03, 0x0B, 0x06, 0x11, 0x0E, 0x02, 0x0F,
    }));
}

static void test_dismount_packet()
{
    WorldPacket packet;
    MopCompactPackets::BuildDismount(
        packet, ObjectGuid(UINT64_C(0x0807060504030201)));
    CHECK(packet.GetOpcode() == SMSG_DISMOUNT);
    CHECK(BytesEqual(packet, {
        0xFF, 0x05, 0x06, 0x09, 0x07, 0x03, 0x04, 0x02, 0x00,
    }));
}

static void test_show_bank_matches_retail_bodies()
{
    // Two retained 18414 bodies independently pin the full-mask and sparse
    // forms. Every present GUID byte is XOR-obfuscated by WriteGuidBytes.
    WorldPacket dense;
    MopCompactPackets::BuildShowBank(
        dense, ObjectGuid(UINT64_C(0xF130F9DF002B6ED1)));
    CHECK(dense.GetOpcode() == SMSG_SHOW_BANK);
    CHECK(BytesEqual(dense, {
        0xDF, 0xF0, 0xD0, 0xF8, 0x31, 0x6F, 0xDE, 0x2A,
    }));

    WorldPacket sparse;
    MopCompactPackets::BuildShowBank(
        sparse, ObjectGuid(UINT64_C(0xF130F9E2000021A2)));
    CHECK(sparse.GetOpcode() == SMSG_SHOW_BANK);
    CHECK(BytesEqual(sparse, {
        0x5F, 0xF0, 0xA3, 0xF8, 0x31, 0x20, 0xE3,
    }));
}

static void test_guild_banker_activate_retail_bodies()
{
    struct Fixture
    {
        std::vector<uint8_t> body;
        uint64_t guid;
    };

    // Build-filtered 18414 corpus bodies from catalogue generation
    // 2BE10C899585BAECD237705AC13BBF9262D81B6BDC085B462808C6869CE88752.
    std::vector<Fixture> const retailFixtures = {
        { { 0x7D, 0x80, 0xF0, 0x06, 0x11, 0x12, 0x70, 0x43 }, UINT64_C(0xF113427100000710) },
        { { 0x7D, 0x80, 0xF0, 0x05, 0x1F, 0x12, 0x0B, 0x26 }, UINT64_C(0xF113270A0000041E) },
        { { 0x7F, 0x80, 0xF0, 0x62, 0x51, 0x12, 0x0B, 0x00, 0x26 }, UINT64_C(0xF113270A00016350) },
        { { 0x7F, 0x80, 0xF0, 0xD6, 0x74, 0x12, 0x0B, 0x12, 0x26 }, UINT64_C(0xF113270A0013D775) },
        { { 0x7D, 0x80, 0xF0, 0x07, 0xE7, 0x12, 0x71, 0x43 }, UINT64_C(0xF1134270000006E6) },
        { { 0x7D, 0x80, 0xF0, 0x07, 0x19, 0x12, 0xDB, 0x45 }, UINT64_C(0xF11344DA00000618) }
    };

    for (Fixture const& fixture : retailFixtures)
    {
        WorldPacket packet = InputPacket(CMSG_GUILD_BANKER_ACTIVATE, fixture.body);
        ObjectGuid guid(UINT64_C(0xFFFFFFFFFFFFFFFF));
        bool fullSlotRefresh = false;
        CHECK(MopCompactPackets::ReadGuildBankerActivate(packet, guid, fullSlotRefresh));
        CHECK(guid.GetRawValue() == fixture.guid);
        CHECK(fullSlotRefresh);
        CHECK(packet.rpos() == packet.size());
    }
}


static void test_guild_banker_activate_rejects_malformed_bodies()
{
    std::vector<uint8_t> const allPresent =
        { 0xFF, 0x80, 0x09, 0x03, 0x00, 0x06, 0x04, 0x02, 0x07, 0x05 };
    std::vector<std::vector<uint8_t>> malformed;

    // Covers zero- and one-byte mask truncation plus every missing-present-byte
    // boundary of the dense GUID body.
    for (size_t size = 0; size < allPresent.size(); ++size)
    {
        malformed.emplace_back(allPresent.begin(), allPresent.begin() + size);
    }
    malformed.push_back({ 0x7D, 0x80, 0xF0, 0x06, 0x11, 0x12, 0x70, 0x43, 0x00 }); // trailing scalar/byte
    malformed.push_back({ 0x00, 0x01 }); // non-zero low-seven padding bits
    malformed.push_back({ 0x20, 0x00, 0x01 }); // present wire byte XOR-decodes to zero
    malformed.push_back({ 0x40, 0x00 }); // all-zero GUID with the refresh bit set

    for (std::vector<uint8_t> const& body : malformed)
    {
        WorldPacket packet = InputPacket(CMSG_GUILD_BANKER_ACTIVATE, body);
        ObjectGuid guid(UINT64_C(0xFFFFFFFFFFFFFFFF));
        bool fullSlotRefresh = true;
        CHECK(!MopCompactPackets::ReadGuildBankerActivate(packet, guid, fullSlotRefresh));
        CHECK(packet.rpos() == packet.size());
        CHECK(guid.GetRawValue() == UINT64_C(0xFFFFFFFFFFFFFFFF));
        CHECK(fullSlotRefresh);
    }
}

static void test_combo_points_packet()
{
    WorldPacket packet;
    MopComboPointPackets::BuildUpdate(
        packet, ObjectGuid(UINT64_C(0x0807060504030201)), 5);
    CHECK(packet.GetOpcode() == SMSG_UPDATE_COMBO_POINTS);
    CHECK(BytesEqual(packet, {
        0xFF, 0x07, 0x06, 0x04, 0x09, 0x05, 0x00, 0x05, 0x02, 0x03,
    }));
}

static void test_pre_resurrect_packet()
{
    WorldPacket packet;
    MopCompactPackets::BuildPreResurrect(
        packet, ObjectGuid(UINT64_C(0x0807060504030201)));
    CHECK(packet.GetOpcode() == SMSG_PRE_RESURRECT);
    CHECK(BytesEqual(packet, {
        0xFF, 0x07, 0x03, 0x09, 0x00, 0x06, 0x04, 0x02, 0x05,
    }));

    // A zero byte clears its mask bit and is omitted entirely, so the body
    // shortens. Guards against writing a fixed nine-byte body.
    WorldPacket sparse;
    MopCompactPackets::BuildPreResurrect(
        sparse, ObjectGuid(UINT64_C(0x0000060000030001)));
    CHECK(BytesEqual(sparse, {
        0x34, 0x07, 0x00, 0x02,
    }));
}

/// CMSG_PET_ACTION, pinned to real 18414 bodies.
///
/// Eight bodies at catalogue 2BE10C89 across FIVE DISTINCT PRESENCE MASKS:
/// 0x7904, 0x7DAE, 0x7DAF, 0xFEBF and 0xFFBF. The mask is what matters, not the
/// length. Bodies sharing a mask reproduce the reader instead of proving it,
/// because bits that are always present together can be permuted without
/// changing any decode. Two reviews of the first version of this test made that
/// point independently, and they were right: it had only two distinct masks.
///
/// 0x7DAE and 0x7DAF are the strongest pair here. They differ in exactly ONE
/// mask bit, for the same pet, and the targets that fall out differ accordingly
/// -- 0xF1311C18000000D4 against 0xF1311C180000012F. That pins the position of
/// that single bit directly, which no amount of same-mask evidence can.
///
/// Each body must consume EXACTLY. Because the length is 18 + popcount of the
/// sixteen presence bits, exact consumption is a real constraint, not a
/// tautology.
///
/// The decoded GUIDs are then checked against a fact outside the packet: 0xF14
/// is HIGHGUID_PET, 0xF13 HIGHGUID_UNIT and 0xF15 HIGHGUID_VEHICLE.
///
/// WHAT THIS STILL DOES NOT PIN. Bits that are present together in every one of
/// these bodies remain mutually permutable, so the interleave is constrained but
/// NOT unique. A review enumerated the residue: three free classes and 86,400
/// equivalent orders, of which only three bit positions are actually pinned --
/// pet[3] and target[3], which are never present, and target[1], by the
/// 0x7DAE/0x7DAF pair. Only the client's own writer can close the rest, and the
/// consequence of a wrong choice inside a free class is not a desync but a
/// silently wrong GUID whenever a real one carries a zero byte in a free slot.
static void CheckPetAction(char const* what, uint8_t const* body, size_t length,
    uint32 expectedAction, uint64 expectedPet, uint64 expectedTarget)
{
    WorldPacket packet(CMSG_PET_ACTION, uint32(length));
    packet.append(body, length);

    uint32 action = 0;
    float posY = 0.0f, posZ = 0.0f, posX = 0.0f;
    ObjectGuid pet;
    ObjectGuid target;
    bool const parsed = MopCompactPackets::ReadPetAction(
        packet, action, posY, posZ, posX, pet, target);

    if (!parsed || action != expectedAction || pet.GetRawValue() != expectedPet ||
        target.GetRawValue() != expectedTarget || packet.rpos() != packet.size())
    {
        std::fprintf(stderr,
            "FAIL %s: action 0x%08X pet 0x%016llX target 0x%016llX consumed %u/%u\n",
            what, action, (unsigned long long)pet.GetRawValue(),
            (unsigned long long)target.GetRawValue(),
            unsigned(packet.rpos()), unsigned(packet.size()));
        ++g_fail;
    }
}

static void CheckPetActionReject(char const* what, std::vector<uint8_t> const& body)
{
    WorldPacket packet(CMSG_PET_ACTION, uint32(body.size()));
    if (!body.empty())
        packet.append(body.data(), body.size());

    uint32 action = 0xA5A5A5A5u;
    float posY = 11.25f, posZ = 22.5f, posX = 33.75f;
    ObjectGuid pet(UINT64_C(0x1122334455667788));
    ObjectGuid target(UINT64_C(0x8877665544332211));

    bool const parsed = MopCompactPackets::ReadPetAction(
        packet, action, posY, posZ, posX, pet, target);
    if (parsed || packet.rpos() != packet.size() || action != 0xA5A5A5A5u ||
        posY != 11.25f || posZ != 22.5f || posX != 33.75f ||
        pet.GetRawValue() != UINT64_C(0x1122334455667788) ||
        target.GetRawValue() != UINT64_C(0x8877665544332211))
    {
        std::fprintf(stderr,
            "FAIL %s: parsed %u consumed %u/%u action 0x%08X pet 0x%016llX target 0x%016llX\n",
            what, unsigned(parsed), unsigned(packet.rpos()), unsigned(packet.size()),
            action, (unsigned long long)pet.GetRawValue(),
            (unsigned long long)target.GetRawValue());
        ++g_fail;
    }
}

static void test_pet_action_matches_retail_bodies()
{
    // Command actions carry no target. UNIT_ACTION_BUTTON_TYPE 0x07 is ACT_COMMAND.
    // Action 3 is COMMAND_ABANDON, not COMMAND_STAY -- this was mislabelled when
    // the fixture landed. The distinction matters: for a hunter pet the abandon
    // path unsummons with PET_SAVE_AS_DELETED, which is permanent.
    uint8_t const abandon[] = {
        0x03, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x79, 0x04, 0xF0, 0x43, 0x0D, 0x9A, 0x29, 0x02
    };
    CheckPetAction("pet action abandon", abandon, sizeof(abandon),
        0x07000003u, UINT64_C(0xF1420C9B28000003), 0);

    uint8_t const follow[] = {
        0x01, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x79, 0x04, 0xF0, 0x43, 0x0D, 0x9B, 0xB8, 0x5E
    };
    CheckPetAction("pet action follow", follow, sizeof(follow),
        0x07000001u, UINT64_C(0xF1420C9AB900005F), 0);

    // The only target-bearing shape sampled: fifteen of the sixteen bits set.
    uint8_t const attack[] = {
        0x02, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0xBF, 0xF0, 0x40, 0x76, 0x03, 0x73, 0x8C, 0xEF, 0x30, 0x3C, 0xA0,
        0xF0, 0x05, 0x31, 0xAF, 0x28
    };
    CheckPetAction("pet action attack", attack, sizeof(attack),
        0x07000002u, UINT64_C(0xF141728D31027729), UINT64_C(0xF130EE0400AEA13D));

    // The only body of this build carrying a non-zero position. It is what
    // identifies the middle slot as z, by coordinate BAND rather than by exact
    // match: the movement bodies alongside it span x 1383..1501, y 548..775 and
    // z 246.835..246.866, and this tuple is (772.5943, 246.8356, 1473.7810). An
    // earlier comment here claimed every neighbour reported 246.8356 exactly;
    // that was wrong, they vary in the fourth decimal and one by more.
    //
    // The same bands place the first value in the y range and the third in the x
    // range, so y, z, x is well supported. It is not proven, since no body ties
    // the tuple to a known actor, and the server does not consume the position.
    uint8_t const moveTo[] = {
        0x04, 0x00, 0x00, 0x07,
        0x09, 0x26, 0x41, 0x44,
        0xEA, 0xD5, 0x76, 0x43,
        0xFE, 0x38, 0xB8, 0x44,
        0x79, 0x04, 0xF0, 0x43, 0x0D, 0x9B, 0xB8, 0x02
    };
    CheckPetAction("pet action move to", moveTo, sizeof(moveTo),
        0x07000004u, UINT64_C(0xF1420C9AB9000003), 0);

    // Masks 0x7DAE and 0x7DAF: identical but for one bit, same pet, targets that
    // differ only in the byte that bit admits. This is the pair that pins the
    // bit order rather than merely reproducing it.
    uint8_t const targetBitClear[] = {
        0x02, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x7D, 0xAE, 0xF0, 0x43, 0x0D, 0x9B, 0x1D, 0xB8, 0xD5, 0xF0, 0x19, 0x30,
        0x00
    };
    CheckPetAction("pet action target bit clear", targetBitClear, sizeof(targetBitClear),
        0x07000002u, UINT64_C(0xF1420C9AB9000001), UINT64_C(0xF1311C18000000D4));

    uint8_t const targetBitSet[] = {
        0x02, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x7D, 0xAF, 0xF0, 0x43, 0x0D, 0x9B, 0x1D, 0xB8, 0x2E, 0x00, 0xF0, 0x19,
        0x30, 0x00
    };
    CheckPetAction("pet action target bit set", targetBitSet, sizeof(targetBitSet),
        0x07000002u, UINT64_C(0xF1420C9AB9000001), UINT64_C(0xF1311C180000012F));

    // Mask 0xFEBF, and the mover is a VEHICLE (0xF15), not a pet. These bodies
    // are why HandlePetAction guards its Pet downcasts: ordinary traffic drives
    // a non-pet through the command path.
    uint8_t const vehicleMover[] = {
        0x02, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFE, 0xBF, 0xF0, 0x51, 0x1E, 0x22, 0x83, 0x24, 0x82, 0xD4, 0x74, 0xF0,
        0x18, 0x31, 0x23, 0xD6
    };
    CheckPetAction("pet action vehicle mover", vehicleMover, sizeof(vehicleMover),
        0x07000002u, UINT64_C(0xF150822500231FD7), UINT64_C(0xF1308319002275D5));

    uint8_t const vehicleMoverTwo[] = {
        0x02, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFE, 0xBF, 0xF0, 0x51, 0x68, 0x44, 0x83, 0x27, 0x82, 0x62, 0xB8, 0xF0,
        0x16, 0x31, 0x41, 0xC2
    };
    CheckPetAction("pet action vehicle mover two", vehicleMoverTwo, sizeof(vehicleMoverTwo),
        0x07000002u, UINT64_C(0xF1508226004569C3), UINT64_C(0xF13083170040B963));

    std::vector<uint8_t> const valid(abandon, abandon + sizeof(abandon));
    for (size_t length = 0; length < valid.size(); ++length)
    {
        CheckPetActionReject("pet action truncated",
            std::vector<uint8_t>(valid.begin(), valid.begin() + length));
    }

    std::vector<uint8_t> trailing = valid;
    trailing.push_back(0x00);
    CheckPetActionReject("pet action trailing byte", trailing);

    std::vector<uint8_t> doubled = valid;
    doubled.insert(doubled.end(), valid.begin(), valid.end());
    CheckPetActionReject("pet action doubled body", doubled);

    CheckPetActionReject("pet action empty pet guid",
        { 0x02, 0x00, 0x00, 0x07,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00,
          0x00, 0x00 });

    std::vector<uint8_t> nonCanonical = valid;
    nonCanonical[18] = 0x01;
    CheckPetActionReject("pet action non-canonical guid byte", nonCanonical);

    // Pin the position itself, so a reordering of the three reads is caught.
    WorldPacket packet(CMSG_PET_ACTION, uint32(sizeof(moveTo)));
    packet.append(moveTo, sizeof(moveTo));

    uint32 action = 0;
    float posY = 0.0f, posZ = 0.0f, posX = 0.0f;
    ObjectGuid pet;
    ObjectGuid target;
    CHECK(MopCompactPackets::ReadPetAction(
        packet, action, posY, posZ, posX, pet, target));

    float expectedY, expectedZ, expectedX;
    uint32 const bitsY = 0x44412609u;
    uint32 const bitsZ = 0x4376D5EAu;
    uint32 const bitsX = 0x44B838FEu;
    std::memcpy(&expectedY, &bitsY, sizeof(expectedY));
    std::memcpy(&expectedZ, &bitsZ, sizeof(expectedZ));
    std::memcpy(&expectedX, &bitsX, sizeof(expectedX));
    CHECK(posY == expectedY);
    CHECK(posZ == expectedZ);
    CHECK(posX == expectedX);
}

static void CheckPetStopAttack(char const* what,
    std::vector<uint8_t> const& body, bool expectedAccepted,
    uint64 expectedGuid)
{
    WorldPacket packet = InputPacket(CMSG_PET_STOP_ATTACK, body);
    ObjectGuid guid(UINT64_C(0x0807060504030201));
    bool const accepted = MopCompactPackets::ReadPetStopAttack(packet, guid);

    if (accepted != expectedAccepted || guid.GetRawValue() != expectedGuid ||
        packet.rpos() != packet.size())
    {
        std::fprintf(stderr,
            "FAIL %s: accepted %u wanted %u guid 0x%016llX wanted "
            "0x%016llX consumed %u/%u\n",
            what, unsigned(accepted), unsigned(expectedAccepted),
            (unsigned long long)guid.GetRawValue(),
            (unsigned long long)expectedGuid,
            unsigned(packet.rpos()), unsigned(packet.size()));
        ++g_fail;
    }
}

static void test_pet_stop_attack_matches_retail_and_rejects_malformed_bodies()
{
    // Captured retail bodies from catalogue generation 2BE10C89.
    CheckPetStopAttack("retail vehicle F1506C6B00002F07",
        { 0xFA, 0x6D, 0x06, 0x6A, 0x2E, 0xF0, 0x51 }, true,
        UINT64_C(0xF1506C6B00002F07));
    CheckPetStopAttack("retail vehicle F1506A7A00A3DB5C",
        { 0xFE, 0xA2, 0x6B, 0x5D, 0x7B, 0xDA, 0xF0, 0x51 }, true,
        UINT64_C(0xF1506A7A00A3DB5C));

    // Binary-derived synthetic fixtures for 0xF150010203040506. The first is
    // dense; each following body clears exactly one distinct raw GUID byte.
    CheckPetStopAttack("binary-derived dense",
        { 0xFF, 0x05, 0x00, 0x07, 0x03, 0x04, 0xF0, 0x51, 0x02 }, true,
        UINT64_C(0xF150010203040506));
    CheckPetStopAttack("binary-derived zero byte 0",
        { 0xF7, 0x05, 0x00, 0x03, 0x04, 0xF0, 0x51, 0x02 }, true,
        UINT64_C(0xF150010203040500));
    CheckPetStopAttack("binary-derived zero byte 1",
        { 0xDF, 0x05, 0x00, 0x07, 0x03, 0xF0, 0x51, 0x02 }, true,
        UINT64_C(0xF150010203040006));
    CheckPetStopAttack("binary-derived zero byte 2",
        { 0xFB, 0x00, 0x07, 0x03, 0x04, 0xF0, 0x51, 0x02 }, true,
        UINT64_C(0xF150010203000506));
    CheckPetStopAttack("binary-derived zero byte 3",
        { 0xFE, 0x05, 0x00, 0x07, 0x03, 0x04, 0xF0, 0x51 }, true,
        UINT64_C(0xF150010200040506));
    CheckPetStopAttack("binary-derived zero byte 4",
        { 0xFD, 0x05, 0x00, 0x07, 0x04, 0xF0, 0x51, 0x02 }, true,
        UINT64_C(0xF150010003040506));
    CheckPetStopAttack("binary-derived zero byte 5",
        { 0xBF, 0x05, 0x07, 0x03, 0x04, 0xF0, 0x51, 0x02 }, true,
        UINT64_C(0xF150000203040506));
    CheckPetStopAttack("binary-derived zero byte 6",
        { 0xEF, 0x05, 0x00, 0x07, 0x03, 0x04, 0xF0, 0x02 }, true,
        UINT64_C(0xF100010203040506));
    CheckPetStopAttack("binary-derived zero byte 7",
        { 0x7F, 0x05, 0x00, 0x07, 0x03, 0x04, 0x51, 0x02 }, true,
        UINT64_C(0x0050010203040506));

    CheckPetStopAttack("empty body", {}, false, 0);
    CheckPetStopAttack("all-zero GUID", { 0x00 }, false, 0);
    CheckPetStopAttack("truncated dense body",
        { 0xFF, 0x05, 0x00, 0x07 }, false, 0);
    CheckPetStopAttack("retail body with tail",
        { 0xFA, 0x6D, 0x06, 0x6A, 0x2E, 0xF0, 0x51, 0xAA }, false, 0);
    CheckPetStopAttack("non-canonical zero GUID byte",
        { 0x80, 0x01 }, false, 0);
    CheckPetStopAttack("non-canonical byte in nonzero GUID",
        { 0xC0, 0x02, 0x01 }, false, 0);
}

/// CMSG_PET_NAME_QUERY and its response, pinned to real 18414 bodies.
///
/// The request carries sixteen presence bits interleaved across the pet GUID and
/// the pet number. Four bodies across three distinct masks -- 0xB7DA, 0xB79A and
/// 0xB7DE.
///
/// That is WEAKER than it looks and an earlier version of this comment overstated
/// it. Because a pet number is small, its top four bytes are absent in every
/// body, and ten further slots are present in all of them, so a review counted
/// 87,091,200 equivalent bit orders. Only number[3] and pet[2] are pinned. These
/// fixtures prove the reader consumes real bodies exactly and recovers the right
/// values for them; they do not prove the interleave.
///
/// The response is tied to the request by evidence rather than by assumption:
/// the "Blue" body below is the actual reply to the first request body, three
/// packets later in the same capture, and the pet number it echoes is the one
/// that falls out of that request. Decoding the two independently and finding
/// the same number is what confirms the pair.
static void test_pet_name_query_matches_retail_bodies()
{
    struct Request
    {
        char const* what;
        uint8_t body[16];
        size_t length;
        uint64 guid;
        uint64 number;
    };

    Request const requests[] = {
        { "pet name query mask B7DA",
          { 0xB7, 0xDA, 0x8F, 0x41, 0x30, 0x8F, 0x61, 0x41, 0x40, 0x00, 0x30, 0x10, 0xF0 },
          13, UINT64_C(0xF1418E4031001160), 26099761 },
        { "pet name query mask B79A",
          { 0xB7, 0x9A, 0xE1, 0x10, 0xC2, 0xE1, 0xAE, 0x10, 0x41, 0xC2, 0x0E, 0xF0 },
          12, UINT64_C(0xF140E011C3000FAF), 14684611 },
        { "pet name query mask B7DE",
          { 0xB7, 0xDE, 0x6A, 0xF8, 0xB3, 0x6A, 0x5C, 0xF8, 0x03, 0x40, 0x00, 0xB3, 0x04, 0xF0 },
          14, UINT64_C(0xF1416BF9B202055D), 23853490 },
        { "pet name query second B7DA",
          { 0xB7, 0xDA, 0x3C, 0x32, 0x24, 0x3C, 0x7B, 0x32, 0x40, 0x00, 0x24, 0xDF, 0xF0 },
          13, UINT64_C(0xF1413D332500DE7A), 20788005 },
    };

    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i)
    {
        Request const& r = requests[i];
        WorldPacket packet(CMSG_PET_NAME_QUERY, uint32(r.length));
        packet.append(r.body, r.length);

        ObjectGuid guid;
        uint64 number = 0;
        MopCompactPackets::ReadPetNameQuery(packet, guid, number);

        if (guid.GetRawValue() != r.guid || number != r.number ||
            packet.rpos() != packet.size())
        {
            std::fprintf(stderr,
                "FAIL %s: guid 0x%016llX number %llu consumed %u/%u\n",
                r.what, (unsigned long long)guid.GetRawValue(),
                (unsigned long long)number,
                unsigned(packet.rpos()), unsigned(packet.size()));
            ++g_fail;
        }
    }

    {   // The reply to the first request above, from the same capture.
        std::string name = "Blue";
        WorldPacket p(SMSG_PET_NAME_QUERY_RESPONSE, 22);
        MopCompactPackets::BuildPetNameQueryResponse(p, 26099761, &name,
            1403635742u, NULL);
        CHECK(BytesEqual(p, {
            0x80,                                           // hasData, then 5x7 declined lengths,
            0x00, 0x00, 0x00, 0x00, 0x20,                   // one spare bit, and name length 4
            0x42, 0x6C, 0x75, 0x65,                         // "Blue", unterminated
            0x1E, 0xC8, 0xA9, 0x53,                         // timestamp
            0x31, 0x40, 0x8E, 0x01, 0x00, 0x00, 0x00, 0x00  // pet number, eight bytes, trailing
        }));
    }
    {   // A longer name, which moves the length field's low bits.
        std::string name = "Werenika";
        WorldPacket p(SMSG_PET_NAME_QUERY_RESPONSE, 26);
        MopCompactPackets::BuildPetNameQueryResponse(p, 23853490, &name, 0, NULL);
        CHECK(BytesEqual(p, {
            0x80, 0x00, 0x00, 0x00, 0x00, 0x40,
            0x57, 0x65, 0x72, 0x65, 0x6E, 0x69, 0x6B, 0x61,
            0x00, 0x00, 0x00, 0x00,
            0xB2, 0xF9, 0x6B, 0x01, 0x00, 0x00, 0x00, 0x00
        }));
    }
    {   // No pet found: one clear bit, then the number echoed so the client can
        // retire the request. Reader-derived shape; no observed body of this form.
        WorldPacket p(SMSG_PET_NAME_QUERY_RESPONSE, 9);
        MopCompactPackets::BuildPetNameQueryResponse(p, 26099761, NULL, 0, NULL);
        CHECK(BytesEqual(p, {
            0x00,
            0x31, 0x40, 0x8E, 0x01, 0x00, 0x00, 0x00, 0x00
        }));
    }
}

/// CMSG_LFG_JOIN, pinned to real 18414 bodies across the observed count range.
///
/// The interesting field is the 22-bit dungeon count, which is packed with an
/// 8-bit comment length and a flag into one 32-bit block. Counts of 1, 6 and 15
/// exercise the low bits of that field, and each body must consume exactly.
///
/// Every decoded slot carries its LFG type in the high byte and the dungeon id
/// in the low three, which is the check that does not come from the packet: the
/// ids land in a plausible LFGDungeons range and the type tags are uniform
/// within a request, as a queue built from one category should be.
static void test_lfg_join_matches_retail_bodies()
{
    struct Case
    {
        char const* what;
        uint8_t const* body;
        size_t length;
        uint32 roles;
        size_t dungeons;
        uint32 firstId;
        uint32 firstType;
    };

    static uint8_t const oneDungeon[] = {
        0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x03, 0x01, 0x00,
        0x06
    };
    static uint8_t const sixDungeons[] = {
        0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x02, 0x8E, 0x02, 0x00,
        0x01, 0x86, 0x02, 0x00, 0x01, 0x1B, 0x02, 0x00, 0x01, 0xF8, 0x01, 0x00,
        0x01, 0x4A, 0x02, 0x00, 0x01, 0x53, 0x02, 0x00, 0x01
    };
    static uint8_t const fifteenDungeons[] = {
        0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x02, 0x05, 0x02, 0x00,
        0x01, 0x6B, 0x02, 0x00, 0x01, 0xFF, 0x01, 0x00, 0x01, 0x19, 0x02, 0x00,
        0x01, 0x8E, 0x02, 0x00, 0x01, 0x86, 0x02, 0x00, 0x01, 0x1B, 0x02, 0x00,
        0x01, 0xF8, 0x01, 0x00, 0x01, 0x4A, 0x02, 0x00, 0x01, 0x87, 0x02, 0x00,
        0x01, 0x53, 0x02, 0x00, 0x01, 0xEC, 0x01, 0x00, 0x01, 0x89, 0x02, 0x00,
        0x01, 0x37, 0x02, 0x00, 0x01, 0xF3, 0x01, 0x00, 0x01
    };

    Case const cases[] = {
        { "lfg join one dungeon", oneDungeon, sizeof(oneDungeon), 7, 1, 259, 6 },
        { "lfg join six dungeons", sixDungeons, sizeof(sixDungeons), 8, 6, 654, 1 },
        { "lfg join fifteen dungeons", fifteenDungeons, sizeof(fifteenDungeons), 8, 15, 517, 1 },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        Case const& c = cases[i];
        WorldPacket packet(CMSG_LFG_JOIN, uint32(c.length));
        packet.append(c.body, c.length);

        uint8 partyIndex = 0;
        uint32 roles = 0, flag = 0;
        std::vector<uint32> dungeons;
        std::string comment;

        bool const ok = MopCompactPackets::ReadLfgJoin(packet, partyIndex, roles,
            flag, dungeons, comment);

        if (!ok || partyIndex != 0x7F || roles != c.roles ||
            dungeons.size() != c.dungeons || flag != 1 || !comment.empty() ||
            packet.rpos() != packet.size() ||
            (dungeons[0] & 0x00FFFFFF) != c.firstId || (dungeons[0] >> 24) != c.firstType)
        {
            std::fprintf(stderr,
                "FAIL %s: ok=%d party=0x%02X roles=%u count=%u flag=%u consumed %u/%u\n",
                c.what, int(ok), unsigned(partyIndex), roles,
                unsigned(dungeons.size()), flag,
                unsigned(packet.rpos()), unsigned(packet.size()));
            ++g_fail;
        }
    }

    {   // A claimed count that the body cannot hold must be refused outright,
        // not resized from. The count field is 22 bits, so this is 0x3FFFFF
        // dungeons in a body with four bytes left.
        uint8_t const liar[] = {
            0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x07, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFC, 0x02, 0x00, 0x00, 0x00,
            0x00
        };
        WorldPacket packet(CMSG_LFG_JOIN, uint32(sizeof(liar)));
        packet.append(liar, sizeof(liar));

        uint8 partyIndex = 0;
        uint32 roles = 0, flag = 0;
        std::vector<uint32> dungeons;
        std::string comment;
        CHECK(!MopCompactPackets::ReadLfgJoin(packet, partyIndex, roles, flag,
                                              dungeons, comment));
        CHECK(dungeons.empty());
    }
    {   // The grammar's total is exact, so a body claiming ONE dungeon while
        // carrying an extra trailing byte is malformed and must be refused too.
        // Accepting it would leave unread suffix data behind a successful parse.
        uint8_t const trailing[] = {
            0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x03, 0x01, 0x00,
            0x06, 0xEE
        };
        WorldPacket packet(CMSG_LFG_JOIN, uint32(sizeof(trailing)));
        packet.append(trailing, sizeof(trailing));

        uint8 partyIndex = 0;
        uint32 roles = 0, flag = 0;
        std::vector<uint32> dungeons;
        std::string comment;
        CHECK(!MopCompactPackets::ReadLfgJoin(packet, partyIndex, roles, flag,
                                              dungeons, comment));
    }
}

/// The mailbox family and the guild-bank tab query, pinned to real 18414 bodies.
///
/// The cross-opcode evidence is what makes these strong. capture-000879 and
/// capture-000025 each show one session where the SAME mailbox is recovered from
/// GET_MAIL_LIST, MARK_AS_READ and TAKE_ITEM -- three different mask orders and
/// three different byte orders -- and all three yield the same GUID byte for
/// byte. A wrong byte order permutes distinct byte values, so agreement across
/// three independent orders is evidence no single opcode's fixtures could give.
/// The mail id likewise matches between MARK_AS_READ and TAKE_ITEM.
///
/// 0x1372 also settles a naming disagreement. The reference overlay carried it
/// as CMSG_LFG_GET_PARTY_INFO; its bodies decode to a HIGHGUID_GAMEOBJECT guid,
/// a tab index and a send-all-slots boolean, which is a guild bank query and is
/// nothing an LFG party-info request would carry.
static void test_mail_family_matches_retail_bodies()
{
    {   // GET_MAIL_LIST: three distinct masks, three body lengths.
        struct Case { char const* what; uint8_t body[8]; size_t length; uint64 guid; };
        Case const cases[] = {
            { "get mail list B9", { 0xB9, 0xF0, 0x12, 0x5A, 0xA5, 0x31 }, 6,
              UINT64_C(0xF1135BA400000030) },
            { "get mail list B5", { 0xB5, 0xF0, 0x05, 0x12, 0x39, 0x04 }, 6,
              UINT64_C(0xF113380000000405) },
            { "get mail list BD", { 0xBD, 0xF0, 0x06, 0x12, 0x3D, 0x96, 0x14 }, 7,
              UINT64_C(0xF1133C9700000715) },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            WorldPacket p(CMSG_GET_MAIL_LIST, uint32(cases[i].length));
            p.append(cases[i].body, cases[i].length);
            ObjectGuid const guid = MopCompactPackets::ReadGetMailList(p);
            if (guid.GetRawValue() != cases[i].guid || p.rpos() != p.size())
            {
                std::fprintf(stderr, "FAIL %s: 0x%016llX consumed %u/%u\n", cases[i].what,
                             (unsigned long long)guid.GetRawValue(),
                             unsigned(p.rpos()), unsigned(p.size()));
                ++g_fail;
            }
        }
    }
    {   // MARK_AS_READ, same two mailboxes as above through a different order.
        uint8_t const a[] = { 0x1F, 0x37, 0x7C, 0x57, 0x8E, 0x80, 0xF0, 0x5A, 0x12, 0xA5, 0x31 };
        WorldPacket p(CMSG_MAIL_MARK_AS_READ, sizeof(a));
        p.append(a, sizeof(a));
        uint32 mailId = 0;
        ObjectGuid const guid = MopCompactPackets::ReadMailMarkAsRead(p, mailId);
        CHECK(mailId == 1467758367u);
        CHECK(guid.GetRawValue() == UINT64_C(0xF1135BA400000030));
        CHECK(p.rpos() == p.size());

        uint8_t const b[] = { 0x16, 0xC5, 0x7C, 0x57, 0x8F, 0x80, 0x06, 0xF0, 0x3D, 0x12, 0x96, 0x14 };
        WorldPacket q(CMSG_MAIL_MARK_AS_READ, sizeof(b));
        q.append(b, sizeof(b));
        uint32 mailIdB = 0;
        ObjectGuid const guidB = MopCompactPackets::ReadMailMarkAsRead(q, mailIdB);
        CHECK(mailIdB == 1467794710u);
        CHECK(guidB.GetRawValue() == UINT64_C(0xF1133C9700000715));
        CHECK(q.rpos() == q.size());
    }
    {   // TAKE_ITEM: same mailboxes and the same mail ids again, third order.
        uint8_t const a[] = { 0x1F, 0x37, 0x7C, 0x57, 0xBE, 0x6D, 0x86, 0x39,
                              0xCB, 0x31, 0xA5, 0x5A, 0x12, 0xF0 };
        WorldPacket p(CMSG_MAIL_TAKE_ITEM, sizeof(a));
        p.append(a, sizeof(a));
        uint32 mailId = 0, itemId = 0;
        ObjectGuid const guid = MopCompactPackets::ReadMailTakeItem(p, mailId, itemId);
        CHECK(mailId == 1467758367u);                       // as MARK_AS_READ above
        CHECK(itemId == 965111230u);
        CHECK(guid.GetRawValue() == UINT64_C(0xF1135BA400000030));
        CHECK(p.rpos() == p.size());

        uint8_t const b[] = { 0x16, 0xC5, 0x7C, 0x57, 0xCE, 0x00, 0x87, 0x39,
                              0xCF, 0x14, 0x06, 0x96, 0x3D, 0x12, 0xF0 };
        WorldPacket q(CMSG_MAIL_TAKE_ITEM, sizeof(b));
        q.append(b, sizeof(b));
        uint32 mailIdB = 0, itemIdB = 0;
        ObjectGuid const guidB = MopCompactPackets::ReadMailTakeItem(q, mailIdB, itemIdB);
        CHECK(mailIdB == 1467794710u);
        CHECK(itemIdB == 965148878u);
        CHECK(guidB.GetRawValue() == UINT64_C(0xF1133C9700000715));
        CHECK(q.rpos() == q.size());
    }
    {   // GUILD_BANK_QUERY_TAB. The first two differ ONLY in the send-all-slots
        // bit and the tab id, which is what proves that bit is a standalone
        // boolean and not a ninth GUID presence bit.
        uint8_t const noSlots[] = { 0x00, 0x97, 0x80, 0xF0, 0x12, 0x70, 0x43, 0x11, 0x06 };
        WorldPacket p(CMSG_GUILD_BANK_QUERY_TAB, sizeof(noSlots));
        p.append(noSlots, sizeof(noSlots));
        uint8 tabId = 0xFF;
        bool sendAll = true;
        ObjectGuid const guid = MopCompactPackets::ReadGuildBankQueryTab(p, tabId, sendAll);
        CHECK(tabId == 0);
        CHECK(sendAll == false);
        CHECK(guid.GetRawValue() == UINT64_C(0xF113427100000710));
        CHECK(p.rpos() == p.size());

        uint8_t const allSlots[] = { 0x03, 0xB7, 0x80, 0xF0, 0x12, 0x70, 0x43, 0x11, 0x06 };
        WorldPacket q(CMSG_GUILD_BANK_QUERY_TAB, sizeof(allSlots));
        q.append(allSlots, sizeof(allSlots));
        uint8 tabIdB = 0xFF;
        bool sendAllB = false;
        ObjectGuid const guidB = MopCompactPackets::ReadGuildBankQueryTab(q, tabIdB, sendAllB);
        CHECK(tabIdB == 3);
        CHECK(sendAllB == true);
        CHECK(guidB.GetRawValue() == UINT64_C(0xF113427100000710));  // same bank
        CHECK(q.rpos() == q.size());

        uint8_t const other[] = { 0x00, 0x9F, 0x80, 0xF0, 0x12, 0x0B, 0x12, 0x26, 0x74, 0xD6 };
        WorldPacket r(CMSG_GUILD_BANK_QUERY_TAB, sizeof(other));
        r.append(other, sizeof(other));
        uint8 tabIdC = 0xFF;
        bool sendAllC = true;
        ObjectGuid const guidC = MopCompactPackets::ReadGuildBankQueryTab(r, tabIdC, sendAllC);
        CHECK(tabIdC == 0);
        CHECK(guidC.GetRawValue() == UINT64_C(0xF113270A0013D775));
        CHECK(r.rpos() == r.size());
    }
}

static void test_outstanding_mail_requests_match_18414_bodies()
{
    {   // capture-000377/361653: largest observed SEND_MAIL body.
        std::vector<uint8_t> const body = {
            0x29,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0xF0,0x7C,0x09,0x6C,0xEB,0xEB,0xE3,0xEB,0xEB,0xEB,0xEB,0xEB,
            0xEB,0xEB,0xEB,0xEB,0x09,0x80,
            0x00,0x1D,0xDF,0x76,0x83,0x45,0x05,
            0x01,0x1D,0xE0,0x71,0x9D,0x45,0x05,
            0x02,0x1D,0x72,0x94,0x45,0x05,
            0x03,0x1D,0xBF,0x75,0x55,0x45,0x05,
            0x04,0x1D,0xA2,0x75,0x02,0x45,0x05,
            0x05,0x1D,0x3D,0x5F,0xB2,0x45,0x05,
            0x06,0x1D,0xBE,0x79,0xF7,0x45,0x05,
            0x07,0x1D,0xEB,0x6E,0xED,0x45,0x05,
            0x08,0x1C,0x9C,0x05,0xBD,0x45,0x05,
            0x09,0x1D,0x8D,0x75,0xE8,0x45,0x05,
            0x0A,0x1D,0x07,0x77,0x5E,0x45,0x05,
            0x0B,0x1D,0x77,0x70,0xC2,0x45,0x05,
            0x10,
            0x70,0x72,0x65,0x72,0x6F,0x62,0x20,0x6D,0x69,0x20,0x74,0x6F,
            0x20,0x70,0x72,0x6F,0x73,0x69,0x6D,0x20,0x6E,0x61,0x20,0x6D,
            0x61,0x67,0x6E,0x69,0x66,0x69,0x63,0x65,0x6E,0x74,0x20,0x68,
            0x69,0x64,0x65,0x20,0x61,0x20,0x70,0x6F,0x73,0x6C,0x69,0x20,
            0x6E,0x61,0x73,0x70,0x61,0x74,0x20,0x3A,0x29,0x20,0x64,0x69,
            0x6B,0x69,
            0x68,
            0x45,0x78,0x6F,0x74,0x69,0x63,0x20,0x4C,0x65,0x61,0x74,0x68,
            0x65,0x72,0x20,0x28,0x32,0x30,0x29,
            0x12,0x03,0xF0,0x0E,
            0x56,0x69,0x6E,0x72,0x79,0x2D,0x42,0x75,0x72,0x6E,0x69,0x6E,
            0x67,0x42,0x6C,0x61,0x64,0x65
        };
        WorldPacket packet = InputPacket(CMSG_SEND_MAIL, body);
        MopCompactPackets::SendMailRequest request;
        CHECK(MopCompactPackets::ReadSendMail(packet, request));
        CHECK(request.stationeryId == 41u);
        CHECK(request.packageId == 0u);
        CHECK(request.COD == 0);
        CHECK(request.money == 0);
        CHECK(request.mailboxGuid.GetRawValue() == UINT64_C(0xF113020F00001169));
        CHECK(request.body == "prerob mi to prosim na magnificent hide a posli naspat :) diki");
        CHECK(request.subject == "Exotic Leather (20)");
        CHECK(request.receiver == "Vinry-BurningBlade");
        CHECK(request.attachments.size() == 12);
        uint64 const expectedGuids[] = {
            UINT64_C(0x440000041C7782DE), UINT64_C(0x440000041C709CE1),
            UINT64_C(0x440000041C739500), UINT64_C(0x440000041C7454BE),
            UINT64_C(0x440000041C7403A3), UINT64_C(0x440000041C5EB33C),
            UINT64_C(0x440000041C78F6BF), UINT64_C(0x440000041C6FECEA),
            UINT64_C(0x440000041D04BC9D), UINT64_C(0x440000041C74E98C),
            UINT64_C(0x440000041C765F06), UINT64_C(0x440000041C71C376)
        };
        for (uint8 i = 0; i < 12; ++i)
        {
            CHECK(request.attachments[i].slot == i);
            CHECK(request.attachments[i].itemGuid.GetRawValue() == expectedGuids[i]);
        }
        CHECK(packet.rpos() == packet.size());
    }
    {   // capture-000025/8046: mode 3, after taking the last attachment.
        WorldPacket packet = InputPacket(CMSG_MAIL_DELETE,
            { 0x16,0xC5,0x7C,0x57, 0x03,0x00,0x00,0x00 });
        MopCompactPackets::MailDeleteRequest request;
        CHECK(MopCompactPackets::ReadMailDelete(packet, request));
        CHECK(request.mailId == 1467794710u);
        CHECK(request.deleteMode == 3u);
        CHECK(packet.rpos() == packet.size());
    }
    {   // capture-000119/320167: packed original sender GUID.
        WorldPacket packet = InputPacket(CMSG_MAIL_RETURN_TO_SENDER,
            { 0x66,0x47,0x15,0x54, 0xCE,0x49,0xD0,0x04,0x28,0x05 });
        MopCompactPackets::MailReturnRequest request;
        CHECK(MopCompactPackets::ReadMailReturnToSender(packet, request));
        CHECK(request.mailId == 1410680678u);
        CHECK(request.senderGuid.GetRawValue() == UINT64_C(0x04000000054829D1));
        CHECK(packet.rpos() == packet.size());
    }
    {   // Client-writer-derived construction; no 18414 corpus row exists.
        WorldPacket packet = InputPacket(CMSG_MAIL_CREATE_TEXT_ITEM,
            { 0x78,0x56,0x34,0x12, 0xF7,0x11,0x10,0x23,0x54,0xF0,0x32,0x45 });
        MopCompactPackets::MailCreateTextItemRequest request;
        CHECK(MopCompactPackets::ReadMailCreateTextItem(packet, request));
        CHECK(request.mailId == 0x12345678u);
        CHECK(request.mailboxGuid.GetRawValue() == UINT64_C(0xF110001122334455));
        CHECK(packet.rpos() == packet.size());
    }
}

static void test_mail_take_money_matches_retail_and_rejects_malformed_bodies()
{
    struct Fixture
    {
        std::vector<uint8_t> body;
        uint32_t mailId;
        uint64_t claimedMoney;
        uint64_t mailboxGuid;
    };

    Fixture const retail[] = {
        { { 0xCF,0xAF,0xE7,0x55, 0x7C,0x73,0x01,0,0,0,0,0,
            0xCF, 0xF0,0x06,0x90,0x1B,0x12,0x3D },
          1441247183u, UINT64_C(95100), UINT64_C(0xF1133C910000071A) },
        { { 0xF7,0x40,0x5E,0x56, 0xA8,0x76,0x9F,0,0,0,0,0,
            0xCF, 0xF0,0x10,0x0E,0x68,0x12,0x03 },
          1449017591u, UINT64_C(10450600), UINT64_C(0xF113020F00001169) },
        { { 0x03,0xFE,0xC5,0x57, 0x40,0x4B,0x4C,0,0,0,0,0,
            0xCF, 0xF0,0x07,0xB5,0xD4,0x13,0xDA },
          1472593411u, UINT64_C(5000000), UINT64_C(0xF112DBB4000006D5) }
    };

    for (Fixture const& fixture : retail)
    {
        WorldPacket packet = InputPacket(CMSG_MAIL_TAKE_MONEY, fixture.body);
        MopCompactPackets::MailTakeMoneyRequest request;
        CHECK(MopCompactPackets::ReadMailTakeMoney(packet, request));
        CHECK(request.mailId == fixture.mailId);
        CHECK(request.claimedMoney == fixture.claimedMoney);
        CHECK(request.mailboxGuid.GetRawValue() == fixture.mailboxGuid);
        CHECK(packet.rpos() == packet.size());
    }

    std::vector<uint8_t> const allZero = {
        0x78,0x56,0x34,0x12, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        0x00
    };
    {
        WorldPacket packet = InputPacket(CMSG_MAIL_TAKE_MONEY, allZero);
        MopCompactPackets::MailTakeMoneyRequest request;
        CHECK(MopCompactPackets::ReadMailTakeMoney(packet, request));
        CHECK(request.mailId == 0x12345678u);
        CHECK(request.claimedMoney == UINT64_C(0x1122334455667788));
        CHECK(request.mailboxGuid.IsEmpty());
    }

    std::vector<uint8_t> const dense = {
        0x78,0x56,0x34,0x12, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        0xFF, 0x86,0x20,0x55,0x11,0x42,0x33,0x77,0x64
    };
    {
        WorldPacket packet = InputPacket(CMSG_MAIL_TAKE_MONEY, dense);
        MopCompactPackets::MailTakeMoneyRequest request;
        CHECK(MopCompactPackets::ReadMailTakeMoney(packet, request));
        CHECK(request.mailboxGuid.GetRawValue() == UINT64_C(0x8776655443322110));
    }

    {   // Seven present bytes: GUID byte 1 is omitted by the mask and body.
        std::vector<uint8_t> const seven = {
            0x78,0x56,0x34,0x12, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
            0xFE, 0x86,0x55,0x11,0x42,0x33,0x77,0x64
        };
        WorldPacket packet = InputPacket(CMSG_MAIL_TAKE_MONEY, seven);
        MopCompactPackets::MailTakeMoneyRequest request;
        CHECK(MopCompactPackets::ReadMailTakeMoney(packet, request));
        CHECK(request.mailboxGuid.GetRawValue() == UINT64_C(0x8776655443320010));
    }

    struct OneHot { uint8_t mask; uint8_t wire; uint64_t expected; };
    OneHot const oneHot[] = {
        { 0x80,0x86,UINT64_C(0x8700000000000000) },
        { 0x40,0x77,UINT64_C(0x0076000000000000) },
        { 0x20,0x42,UINT64_C(0x0000000043000000) },
        { 0x10,0x33,UINT64_C(0x0000000000320000) },
        { 0x08,0x55,UINT64_C(0x0000005400000000) },
        { 0x04,0x64,UINT64_C(0x0000650000000000) },
        { 0x02,0x11,UINT64_C(0x0000000000000010) },
        { 0x01,0x20,UINT64_C(0x0000000000002100) }
    };
    for (OneHot const& fixture : oneHot)
    {
        std::vector<uint8_t> body(allZero.begin(), allZero.end());
        body[12] = fixture.mask;
        body.push_back(fixture.wire);
        WorldPacket packet = InputPacket(CMSG_MAIL_TAKE_MONEY, body);
        MopCompactPackets::MailTakeMoneyRequest request;
        CHECK(MopCompactPackets::ReadMailTakeMoney(packet, request));
        CHECK(request.mailboxGuid.GetRawValue() == fixture.expected);
    }

    std::vector<std::vector<uint8_t>> malformed;
    for (size_t length = 0; length < dense.size(); ++length)
    {
        malformed.push_back(std::vector<uint8_t>(dense.begin(), dense.begin() + length));
    }
    std::vector<uint8_t> trailing = dense;
    trailing.push_back(0xAA);
    malformed.push_back(trailing);
    std::vector<uint8_t> nonCanonical = allZero;
    nonCanonical[12] = 0x80;
    nonCanonical.push_back(0x01);
    malformed.push_back(nonCanonical);

    for (std::vector<uint8_t> const& body : malformed)
    {
        WorldPacket packet = InputPacket(CMSG_MAIL_TAKE_MONEY, body);
        MopCompactPackets::MailTakeMoneyRequest request;
        request.mailId = 0xFFFFFFFFu;
        request.claimedMoney = UINT64_C(0xFFFFFFFFFFFFFFFF);
        request.mailboxGuid = ObjectGuid(UINT64_C(0xFFFFFFFFFFFFFFFF));
        CHECK(!MopCompactPackets::ReadMailTakeMoney(packet, request));
        CHECK(request.mailId == 0);
        CHECK(request.claimedMoney == 0);
        CHECK(request.mailboxGuid.IsEmpty());
        CHECK(packet.rpos() == packet.size());
    }
}

/// CMSG_TOTEM_DESTROYED and CMSG_SET_ACTION_BUTTON, pinned to real 18414 bodies.
///
/// Both are a plain byte, a mask byte, then the present bytes of a packed value.
/// Five observed totem masks and four action-button masks. The action button is
/// additionally byte-for-byte identical to the client's writer sub_669CAE,
/// which is what proves its orders rather than merely constraining them.
///
/// The empty action-button body is the important one: mask 0x00 means every byte
/// of the packed value is absent, so the whole request is two bytes and clears
/// the slot. A reader expecting a fixed uint32 cannot express that at all.
static void test_totem_and_action_button_bodies()
{
    struct Totem { char const* what; uint8_t body[9]; size_t length; uint8 slot; uint64 guid; };
    Totem const totems[] = {
        { "totem mask A7", { 0x00, 0xA7, 0x31, 0x0C, 0x92, 0x67, 0xF0 }, 7, 0,
          UINT64_C(0xF130660D00009300) },
        { "totem mask 8F", { 0x00, 0x8F, 0x31, 0x07, 0xE9, 0x80, 0xF0 }, 7, 0,
          UINT64_C(0xF130E80600000081) },
        { "totem mask AF", { 0x01, 0xAF, 0x31, 0x20, 0x09, 0xBB, 0x84, 0xF0 }, 8, 1,
          UINT64_C(0xF130BA2100000885) },
        { "totem mask E7", { 0x00, 0xE7, 0x31, 0x26, 0xEA, 0xB6, 0xA8, 0xF0 }, 8, 0,
          UINT64_C(0xF130A9EB0027B700) },
        { "totem mask EF", { 0x00, 0xEF, 0x31, 0x90, 0x06, 0xB1, 0xE9, 0xCB, 0xF0 }, 9, 0,
          UINT64_C(0xF130E8070091B0CA) },
        { "manual totem slot 0", { 0x00, 0x00 }, 2, 0, UINT64_C(0) },
        { "manual totem slot 3", { 0x03, 0x00 }, 2, 3, UINT64_C(0) },
    };
    for (size_t i = 0; i < sizeof(totems) / sizeof(totems[0]); ++i)
    {
        WorldPacket p(CMSG_TOTEM_DESTROYED, uint32(totems[i].length));
        p.append(totems[i].body, totems[i].length);
        uint8 slot = 0xFF;
        ObjectGuid const guid = MopCompactPackets::ReadTotemDestroyed(p, slot);
        if (slot != totems[i].slot || guid.GetRawValue() != totems[i].guid ||
            p.rpos() != p.size())
        {
            std::fprintf(stderr, "FAIL %s: slot %u guid 0x%016llX consumed %u/%u\n",
                         totems[i].what, unsigned(slot),
                         (unsigned long long)guid.GetRawValue(),
                         unsigned(p.rpos()), unsigned(p.size()));
            ++g_fail;
        }
        // Every non-empty observed totem is a creature. The official manual UI
        // path deliberately supplies the empty GUID as a slot-only sentinel.
        if (totems[i].guid != 0)
        {
            CHECK((guid.GetRawValue() >> 52) == 0xF13);
        }
        CHECK(MopCompactPackets::IsTotemDestroyedRequestAdmissible(p, slot));
    }

    {
        // A canonical writer omits zero GUID bytes. Marking all bytes present
        // and sending 0x01 decodes to zero, but must not gain sentinel meaning.
        uint8_t const nonCanonicalEmpty[] = {
            0x00, 0xFF, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
        };
        WorldPacket p(CMSG_TOTEM_DESTROYED, sizeof(nonCanonicalEmpty));
        p.append(nonCanonicalEmpty, sizeof(nonCanonicalEmpty));
        uint8 slot = 0xFF;
        ObjectGuid const guid = MopCompactPackets::ReadTotemDestroyed(p, slot);
        CHECK(slot == 0);
        CHECK(guid.IsEmpty());
        CHECK(!MopCompactPackets::IsTotemDestroyedRequestAdmissible(p, slot));
    }

    {   // The dense EF body has seven GUID bytes. Every strict prefix from the
        // slot-only body through the final missing GUID byte must throw.
        uint8_t const dense[] = { 0x00, 0xEF, 0x31, 0x90, 0x06, 0xB1, 0xE9, 0xCB, 0xF0 };
        for (size_t length = 1; length < sizeof(dense); ++length)
        {
            WorldPacket p(CMSG_TOTEM_DESTROYED, uint32(length));
            p.append(dense, length);
            uint8 slot = 0xFF;
            bool threw = false;
            try
            {
                MopCompactPackets::ReadTotemDestroyed(p, slot);
            }
            catch (ByteBufferException const&)
            {
                threw = true;
            }
            CHECK(threw);
        }
    }

    {
        uint8_t const trailing[] = { 0x00, 0x00, 0xFF };
        WorldPacket p(CMSG_TOTEM_DESTROYED, sizeof(trailing));
        p.append(trailing, sizeof(trailing));
        uint8 slot = 0xFF;
        ObjectGuid const guid = MopCompactPackets::ReadTotemDestroyed(p, slot);
        CHECK(guid.IsEmpty());
        CHECK(!MopCompactPackets::IsTotemDestroyedRequestAdmissible(p, slot));
    }

    {
        uint8_t const hostileSlot[] = { 0x04, 0x00 };
        WorldPacket p(CMSG_TOTEM_DESTROYED, sizeof(hostileSlot));
        p.append(hostileSlot, sizeof(hostileSlot));
        uint8 slot = 0xFF;
        ObjectGuid const guid = MopCompactPackets::ReadTotemDestroyed(p, slot);
        CHECK(slot == 4);
        CHECK(guid.IsEmpty());
        CHECK(!MopCompactPackets::IsTotemDestroyedRequestAdmissible(p, slot));
    }

    {
        ObjectGuid const occupied(UINT64_C(0xF130660D00009300));
        ObjectGuid const replacement(UINT64_C(0xF130660D00009400));
        CHECK(MopCompactPackets::TotemDestroyedGuidMatches(ObjectGuid(), occupied));
        CHECK(MopCompactPackets::TotemDestroyedGuidMatches(occupied, occupied));
        CHECK(!MopCompactPackets::TotemDestroyedGuidMatches(occupied, replacement));
    }

    struct Button { char const* what; uint8_t body[6]; size_t length; uint8 slot; uint32 action; };
    Button const buttons[] = {
        { "action button cleared", { 0x0C, 0x00 }, 2, 12, 0 },
        { "action button mask 40", { 0x09, 0x40, 0x8A }, 3, 9, 139 },
        { "action button mask 48", { 0x00, 0x48, 0x00, 0x92 }, 4, 0, 403 },
        { "action button mask 18", { 0x3F, 0x18, 0x00, 0xA6 }, 4, 63, 108288 },
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        WorldPacket p(CMSG_SET_ACTION_BUTTON, uint32(buttons[i].length));
        p.append(buttons[i].body, buttons[i].length);
        uint8 slot = 0xFF, type = 0xFF;
        uint32 action = 0xFFFFFFFF;
        MopCompactPackets::ReadSetActionButton(p, slot, action, type);
        if (slot != buttons[i].slot || action != buttons[i].action || type != 0 ||
            p.rpos() != p.size())
        {
            std::fprintf(stderr, "FAIL %s: slot %u action %u type %u consumed %u/%u\n",
                         buttons[i].what, unsigned(slot), action, unsigned(type),
                         unsigned(p.rpos()), unsigned(p.size()));
            ++g_fail;
        }
    }

    {   // CONSTRUCTED, not observed: no corpus body sets the action's high byte,
        // so this one is built to the layout to pin the field's WIDTH. The action
        // occupies the full low 32 bits, and the inherited macro cut it at 24, so
        // a value above 0x00FFFFFF would have been silently truncated.
        //
        // Mask 0x42 marks byte0 (bit 6) and byte3 (bit 1) present. The byte order
        // reaches byte3 before byte0, and each is sent XOR 1, giving 0x03 then
        // 0x00 for the real values 0x02 and 0x01.
        uint8_t const wide[] = { 0x01, 0x42, 0x03, 0x00 };
        WorldPacket p(CMSG_SET_ACTION_BUTTON, sizeof(wide));
        p.append(wide, sizeof(wide));
        uint8 slot = 0, type = 0;
        uint32 action = 0;
        MopCompactPackets::ReadSetActionButton(p, slot, action, type);
        CHECK(p.rpos() == p.size());
        CHECK(slot == 0x01);
        CHECK(action == 0x02000001u);                       // survives past bit 24
        CHECK(type == 0);
    }
}

/// SMSG_SEND_MAIL_RESULT, pinned to real 18414 bodies.
///
/// Always 24 bytes, six little-endian uint32. The inherited sender wrote three
/// of them and made the rest conditional, so it produced 12, 16 or 20 bytes in a
/// different order.
///
/// The equip-error body is the one that discriminates the field order. Across
/// every body of this build, a non-zero word at offset 4 occurs in exactly the
/// bodies carrying 1 at offset 8 -- an equip error is only meaningful alongside
/// MAIL_ERR_EQUIP_ERROR. Swap the two and 50 lands in a field whose range stops
/// around 21, on a take that also reports success.
static void test_send_mail_result_matches_retail_bodies()
{
    {   // An item taken successfully: action 2, one item, no error.
        WorldPacket p(SMSG_SEND_MAIL_RESULT, 24);
        MopCompactPackets::BuildSendMailResult(p, 1467794710u, 0, 0, 2, 965148878u, 1);
        CHECK(BytesEqual(p, {
            0x16, 0xC5, 0x7C, 0x57,                         // mailId
            0x00, 0x00, 0x00, 0x00,                         // equipError
            0x00, 0x00, 0x00, 0x00,                         // mailError
            0x02, 0x00, 0x00, 0x00,                         // mailAction
            0xCE, 0x00, 0x87, 0x39,                         // itemGuidLow
            0x01, 0x00, 0x00, 0x00                          // itemCount
        }));
    }
    {   // The discriminating body: equipError 50 with mailError 1, and the take
        // reports no items because it failed.
        WorldPacket p(SMSG_SEND_MAIL_RESULT, 24);
        MopCompactPackets::BuildSendMailResult(p, 1442599846u, 50, 1, 2, 938134456u, 0);
        CHECK(BytesEqual(p, {
            0xA6, 0x53, 0xFC, 0x55,
            0x32, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00,
            0xB8, 0xCB, 0xEA, 0x37,
            0x00, 0x00, 0x00, 0x00
        }));
    }
    {   // A send that created no mail: everything zero but the error.
        WorldPacket p(SMSG_SEND_MAIL_RESULT, 24);
        MopCompactPackets::BuildSendMailResult(p, 0, 0, 2, 0, 0, 0);
        CHECK(BytesEqual(p, {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        }));
    }
    {   // Constructed containment reply: authority/COD failure names the item.
        WorldPacket p(SMSG_SEND_MAIL_RESULT, 24);
        MopCompactPackets::BuildSendMailResult(
            p, 0x11223344u, 0, 6, 2, 0xA1B2C3D4u, 0);
        CHECK(BytesEqual(p, {
            0x44, 0x33, 0x22, 0x11,
            0x00, 0x00, 0x00, 0x00,
            0x06, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00,
            0xD4, 0xC3, 0xB2, 0xA1,
            0x00, 0x00, 0x00, 0x00
        }));
    }
    {   // Deleted: action 4 is MAIL_DELETED, not MAIL_RETURNED_TO_SENDER, which
        // is 3. Either way the item fields are zero but still present.
        WorldPacket p(SMSG_SEND_MAIL_RESULT, 24);
        MopCompactPackets::BuildSendMailResult(p, 1467794710u, 0, 0, 4, 0, 0);
        CHECK(p.size() == 24);                              // never conditional
        CHECK(BytesEqual(p, {
            0x16, 0xC5, 0x7C, 0x57,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        }));
    }
    {   // Take-money success: action 1, no error, fixed zero tail.
        WorldPacket p(SMSG_SEND_MAIL_RESULT, 24);
        MopCompactPackets::BuildSendMailResult(p, 0x12345678u, 0, 0, 1, 0, 0);
        CHECK(BytesEqual(p, {
            0x78,0x56,0x34,0x12, 0,0,0,0, 0,0,0,0,
            1,0,0,0, 0,0,0,0, 0,0,0,0
        }));
    }
    {   // Take-money cap failure: equip 77, mail error 1, action 1.
        WorldPacket p(SMSG_SEND_MAIL_RESULT, 24);
        MopCompactPackets::BuildSendMailResult(p, 0x12345678u, 77, 1, 1, 0, 0);
        CHECK(BytesEqual(p, {
            0x78,0x56,0x34,0x12, 77,0,0,0, 1,0,0,0,
            1,0,0,0, 0,0,0,0, 0,0,0,0
        }));
    }
    {   // Take-money internal failure: no equip error, mail error 6, action 1.
        WorldPacket p(SMSG_SEND_MAIL_RESULT, 24);
        MopCompactPackets::BuildSendMailResult(p, 0x12345678u, 0, 6, 1, 0, 0);
        CHECK(BytesEqual(p, {
            0x78,0x56,0x34,0x12, 0,0,0,0, 6,0,0,0,
            1,0,0,0, 0,0,0,0, 0,0,0,0
        }));
    }
}

/// The can-fly family, all four opcodes, pinned to real 18414 bodies.
///
/// Every one of these builders was wrong before, and none of the errors would
/// have shown as a wrong length. The two mover packets had their mask and byte
/// orders permuted, and SMSG_MOVE_UNSET_CAN_FLY wrote TWO GUID bytes before the
/// counter where the client reads THREE -- so the client takes a GUID byte as
/// the counter's low byte and then over-reads, while the total length matches
/// either way. That is why it survived: nothing here was checking order.
///
/// Five mover bodies across five distinct masks, four observer bodies across two.
/// The mover pair carries a uint32 counter interleaved into the byte run; the
/// observer pair carries no scalar at all.
static void test_can_fly_family_matches_retail_bodies()
{
    {   // set can fly
        WorldPacket p(SMSG_MOVE_SET_CAN_FLY, 13);
        MopCompactPackets::BuildMoveSetCanFly(p, UINT64_C(0xF15070260011F951), 23);
        CHECK(BytesEqual(p, { 0xF7, 0x27, 0x10, 0x17, 0x00, 0x00, 0x00, 0x51, 0xF8, 0x50, 0xF0, 0x71 }));
    }
    {   // set can fly, sparse guid
        WorldPacket p(SMSG_MOVE_SET_CAN_FLY, 13);
        MopCompactPackets::BuildMoveSetCanFly(p, UINT64_C(0x04000000053CC8E8), 66);
        CHECK(BytesEqual(p, { 0x5D, 0x3D, 0x42, 0x00, 0x00, 0x00, 0x04, 0xC9, 0xE9, 0x05 }));
    }
    {   // set can fly, third mask
        WorldPacket p(SMSG_MOVE_SET_CAN_FLY, 13);
        MopCompactPackets::BuildMoveSetCanFly(p, UINT64_C(0x0180000004B22206), 88);
        CHECK(BytesEqual(p, { 0xDD, 0xB3, 0x58, 0x00, 0x00, 0x00, 0x81, 0x05, 0x23, 0x07, 0x00 }));
    }
    {   // unset can fly
        WorldPacket p(SMSG_MOVE_UNSET_CAN_FLY, 13);
        MopCompactPackets::BuildMoveUnsetCanFly(p, UINT64_C(0x04000000053CC8E8), 74);
        CHECK(BytesEqual(p, { 0x2F, 0x05, 0x4A, 0x00, 0x00, 0x00, 0x3D, 0x04, 0xC9, 0xE9 }));
    }
    {   // unset can fly, second mask
        WorldPacket p(SMSG_MOVE_UNSET_CAN_FLY, 13);
        MopCompactPackets::BuildMoveUnsetCanFly(p, UINT64_C(0x0180000004B22206), 67);
        CHECK(BytesEqual(p, { 0xAF, 0x00, 0x43, 0x00, 0x00, 0x00, 0x81, 0xB3, 0x05, 0x23, 0x07 }));
    }
    {   // spline set flying
        WorldPacket p(SMSG_SPLINE_MOVE_SET_FLYING, 9);
        MopCompactPackets::BuildSplineMoveSetFlying(p, UINT64_C(0x0580000003EC8BCD));
        CHECK(BytesEqual(p, { 0x7B, 0x04, 0x8A, 0xCC, 0x02, 0x81, 0xED }));
    }
    {   // spline set flying, other guid
        WorldPacket p(SMSG_SPLINE_MOVE_SET_FLYING, 9);
        MopCompactPackets::BuildSplineMoveSetFlying(p, UINT64_C(0x0180000004F3F30B));
        CHECK(BytesEqual(p, { 0x7B, 0x00, 0xF2, 0x0A, 0x05, 0x81, 0xF2 }));
    }
    {   // spline unset flying
        WorldPacket p(SMSG_SPLINE_MOVE_UNSET_FLYING, 9);
        MopCompactPackets::BuildSplineMoveUnsetFlying(p, UINT64_C(0x04000000053C811E));
        CHECK(BytesEqual(p, { 0xB6, 0x3D, 0x80, 0x1F, 0x05, 0x04 }));
    }
    {   // spline unset flying, other guid
        WorldPacket p(SMSG_SPLINE_MOVE_UNSET_FLYING, 9);
        MopCompactPackets::BuildSplineMoveUnsetFlying(p, UINT64_C(0x0400000002196723));
        CHECK(BytesEqual(p, { 0xB6, 0x18, 0x66, 0x22, 0x05, 0x03 }));
    }
}

static void test_instance_reset_result_bodies()
{
    WorldPacket success(SMSG_INSTANCE_RESET, 4);
    MopCompactPackets::BuildInstanceResetSuccess(success, 0x11223344u);
    CHECK(BytesEqual(success, { 0x44, 0x33, 0x22, 0x11 }));

    WorldPacket failed(SMSG_INSTANCE_RESET_FAILED, 8);
    MopCompactPackets::BuildInstanceResetFailed(failed, 3u, 0x11223344u);
    CHECK(BytesEqual(failed, { 0x03, 0x00, 0x00, 0x00,
                               0x44, 0x33, 0x22, 0x11 }));

    WorldPacket notify(SMSG_RESET_FAILED_NOTIFY, 0);
    MopCompactPackets::BuildResetFailedNotify(notify);
    CHECK(BytesEqual(notify, {}));
}


int main(int /*argc*/, char** /*argv*/)
{
    test_attack_packets();
    test_attacker_state_update();
    test_run_speed_matches_retail_body();
    test_spline_run_speed_matches_retail_body();
    test_pet_action_matches_retail_bodies();
    test_pet_stop_attack_matches_retail_and_rejects_malformed_bodies();
    test_pet_set_action_matches_retail_and_rejects_malformed_bodies();
    test_pet_name_query_matches_retail_bodies();
    test_lfg_join_matches_retail_bodies();
    test_mail_family_matches_retail_bodies();
    test_outstanding_mail_requests_match_18414_bodies();
    test_mail_take_money_matches_retail_and_rejects_malformed_bodies();
    test_send_mail_result_matches_retail_bodies();
    test_totem_and_action_button_bodies();
    test_can_fly_family_matches_retail_bodies();
    test_random_roll_guid_layouts();
    test_instance_encounter_variants();
    test_raid_difficulty();
    test_dungeon_difficulty();
    test_cancel_combat();
    test_party_kill_log();
    test_duel_state_packets();
    test_duel_request_and_winner_packets();
    test_mirror_timer_packets();
    test_rune_packets();
    test_threat_packets();
    test_dismount_packet();
    test_show_bank_matches_retail_bodies();
    test_guild_banker_activate_retail_bodies();
    test_guild_banker_activate_rejects_malformed_bodies();
    test_pre_resurrect_packet();
    test_combo_points_packet();
    test_instance_reset_result_bodies();

    if (g_fail)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }

    std::printf("mop_compact_packets: all checks passed\n");
    return 0;
}
