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
 * Byte-exact tests for the 5.4.8 name-query request/response path.
 */

#include "WorldSession.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "Database/DatabaseEnv.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// The server process owns these globals. Initializing the real opcode table
// pulls handler translation units from game.lib into this standalone fixture.
DatabaseType WorldDatabase;
DatabaseType CharacterDatabase;
DatabaseType LoginDatabase;
uint32 realmID = 0;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static bool ExpectBytes(WorldPacket const& packet, std::vector<uint8_t> const& expected)
{
    if (packet.size() != expected.size())
        return false;
    for (size_t i = 0; i < expected.size(); ++i)
        if (packet.contents()[i] != expected[i])
            return false;
    return true;
}

static uint8_t GuidByte(uint64 value, size_t index)
{
    return uint8_t(value >> (index * 8));
}

class ReferenceWriter
{
public:
    void Bit(bool value)
    {
        if (m_bit == 8)
        {
            m_bytes.push_back(0);
            m_bit = 0;
        }
        if (value)
            m_bytes.back() |= uint8_t(0x80u >> m_bit);
        ++m_bit;
    }

    void Bits(uint32 value, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            Bit((value & (uint32(1) << (count - i - 1))) != 0);
    }

    void Align() { m_bit = 8; }
    void U8(uint8 value) { Align(); m_bytes.push_back(value); }
    void U32(uint32 value)
    {
        Align();
        for (size_t i = 0; i < 4; ++i)
            m_bytes.push_back(uint8(value >> (i * 8)));
    }
    void GuidXor(uint64 value, size_t index)
    {
        if (uint8 byte = GuidByte(value, index))
            U8(byte ^ 1);
    }
    void String(std::string const& value)
    {
        Align();
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
    }
    std::vector<uint8_t> const& Bytes() const { return m_bytes; }

private:
    std::vector<uint8_t> m_bytes;
    size_t m_bit = 8;
};

static std::vector<uint8_t> ReferenceResponse(MopQueryPackets::NameQueryResponse const& r)
{
    ReferenceWriter w;
    for (size_t i : { 3u, 6u, 7u, 2u, 5u, 4u, 0u, 1u })
        w.Bit(GuidByte(r.guid, i) != 0);
    w.Align();
    for (size_t i : { 5u, 4u, 7u, 6u, 1u, 2u })
        w.GuidXor(r.guid, i);
    w.U8(r.result);
    if (r.result == 0)
    {
        w.U32(r.realmId);
        w.U32(r.accountId);
        w.U8(r.classId);
        w.U8(r.race);
        w.U8(r.level);
        w.U8(r.gender);
    }
    w.GuidXor(r.guid, 0);
    w.GuidXor(r.guid, 3);
    if (r.result != 0)
        return w.Bytes();

    for (size_t i : { 2u, 7u }) w.Bit(GuidByte(r.auxiliaryGuid, i) != 0);
    for (size_t i : { 7u, 2u, 0u }) w.Bit(GuidByte(r.displayGuid, i) != 0);
    w.Bit(r.isDeleted);
    w.Bit(GuidByte(r.auxiliaryGuid, 4) != 0);
    w.Bit(GuidByte(r.displayGuid, 5) != 0);
    for (size_t i : { 1u, 3u, 0u }) w.Bit(GuidByte(r.auxiliaryGuid, i) != 0);
    for (std::string const& name : r.declinedNames) w.Bits(uint32(name.size()), 7);
    for (size_t i : { 6u, 3u }) w.Bit(GuidByte(r.displayGuid, i) != 0);
    w.Bit(GuidByte(r.auxiliaryGuid, 5) != 0);
    for (size_t i : { 1u, 4u }) w.Bit(GuidByte(r.displayGuid, i) != 0);
    w.Bits(uint32(r.name.size()), 6);
    w.Bit(GuidByte(r.auxiliaryGuid, 6) != 0);
    w.Align();

    w.GuidXor(r.displayGuid, 6);
    w.GuidXor(r.displayGuid, 0);
    w.String(r.name);
    w.GuidXor(r.auxiliaryGuid, 5);
    w.GuidXor(r.auxiliaryGuid, 2);
    w.GuidXor(r.displayGuid, 3);
    w.GuidXor(r.auxiliaryGuid, 4);
    w.GuidXor(r.auxiliaryGuid, 3);
    w.GuidXor(r.displayGuid, 4);
    w.GuidXor(r.displayGuid, 2);
    w.GuidXor(r.auxiliaryGuid, 7);
    for (std::string const& name : r.declinedNames) w.String(name);
    w.GuidXor(r.auxiliaryGuid, 6);
    w.GuidXor(r.displayGuid, 7);
    w.GuidXor(r.displayGuid, 1);
    w.GuidXor(r.auxiliaryGuid, 1);
    w.GuidXor(r.displayGuid, 5);
    w.GuidXor(r.auxiliaryGuid, 0);
    return w.Bytes();
}

static void test_request_fixtures()
{
    {
        uint8_t const fixture[] = {
            0xFF, 0xC0, 0x81, 0x61, 0x21, 0x31, 0x71, 0x41, 0x11, 0x51,
            0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55
        };
        WorldPacket packet(CMSG_NAME_QUERY, sizeof(fixture));
        packet.append(fixture, sizeof(fixture));
        MopQueryPackets::NameQueryRequest const request =
            MopQueryPackets::ReadNameQueryRequest(packet);
        CHECK(request.guid == 0x8070605040302010ull);
        CHECK(request.hasRealmId2 && request.realmId2 == 0x11223344u);
        CHECK(request.hasRealmId1 && request.realmId1 == 0x55667788u);
        CHECK(packet.rpos() == packet.size());
    }
    {
        uint8_t const fixture[] = { 0x92, 0x00, 0x11, 0x11, 0xDD, 0xCC, 0xBB, 0xAA };
        WorldPacket packet(CMSG_NAME_QUERY, sizeof(fixture));
        packet.append(fixture, sizeof(fixture));
        MopQueryPackets::NameQueryRequest const request =
            MopQueryPackets::ReadNameQueryRequest(packet);
        CHECK(request.guid == 0x0000001000000010ull);
        CHECK(!request.hasRealmId2 && request.realmId2 == 0);
        CHECK(request.hasRealmId1 && request.realmId1 == 0xAABBCCDDu);
        CHECK(packet.rpos() == packet.size());
    }
}

static void test_exact_missing_response()
{
    MopQueryPackets::NameQueryResponse record;
    record.guid = 0x8070605040302010ull;
    WorldPacket packet(SMSG_NAME_QUERY_RESPONSE, 10);
    MopQueryPackets::BuildNameQueryResponse(packet, record);
    CHECK(ExpectBytes(packet, { 0xFF, 0x61, 0x51, 0x81, 0x71, 0x21, 0x31, 0x01, 0x11, 0x41 }));
}

static void test_populated_success()
{
    MopQueryPackets::NameQueryResponse record;
    record.guid = 0x8070605040302010ull;
    record.result = 0;
    record.realmId = 0x11223344u;
    record.accountId = 0x55667788u;
    record.classId = 0x21;
    record.race = 0x22;
    record.level = 0x23;
    record.gender = 0x24;
    record.auxiliaryGuid = 0x0102030405060708ull;
    record.displayGuid = 0x8877665544332211ull;
    record.isDeleted = true;
    record.name = "Name";
    record.declinedNames = {{ "One", "Two2", "Three33", "Four4444", "Five55555" }};
    WorldPacket packet(SMSG_NAME_QUERY_RESPONSE, 128);
    MopQueryPackets::BuildNameQueryResponse(packet, record);
    CHECK(ExpectBytes(packet, ReferenceResponse(record)));
}

static void test_empty_optional_success()
{
    MopQueryPackets::NameQueryResponse record;
    record.guid = 0x0000000000000010ull;
    record.result = 0;
    record.displayGuid = record.guid;
    record.name = "A";
    WorldPacket packet(SMSG_NAME_QUERY_RESPONSE, 32);
    MopQueryPackets::BuildNameQueryResponse(packet, record);
    CHECK(ExpectBytes(packet, ReferenceResponse(record)));
}

static void test_client_string_boundaries()
{
    MopQueryPackets::NameQueryResponse record;
    record.guid = 0x10;
    record.displayGuid = record.guid;
    record.result = 0;
    record.name.assign(48, 'N');
    for (std::string& name : record.declinedNames)
        name.assign(64, 'D');
    WorldPacket packet(SMSG_NAME_QUERY_RESPONSE, 384);
    MopQueryPackets::BuildNameQueryResponse(packet, record);
    CHECK(ExpectBytes(packet, ReferenceResponse(record)));
}

static void test_opcode_values_are_framable()
{
    CHECK(uint32_t(CMSG_NAME_QUERY) == 0x0328u);
    CHECK(uint32_t(SMSG_NAME_QUERY_RESPONSE) == 0x169Bu);
    CHECK(uint32_t(CMSG_NAME_QUERY) < uint32_t(OPCODE_TABLE_SIZE));
    CHECK(uint32_t(SMSG_NAME_QUERY_RESPONSE) < uint32_t(OPCODE_TABLE_SIZE));
}

static void test_request_is_dispatched_during_transfer()
{
    InitializeOpcodes();
    OpcodeHandler const* handler = LookupClientOpcode(CMSG_NAME_QUERY);
    CHECK(handler != nullptr);
    if (handler)
    {
        CHECK(handler->status == STATUS_LOGGEDIN_OR_TRANSFER);
        CHECK(handler->handler == &WorldSession::HandleNameQueryOpcode);
    }
}

int main(int /*argc*/, char** /*argv*/)
{
    test_request_fixtures();
    test_exact_missing_response();
    test_populated_success();
    test_empty_optional_success();
    test_client_string_boundaries();
    test_opcode_values_are_framable();
    test_request_is_dispatched_during_transfer();
    if (g_fail)
        return 1;
    std::printf("mop_name_query: all checks passed\n");
    return 0;
}
