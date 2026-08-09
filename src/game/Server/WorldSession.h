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

/// \addtogroup u2w
/// @{
/// \file

#ifndef MANGOS_H_WORLDSESSION
#define MANGOS_H_WORLDSESSION

#include "Common.h"
#include "Auth/BigNumber.h"
#include "Auth/MopAuthKey.h"
#include "SharedDefines.h"
#include "ObjectGuid.h"
#include "AuctionHouseMgr.h"
#include "Item.h"
#include "LFGMgr.h"
#include "Opcodes.h"
#include "WorldPacket.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ItemPrototype;
struct AuctionEntry;
struct AuctionHouseEntry;
struct DeclinedName;

class ObjectGuid;
class Creature;
class Item;
class Object;
class Player;
class Unit;
class Warden;
class WorldPacket;
class QueryResult;
class LoginQueryHolder;
class CharacterHandler;
class GMTicket;
class MovementInfo;
class WorldSession;

namespace proto { class IClientLink; }

namespace MopTradePackets
{
    struct StatusData;
}

namespace MopHotfixPackets
{
    struct HotfixRecord
    {
        uint64 guid = 0;
        uint32 entry = 0;
    };

    struct HotfixRequest
    {
        uint32 type = 0;
        std::vector<HotfixRecord> records;
    };

    inline bool ReadHotfixRequest(WorldPacket& in, HotfixRequest& request)
    {
        request.records.clear();
        in >> request.type;
        uint32 const count = in.ReadBits(21);
        size_t const remaining = in.size() - in.rpos();
        if (count > remaining / 5)
            return false;

        std::vector<ObjectGuid> guids(count);
        request.records.resize(count);
        for (ObjectGuid& guid : guids)
            in.ReadGuidMask<6, 3, 0, 1, 4, 5, 7, 2>(guid);

        for (uint32 i = 0; i < count; ++i)
        {
            in.ReadGuidBytes<1>(guids[i]);
            in >> request.records[i].entry;
            in.ReadGuidBytes<0, 5, 6, 4, 7, 2, 3>(guids[i]);
            request.records[i].guid = guids[i].GetRawValue();
        }
        return true;
    }

    inline void BuildDbReply(WorldPacket& out, uint32 entry, uint32 hotfixDate,
        uint32 type, ByteBuffer const& record)
    {
        out << entry;
        out << hotfixDate;
        out << type;
        out << uint32(record.size());
        out.append(record);
    }
}

namespace MopClientRequestPackets
{
    struct LoadScreenRequest
    {
        uint32 mapId = 0;
        bool loading = false;
    };

    inline LoadScreenRequest ReadLoadScreenRequest(WorldPacket& in)
    {
        LoadScreenRequest request;
        in >> request.mapId;
        request.loading = in.ReadBit();
        return request;
    }
}

namespace MopQueryPackets
{
    constexpr uint32 MaximumQuestPoiQueries = 25;

    struct NameQueryRequest
    {
        uint64 guid = 0;
        bool hasRealmId2 = false;
        uint32 realmId2 = 0;
        bool hasRealmId1 = false;
        uint32 realmId1 = 0;
    };

    struct NameQueryResponse
    {
        uint64 guid = 0;
        uint8 result = 1;
        uint32 realmId = 0;
        uint32 accountId = 0;
        uint8 classId = 0;
        uint8 race = 0;
        uint8 level = 0;
        uint8 gender = 0;
        uint64 auxiliaryGuid = 0;
        uint64 displayGuid = 0;
        bool isDeleted = false;
        std::string name;
        std::array<std::string, 5> declinedNames;
    };

    NameQueryRequest ReadNameQueryRequest(WorldPacket& in);
    void BuildNameQueryResponse(WorldPacket& out,
        NameQueryResponse const& record);

    struct RealmNameQueryResponse
    {
        uint32 realmId = 0;
        uint8 status = 0;
        bool isHomeRealm = false;
        std::string name;
        std::string normalizedName;
    };

    uint32 ReadRealmNameQueryRequest(WorldPacket& in);
    void BuildRealmNameQueryResponse(WorldPacket& out,
        RealmNameQueryResponse const& record);

    void BuildQueryTimeResponse(WorldPacket& out, uint32 serverTime,
        uint32 secondsUntilReset);
    bool ReadPlayedTimeRequest(WorldPacket& in);
    void BuildPlayedTimeResponse(WorldPacket& out, uint32 totalPlayed,
        uint32 levelPlayed, bool displayEvent);

    struct MailNextTimeEntry
    {
        uint64 senderGuid = 0;
        uint32 nonPlayerSender = 0;
        uint8 messageType = 0;
        float deliveryTime = 0.0f;
        bool hasNativeRealmAddress = false;
        uint32 nativeRealmAddress = 0;
        uint32 stationery = 0;
        bool hasVirtualRealmAddress = false;
        uint32 virtualRealmAddress = 0;
    };

    bool BuildMailQueryNextTimeResult(WorldPacket& out,
        std::vector<MailNextTimeEntry> const& records, float nextMailTime);

    struct CreatureQueryResponse
    {
        uint32 entry = 0;
        bool hasData = false;
        std::string name;
        std::string subName;
        std::string iconName;
        uint32 creatureType = 0;
        uint32 family = 0;
        uint32 rank = 0;
        uint32 expansion = 0;
        uint32 movementTemplateId = 0;
        uint32 creatureTypeFlags = 0;
        uint32 creatureTypeFlags2 = 0;
        std::array<uint32, 4> modelIds{};
        std::array<uint32, 2> killCredits{};
        float healthMultiplier = 0.0f;
        float powerMultiplier = 0.0f;
        bool racialLeader = false;
        std::array<uint32, 6> questItems{};
    };

    void BuildCreatureQueryResponse(WorldPacket& out,
        CreatureQueryResponse const& record);

    struct GameObjectQueryRequest
    {
        uint32 entry = 0;
        uint64 guid = 0;
    };

    struct GameObjectQueryResponse
    {
        uint32 entry = 0;
        bool hasData = false;
        uint32 type = 0;
        uint32 displayId = 0;
        std::array<std::string, 4> names;
        std::string iconName;
        std::string castBarCaption;
        std::string unknownString;
        std::array<uint32, 32> data{};
        float size = 0.0f;
        std::vector<uint32> questItems;
        uint32 trailingUnknown = 0;
    };

    GameObjectQueryRequest ReadGameObjectQueryRequest(WorldPacket& in);
    void BuildGameObjectQueryResponse(WorldPacket& out,
        GameObjectQueryResponse const& record);

    struct PageTextQueryRequest
    {
        uint32 pageId = 0;
        uint64 sourceGuid = 0;
    };

    struct PageTextQueryResponse
    {
        bool hasData = false;
        uint32 pageId = 0;
        uint32 nextPageId = 0;
        std::string text;
    };

    bool ParsePageTextQueryRequest(WorldPacket& in,
        PageTextQueryRequest& request);
    bool BuildPageTextQueryResponse(WorldPacket& out,
        PageTextQueryResponse const& response);

    struct CorpseQueryResponse
    {
        bool found = false;
        int32 displayMapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32 corpseMapId = 0;
        uint64 transportGuid = 0;
    };

    uint64 ReadCorpseMapPositionQuery(WorldPacket& in);
    void BuildCorpseQueryResponse(WorldPacket& out,
        CorpseQueryResponse const& response);
    void BuildCorpseMapPositionQueryResponse(WorldPacket& out,
        float x, float y, float z, float orientation);

    struct QuestPoiPoint
    {
        int32 x = 0;
        int32 y = 0;
    };

    struct QuestPoiRecord
    {
        uint32 poiId = 0;
        int32 objectiveIndex = 0;
        uint32 unknown2 = 0;
        uint32 mapId = 0;
        uint32 mapAreaId = 0;
        uint32 worldEffectId = 0;
        uint32 playerConditionId = 0;
        uint32 unknown1 = 0;
        uint32 unknown3 = 0;
        uint32 unknown4 = 0;
        // No floorId. SMSG_QUEST_POI_QUERY_RESPONSE has no floor field -- the parser
        // (sub_14043F7A0 in the 18414 x64 client) reads exactly ten scalars per POI and
        // none of them is a floor. quest_poi.floorId still holds six-digit blob ids on 49
        // rows, and putting any of them back on the wire lands in the slot the client
        // treats as an element count, which is what crashed it on quest accept. The member
        // is deliberately absent so that reintroducing it is a compile error rather than a
        // silent regression; LoadQuestPOI() keeps the column and its clamp as db hygiene.
        std::vector<QuestPoiPoint> points;
    };

    struct QuestPoiResponse
    {
        uint32 questId = 0;
        std::vector<QuestPoiRecord> pois;
    };

    bool ParseQuestPoiQueryRequest(WorldPacket& in,
        std::vector<uint32>& questIds);
    bool BuildQuestPoiQueryResponse(WorldPacket& out,
        std::vector<QuestPoiResponse> const& response);

    struct QuestNpcResponse
    {
        uint32 questId = 0;
        std::vector<uint32> npcIds;
    };

    bool ParseQuestNpcQueryRequest(WorldPacket& in, uint32& questId);
    bool BuildQuestNpcQueryResponse(WorldPacket& out,
        std::vector<QuestNpcResponse> const& response);
}

namespace MopStablePackets
{
    struct StablePetRecord
    {
        uint32 entry = 0;
        uint32 level = 0;
        uint8 state = 0;
        uint32 modelId = 0;
        std::string name;
        uint32 petNumber = 0;
        uint32 slot = 0;
    };

    uint64 ReadStableListRequest(WorldPacket& in);
    bool BuildPetStableList(WorldPacket& out, uint64 stableMasterGuid,
        std::vector<StablePetRecord> const& records);
    void BuildStableResult(WorldPacket& out, uint8 result);
}

namespace MopTrainerBuyFailed
{
    enum Reason
    {
        REASON_UNAVAILABLE = 0,
        REASON_NOT_ENOUGH_MONEY = 1
    };

    void Build(WorldPacket& out, uint64 trainerGuid, uint32 reason, uint32 serviceId);
}

namespace MopQueryPacketDetail
{
    inline uint8 GuidByte(uint64 guid, size_t index)
    {
        return uint8(guid >> (index * 8));
    }

    inline size_t OptionalCStringLength(std::string const& text, size_t bitCount)
    {
        size_t const encoded = text.empty() ? 0 : text.size() + 1;
        MANGOS_ASSERT(encoded < (size_t(1) << bitCount));
        return encoded;
    }
}

namespace MopStablePacketDetail
{
    inline uint8 GuidByte(uint64 guid, uint8 index)
    {
        return uint8(guid >> (index * 8));
    }
}

namespace MopTrainerPacketDetail
{
    inline uint8 GuidByte(uint64 guid, int index)
    {
        return uint8(guid >> (8 * index));
    }

    inline constexpr int MaskOrder[8] = { 3, 0, 4, 7, 6, 1, 5, 2 };
    inline constexpr int BytesPre[5] = { 1, 2, 0, 3, 4 };
    inline constexpr int BytesPost[3] = { 5, 6, 7 };
}

inline MopQueryPackets::NameQueryRequest MopQueryPackets::ReadNameQueryRequest(
    WorldPacket& in)
{
    NameQueryRequest request;
    std::array<uint8, 8> guidBytes{};

    guidBytes[4] = in.ReadBit();
    request.hasRealmId2 = in.ReadBit();
    guidBytes[6] = in.ReadBit();
    guidBytes[0] = in.ReadBit();
    guidBytes[7] = in.ReadBit();
    guidBytes[1] = in.ReadBit();
    request.hasRealmId1 = in.ReadBit();
    guidBytes[5] = in.ReadBit();
    guidBytes[2] = in.ReadBit();
    guidBytes[3] = in.ReadBit();

    in.ReadByteSeq(guidBytes[7]);
    in.ReadByteSeq(guidBytes[5]);
    in.ReadByteSeq(guidBytes[1]);
    in.ReadByteSeq(guidBytes[2]);
    in.ReadByteSeq(guidBytes[6]);
    in.ReadByteSeq(guidBytes[3]);
    in.ReadByteSeq(guidBytes[0]);
    in.ReadByteSeq(guidBytes[4]);

    if (request.hasRealmId2)
        in >> request.realmId2;
    if (request.hasRealmId1)
        in >> request.realmId1;

    for (size_t i = 0; i < guidBytes.size(); ++i)
        request.guid |= uint64(guidBytes[i]) << (i * 8);
    return request;
}

inline void MopQueryPackets::BuildNameQueryResponse(WorldPacket& out,
    NameQueryResponse const& record)
{
    for (size_t index : { 3u, 6u, 7u, 2u, 5u, 4u, 0u, 1u })
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.guid, index) != 0);
    out.FlushBits();

    for (size_t index : { 5u, 4u, 7u, 6u, 1u, 2u })
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.guid, index));
    out << record.result;

    if (record.result == 0)
    {
        out << record.realmId;
        out << record.accountId;
        out << record.classId;
        out << record.race;
        out << record.level;
        out << record.gender;
    }

    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.guid, 0));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.guid, 3));
    if (record.result != 0)
        return;

    MANGOS_ASSERT(record.name.size() <= 48);
    for (std::string const& name : record.declinedNames)
        MANGOS_ASSERT(name.size() <= 64);

    out.WriteBit(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 2) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 7) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.displayGuid, 7) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.displayGuid, 2) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.displayGuid, 0) != 0);
    out.WriteBit(record.isDeleted);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 4) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.displayGuid, 5) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 1) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 3) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 0) != 0);
    for (std::string const& name : record.declinedNames)
        out.WriteBits(uint32(name.size()), 7);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.displayGuid, 6) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.displayGuid, 3) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 5) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.displayGuid, 1) != 0);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.displayGuid, 4) != 0);
    out.WriteBits(uint32(record.name.size()), 6);
    out.WriteBit(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 6) != 0);
    out.FlushBits();

    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.displayGuid, 6));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.displayGuid, 0));
    if (!record.name.empty())
        out.append(record.name.c_str(), record.name.size());
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 5));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 2));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.displayGuid, 3));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 4));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 3));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.displayGuid, 4));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.displayGuid, 2));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 7));
    for (std::string const& name : record.declinedNames)
        if (!name.empty())
            out.append(name.c_str(), name.size());
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 6));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.displayGuid, 7));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.displayGuid, 1));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 1));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.displayGuid, 5));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.auxiliaryGuid, 0));
}

inline uint32 MopQueryPackets::ReadRealmNameQueryRequest(WorldPacket& in)
{
    uint32 realmId = 0;
    in >> realmId;
    return realmId;
}

inline void MopQueryPackets::BuildRealmNameQueryResponse(WorldPacket& out,
    RealmNameQueryResponse const& record)
{
    // 18414 wire layout for the client handler sub_1403073A0 (fills the RealmCache
    // keyed by realmId and, on status==0, sets the ready-flag that un-gates the parked
    // name-query result). The status byte leads, then realmId; the name/normalizedName
    // tail follows only when the realm is reported found. (Live-confirmed: with realmId
    // leading, the client read realmId's low byte as the status and skipped the store.)
    out << uint8(record.status);                          // 0 = found -> commits the parked player name
    out << uint32(record.realmId);
    if (record.status == 0)
    {
        out.WriteBits(uint32(record.name.size()), 8);
        out.WriteBit(record.isHomeRealm);                 // 1 = home realm -> no cross-realm suffix
        out.WriteBits(uint32(record.normalizedName.size()), 8);
        out.FlushBits();
        if (!record.name.empty())
            out.append(record.name.c_str(), record.name.size());       // no trailing NUL
        if (!record.normalizedName.empty())
            out.append(record.normalizedName.c_str(), record.normalizedName.size());
    }
}

inline void MopQueryPackets::BuildQueryTimeResponse(WorldPacket& out, uint32 serverTime,
    uint32 secondsUntilReset)
{
    out << serverTime;
    out << secondsUntilReset;
}

inline bool MopQueryPackets::ReadPlayedTimeRequest(WorldPacket& in)
{
    return in.ReadBit();
}

inline void MopQueryPackets::BuildPlayedTimeResponse(WorldPacket& out, uint32 totalPlayed,
    uint32 levelPlayed, bool displayEvent)
{
    out << totalPlayed;
    out << levelPlayed;
    out.WriteBit(displayEvent);
    out.FlushBits();
}

inline bool MopQueryPackets::BuildMailQueryNextTimeResult(WorldPacket& out,
    std::vector<MailNextTimeEntry> const& records, float nextMailTime)
{
    // The 18414 client retains only three records, and the reference server
    // stops producing records at that same bound.
    if (records.size() > 3)
        return false;

    out.WriteBits(records.size(), 20);
    for (MailNextTimeEntry const& record : records)
    {
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.senderGuid, 3) != 0);
        out.WriteBit(record.hasVirtualRealmAddress);
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.senderGuid, 2) != 0);
        out.WriteBit(record.hasNativeRealmAddress);
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.senderGuid, 6) != 0);
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.senderGuid, 1) != 0);
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.senderGuid, 4) != 0);
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.senderGuid, 0) != 0);
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.senderGuid, 5) != 0);
        out.WriteBit(MopQueryPacketDetail::GuidByte(record.senderGuid, 7) != 0);
    }
    out.FlushBits();

    for (MailNextTimeEntry const& record : records)
    {
        out << record.nonPlayerSender;
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.senderGuid, 5));
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.senderGuid, 4));
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.senderGuid, 6));
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.senderGuid, 1));
        out << record.messageType;
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.senderGuid, 0));
        out << record.deliveryTime;
        if (record.hasNativeRealmAddress)
            out << record.nativeRealmAddress;
        out << record.stationery;
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.senderGuid, 3));
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.senderGuid, 2));
        if (record.hasVirtualRealmAddress)
            out << record.virtualRealmAddress;
        out.WriteByteSeq(MopQueryPacketDetail::GuidByte(record.senderGuid, 7));
    }

    out << nextMailTime;
    return true;
}

inline void MopQueryPackets::BuildCreatureQueryResponse(WorldPacket& out,
    CreatureQueryResponse const& record)
{
    out << record.entry;
    out.WriteBit(record.hasData);
    if (!record.hasData)
    {
        out.FlushBits();
        return;
    }

    size_t const subNameLength = MopQueryPacketDetail::OptionalCStringLength(record.subName, 11);
    size_t const nameLength = MopQueryPacketDetail::OptionalCStringLength(record.name, 11);
    size_t const iconLength = MopQueryPacketDetail::OptionalCStringLength(record.iconName, 6);
    MANGOS_ASSERT(record.questItems.size() < (size_t(1) << 22));

    out.WriteBits(uint32(subNameLength), 11);
    out.WriteBits(uint32(record.questItems.size()), 22);
    out.WriteBits(0, 11);
    out.WriteBits(uint32(nameLength), 11);
    for (int i = 0; i < 7; ++i)
        out.WriteBits(0, 11);
    out.WriteBit(record.racialLeader);
    out.WriteBits(uint32(iconLength), 6);
    out.FlushBits();

    out << record.killCredits[0];
    out << record.modelIds[3];
    out << record.modelIds[1];
    out << record.expansion;
    out << record.creatureType;
    out << record.healthMultiplier;
    out << record.creatureTypeFlags;
    out << record.creatureTypeFlags2;
    out << record.rank;
    out << record.movementTemplateId;
    if (nameLength)
        out << record.name;
    if (subNameLength)
        out << record.subName;
    out << record.modelIds[0];
    out << record.modelIds[2];
    if (iconLength)
        out << record.iconName;
    for (uint32 questItem : record.questItems)
        out << questItem;
    out << record.killCredits[1];
    out << record.powerMultiplier;
    out << record.family;
}

inline MopQueryPackets::GameObjectQueryRequest MopQueryPackets::ReadGameObjectQueryRequest(
    WorldPacket& in)
{
    GameObjectQueryRequest request;
    in >> request.entry;

    std::array<uint8, 8> guidBytes{};
    guidBytes[5] = in.ReadBit();
    guidBytes[3] = in.ReadBit();
    guidBytes[6] = in.ReadBit();
    guidBytes[2] = in.ReadBit();
    guidBytes[7] = in.ReadBit();
    guidBytes[1] = in.ReadBit();
    guidBytes[0] = in.ReadBit();
    guidBytes[4] = in.ReadBit();

    in.ReadByteSeq(guidBytes[1]);
    in.ReadByteSeq(guidBytes[5]);
    in.ReadByteSeq(guidBytes[3]);
    in.ReadByteSeq(guidBytes[4]);
    in.ReadByteSeq(guidBytes[6]);
    in.ReadByteSeq(guidBytes[2]);
    in.ReadByteSeq(guidBytes[7]);
    in.ReadByteSeq(guidBytes[0]);

    for (size_t i = 0; i < guidBytes.size(); ++i)
        request.guid |= uint64(guidBytes[i]) << (i * 8);
    return request;
}

inline void MopQueryPackets::BuildGameObjectQueryResponse(WorldPacket& out,
    GameObjectQueryResponse const& record)
{
    ByteBuffer blob(160);
    if (record.hasData)
    {
        for (std::string const& name : record.names)
            MANGOS_ASSERT(name.size() < 0x400);
        MANGOS_ASSERT(record.iconName.size() < 0x400);
        MANGOS_ASSERT(record.castBarCaption.size() < 0x400);
        MANGOS_ASSERT(record.unknownString.size() < 0x400);
        MANGOS_ASSERT(record.questItems.size() <= 0xFF);

        blob << record.type;
        blob << record.displayId;
        for (std::string const& name : record.names)
            blob << name;
        blob << record.iconName;
        blob << record.castBarCaption;
        blob << record.unknownString;
        for (uint32 value : record.data)
            blob << value;
        blob << record.size;
        blob << uint8(record.questItems.size());
        for (uint32 questItem : record.questItems)
            blob << questItem;
        blob << record.trailingUnknown;
    }

    out.WriteBit(record.hasData);
    out.FlushBits();
    out << record.entry;
    out << uint32(blob.size());
    out.append(blob);
}

inline bool MopQueryPackets::ParsePageTextQueryRequest(WorldPacket& in,
    PageTextQueryRequest& request)
{
    if (in.size() - in.rpos() < 5)
    {
        in.rfinish();
        return false;
    }

    PageTextQueryRequest parsed;
    in >> parsed.pageId;

    uint8 const mask = in[in.rpos()];
    size_t byteCount = 0;
    for (uint8 remainingMask = mask; remainingMask != 0;
        remainingMask >>= 1)
    {
        byteCount += remainingMask & 1;
    }

    if (in.size() - in.rpos() != 1 + byteCount)
    {
        in.rfinish();
        return false;
    }

    // A present packed-GUID byte is XORed with one on the wire. The client
    // never emits encoded 1 because that would decode to an absent zero byte.
    for (size_t index = 0; index < byteCount; ++index)
    {
        if (in[in.rpos() + 1 + index] == 1)
        {
            in.rfinish();
            return false;
        }
    }

    ObjectGuid sourceGuid;
    in.ReadGuidMask<2, 1, 3, 7, 6, 4, 0, 5>(sourceGuid);
    in.ReadGuidBytes<0, 6, 3, 5, 1, 7, 4, 2>(sourceGuid);
    if (in.rpos() != in.size())
    {
        in.rfinish();
        return false;
    }

    parsed.sourceGuid = sourceGuid.GetRawValue();
    request = parsed;
    return true;
}

inline bool MopQueryPackets::BuildPageTextQueryResponse(WorldPacket& out,
    PageTextQueryResponse const& response)
{
    // The active sub_70F012 record has 4000-byte text storage; the alternate
    // validating reader sub_706F6F explicitly rejects 4001 and above.
    if (response.hasData && response.text.size() > 4000)
    {
        return false;
    }

    WorldPacket built(SMSG_PAGE_TEXT_QUERY_RESPONSE,
        response.hasData ? 14 + response.text.size() : 5);
    built.WriteBit(response.hasData);
    if (response.hasData)
    {
        built.WriteBits(uint32(response.text.size()), 12);
    }
    built.FlushBits();

    if (response.hasData)
    {
        built << response.nextPageId;
        built << response.pageId;
        built.append(response.text.data(), response.text.size());
    }

    // This key is present on both success and failure and drives the client's
    // pagetextcache.wdb insert/miss callback.
    built << response.pageId;
    out = built;
    return true;
}

inline uint64 MopQueryPackets::ReadCorpseMapPositionQuery(WorldPacket& in)
{
    uint8 const maskOrder[] = { 7, 6, 3, 0, 4, 1, 5, 2 };
    uint8 const byteOrder[] = { 1, 6, 0, 5, 3, 2, 4, 7 };
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

inline void MopQueryPackets::BuildCorpseQueryResponse(WorldPacket& out,
    CorpseQueryResponse const& response)
{
    uint64 const guid = response.transportGuid;

    for (uint8 index : { uint8(0), uint8(3), uint8(2) })
        out.WriteBit(MopQueryPacketDetail::GuidByte(guid, index) != 0);
    out.WriteBit(response.found);
    for (uint8 index : { uint8(5), uint8(4), uint8(1), uint8(7), uint8(6) })
        out.WriteBit(MopQueryPacketDetail::GuidByte(guid, index) != 0);
    out.FlushBits();

    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(guid, 5));
    out << response.z;
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(guid, 1));
    out << response.displayMapId;
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(guid, 6));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(guid, 4));
    out << response.x;
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(guid, 3));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(guid, 7));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(guid, 2));
    out.WriteByteSeq(MopQueryPacketDetail::GuidByte(guid, 0));
    out << response.corpseMapId;
    out << response.y;
}

inline void MopQueryPackets::BuildCorpseMapPositionQueryResponse(
    WorldPacket& out, float x, float y, float z, float orientation)
{
    out << x << orientation << z << y;
}

inline bool MopQueryPackets::ParseQuestPoiQueryRequest(WorldPacket& in,
    std::vector<uint32>& questIds)
{
    if (in.size() - in.rpos() < 3)
    {
        in.rfinish();
        return false;
    }

    uint32 const count = in.ReadBits(22);
    in.ResetBitReader();
    size_t const remaining = in.size() - in.rpos();
    if (count > MaximumQuestPoiQueries ||
        remaining != size_t(count) * sizeof(uint32))
    {
        in.rfinish();
        return false;
    }

    std::vector<uint32> parsed;
    parsed.reserve(count);
    for (uint32 i = 0; i < count; ++i)
    {
        uint32 questId = 0;
        in >> questId;
        parsed.push_back(questId);
    }

    if (in.rpos() != in.size())
    {
        in.rfinish();
        return false;
    }

    questIds = parsed;
    return true;
}

inline bool MopQueryPackets::BuildQuestPoiQueryResponse(WorldPacket& out,
    std::vector<QuestPoiResponse> const& response)
{
    if (response.size() >= (size_t(1) << 20))
        return false;

    for (QuestPoiResponse const& quest : response)
    {
        if (quest.pois.size() >= (size_t(1) << 18))
            return false;
        for (QuestPoiRecord const& poi : quest.pois)
        {
            if (poi.points.size() >= (size_t(1) << 21))
                return false;
        }
    }

    WorldPacket built(SMSG_QUEST_POI_QUERY_RESPONSE, 4);
    built.WriteBits(uint32(response.size()), 20);
    for (QuestPoiResponse const& quest : response)
    {
        built.WriteBits(uint32(quest.pois.size()), 18);
        for (QuestPoiRecord const& poi : quest.pois)
            built.WriteBits(uint32(poi.points.size()), 21);
    }
    built.FlushBits();

    // Build 18414 reads this byte phase only after all nested counts. The
    // trailing per-quest and top-level counts are deliberately duplicated:
    // the client stores and uses both the bit-phase and byte-phase values.
    for (QuestPoiResponse const& quest : response)
    {
        for (QuestPoiRecord const& poi : quest.pois)
        {
            built << poi.worldEffectId;
            for (QuestPoiPoint const& point : poi.points)
                built << point.x << point.y;
            built << poi.objectiveIndex;
            built << poi.poiId;
            built << poi.unknown2;
            // Slot 3, not the home of quest_poi.unk4. Across 3825 retail POIs this field
            // is 0 or a five-digit id (13903, 13904, 15624, ...); we have no source for
            // it, so 0 -- which is what unknown1 holds -- is the correct value. unk4 was
            // being written here and its domain cannot occur in this slot.
            built << poi.unknown1;
            built << poi.mapId;
            // The point count again, NOT floorId. Same bit-phase/byte-phase duplication
            // the quest and POI counts use above. This slot held floorId, which is 0 in
            // 28,128 of our 29,117 quest_poi rows, so for 96.6% of POIs the client read
            // "this POI has no points" and drew nothing -- no error, no short read, the
            // response simply rendered empty and no quest markers appeared on the map.
            // Measured over 3825 POIs in 400 retail SMSG_QUEST_POI_QUERY_RESPONSE bodies:
            // this field equals that POI's own point count in 3825 of 3825, across counts
            // 1,3,5,6,7,8,9,10,11 and 12. floorId is not carried in this packet.
            built << uint32(poi.points.size());
            built << poi.mapAreaId;
            built << poi.unknown3;
            // Slot 8 is where quest_poi.unk4 belongs. Its value domain settles it: our
            // table holds 1, 7, 3, 5, 0, 2 and retail's slot 8 holds 0, 1, 3, 7, while
            // retail's slot 3 holds 0 and five-digit ids that unk4 never takes. Every
            // retail POI sampled in Elwynn (WorldMapArea 30) carries 1 here, which is
            // also our most common unk4 by a wide margin (23,637 of 29,117 rows).
            built << poi.unknown4;
            built << poi.playerConditionId;
        }
        built << quest.questId;
        built << uint32(quest.pois.size());
    }
    built << uint32(response.size());

    out = built;
    return true;
}

inline bool MopQueryPackets::ParseQuestNpcQueryRequest(WorldPacket& in,
    uint32& questId)
{
    if (in.size() < 4)
        return false;

    // The 18414 client always sends a 204-byte body but initialises only the
    // leading quest id; the remainder is uninitialised client stack memory
    // (on the 64-bit client, image pointers identical across a whole run).
    // Read the one defined field and deliberately ignore the rest rather than
    // validating a length the client does not actually populate.
    in.rpos(0);
    in >> questId;
    in.rpos(in.size());
    return true;
}

inline bool MopQueryPackets::BuildQuestNpcQueryResponse(WorldPacket& out,
    std::vector<QuestNpcResponse> const& response)
{
    if (response.size() >= (size_t(1) << 21))
        return false;

    for (QuestNpcResponse const& quest : response)
    {
        if (quest.npcIds.size() >= (size_t(1) << 22))
            return false;
    }

    // Grammar from client parser sub_6B8B3B -> sub_6B8A06: a 21-bit quest
    // count, then one 22-bit NPC count per quest, then a byte-aligned phase
    // of quest id followed by that quest's NPC ids. Confirmed against real
    // 18414 retail captures, which decode byte-exact under this reader.
    WorldPacket built(SMSG_QUEST_NPC_QUERY_RESPONSE, 4);
    built.WriteBits(uint32(response.size()), 21);
    for (QuestNpcResponse const& quest : response)
        built.WriteBits(uint32(quest.npcIds.size()), 22);
    built.FlushBits();

    for (QuestNpcResponse const& quest : response)
    {
        built << quest.questId;
        for (uint32 npcId : quest.npcIds)
            built << npcId;
    }

    out = built;
    return true;
}

inline uint64 MopStablePackets::ReadStableListRequest(WorldPacket& in)
{
    uint8 const maskOrder[] = { 0, 5, 1, 3, 6, 7, 2, 4 };
    uint8 const byteOrder[] = { 0, 5, 7, 1, 2, 3, 4, 6 };
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

inline bool MopStablePackets::BuildPetStableList(WorldPacket& out,
    uint64 stableMasterGuid, std::vector<StablePetRecord> const& records)
{
    if (records.size() > 55)
        return false;
    bool occupiedSlots[55] = {};
    for (StablePetRecord const& record : records)
    {
        if (record.name.size() > 255 || record.slot > 54)
            return false;
        if (occupiedSlots[record.slot])
            return false;
        occupiedSlots[record.slot] = true;
    }

    uint8 const maskOrder[] = { 3, 0, 4, 7, 2, 1, 6, 5 };
    uint8 const byteOrder[] = { 3, 5, 7, 2, 0, 4, 1, 6 };

    out.Initialize(SMSG_PET_STABLE_LIST, 32 + records.size() * 24);
    for (uint8 index : maskOrder)
        out.WriteBit(MopStablePacketDetail::GuidByte(stableMasterGuid, index) != 0);
    out.WriteBits(records.size(), 19);
    for (StablePetRecord const& record : records)
        out.WriteBits(record.name.size(), 8);
    out.FlushBits();

    for (StablePetRecord const& record : records)
    {
        out << record.entry;
        out << record.level;
        out << record.state;
        out << record.modelId;
        out.append(record.name.c_str(), record.name.size());
        out << record.petNumber;
        out << record.slot;
    }
    for (uint8 index : byteOrder)
        out.WriteByteSeq(MopStablePacketDetail::GuidByte(stableMasterGuid, index));
    return true;
}

inline void MopStablePackets::BuildStableResult(WorldPacket& out, uint8 result)
{
    out.Initialize(SMSG_STABLE_RESULT, 1);
    out << result;
}

inline void MopTrainerBuyFailed::Build(WorldPacket& out, uint64 trainerGuid, uint32 reason, uint32 serviceId)
{
    for (int i = 0; i < 8; ++i)
    {
        out.WriteBit(MopTrainerPacketDetail::GuidByte(trainerGuid, MopTrainerPacketDetail::MaskOrder[i]) != 0);
    }

    // Exactly eight bits were written, so this emits one whole mask byte and
    // leaves the buffer byte-aligned for the byte block below.
    out.FlushBits();

    for (int i = 0; i < 5; ++i)
    {
        out.WriteByteSeq(MopTrainerPacketDetail::GuidByte(trainerGuid, MopTrainerPacketDetail::BytesPre[i]));
    }

    out << uint32(reason);

    for (int i = 0; i < 3; ++i)
    {
        out.WriteByteSeq(MopTrainerPacketDetail::GuidByte(trainerGuid, MopTrainerPacketDetail::BytesPost[i]));
    }

    out << uint32(serviceId);
}


struct OpcodeHandler;

namespace MopCreateGating
{
    inline uint8 ClassRequiredExpansion(uint8 class_)
    {
        switch (class_)
        {
            case CLASS_MONK:
                return EXPANSION_MOP;
            case CLASS_DEATH_KNIGHT:
                return EXPANSION_WOTLK;
            default:
                return EXPANSION_NONE;
        }
    }

    inline bool TwoSideCreateViolation(Team newTeam,
        std::vector<Team> const& existingTeams)
    {
        if (newTeam == TEAM_NONE)
            return false;

        for (Team existingTeam : existingTeams)
            if (existingTeam != TEAM_NONE && existingTeam != newTeam)
                return true;

        return false;
    }
}

namespace MopCompactPackets
{
    inline uint8 RandomRollGuidByte(uint64 guid, uint8 index)
    {
        return uint8(guid >> (8 * index));
    }

    inline void BuildRandomRoll(WorldPacket& out, uint64 rollerGuid,
        uint32 minimum, uint32 maximum, uint32 roll)
    {
        uint8 const maskOrder[] = { 0, 6, 7, 1, 4, 5, 2, 3 };
        uint8 const byteOrder[] = { 5, 4, 2, 0, 3, 1, 6, 7 };

        out << uint32(roll) << uint32(minimum) << uint32(maximum);
        for (uint8 index : maskOrder)
            out.WriteBit(RandomRollGuidByte(rollerGuid, index) != 0);
        out.FlushBits();
        for (uint8 index : byteOrder)
            out.WriteByteSeq(RandomRollGuidByte(rollerGuid, index));
    }
}

enum AccountDataType
{
    GLOBAL_CONFIG_CACHE             = 0,                    // 0x01 g
    PER_CHARACTER_CONFIG_CACHE      = 1,                    // 0x02 p
    GLOBAL_BINDINGS_CACHE           = 2,                    // 0x04 g
    PER_CHARACTER_BINDINGS_CACHE    = 3,                    // 0x08 p
    GLOBAL_MACROS_CACHE             = 4,                    // 0x10 g
    PER_CHARACTER_MACROS_CACHE      = 5,                    // 0x20 p
    PER_CHARACTER_LAYOUT_CACHE      = 6,                    // 0x40 p
    PER_CHARACTER_CHAT_CACHE        = 7,                    // 0x80 p
    NUM_ACCOUNT_DATA_TYPES          = 8
};

#define GLOBAL_CACHE_MASK           0x15
#define PER_CHARACTER_CACHE_MASK    0xEA

// A hotfix type is the DB2's own table hash, taken from the WDB2 header of the
// extracted file. Confirmed against the two already served: 1344507586 is
// 0x50238EC2, the table hash of Item.db2, and 2442913102 is 0x919BE54E, that of
// Item-sparse.db2.
#define DB2_REPLY_ITEM 1344507586
#define DB2_REPLY_SPARSE 2442913102
// 0x021826BB, the table hash of BroadcastText.db2. The live client requests
// this the moment it is handed a BroadcastText id it cannot resolve locally.
#define DB2_REPLY_BROADCAST_TEXT 35137211
// 0x63B4C4BA, the table hash of BattlePetEffectProperties.db2. This is the
// first thing the client asks for after logging in, and every answer is a
// not-found -- see SendBattlePetEffectPropertiesDb2Reply.
#define DB2_REPLY_BATTLE_PET_EFFECT_PROPERTIES 1672791226

struct AccountData
{
    AccountData() : Time(0), Data("") {}

    time_t Time;
    std::string Data;
};

struct AddonInfo
{
    AddonInfo(const std::string& name, uint8 enabled, uint32 crc) :
        Name(name),
        Enabled(enabled),
        CRC(crc)
    {
    }

    std::string Name;
    uint8 Enabled;
    uint32 CRC;
};

typedef std::list<AddonInfo> AddonsList;

namespace MopAddonPackets
{
    // SMSG_ADDON_INFO does not carry the addon public key in modulus order. The client's
    // generated parser (JamAddonInfo, sub_14050D1B0 in the 18414 x64 client) reads the 256
    // key bytes one at a time and scatters each one to a fixed destination inside the addon
    // record: wire byte i is stored at key[kAddonKeyWireOrder[i]]. To make the client
    // reconstruct the modulus, the server must therefore emit key[kAddonKeyWireOrder[i]].
    //
    // Sending the modulus in its natural order left the client holding a key that differed
    // from the real one at 254 of 256 positions, so the RSA check inside sub_140D0CFD0
    // failed, sub_140D0D480 returned 1 ("corrupt signature") instead of 3, and every
    // "## Secure:" Blizzard addon loaded UNTRUSTED. An untrusted addon is executed under its
    // own taint identity, which is why Blizzard_TimeManager, Blizzard_CompactRaidFrames and
    // the rest tainted their globals at the very first line of each file.
    //
    // The table was recovered from the parser and then confirmed against live client output:
    // the 44 .pub files the client wrote back satisfy pub[kAddonKeyWireOrder[i]] == tdata[i]
    // for all 256 i, which is exactly the raw-order send being scattered.
    // inline constexpr, not plain const: a namespace-scope const has internal linkage, so an
    // external-linkage inline function odr-using it would name a different object in every
    // translation unit that includes this header -- ill-formed, no diagnostic required, and it
    // would also duplicate the table across a lot of object files.
    inline constexpr uint8 kAddonKeyWireOrder[256] =
    {
          5, 176, 148,  43,  28, 135,  64,   8, 160, 145, 226, 119, 181, 192, 240,  72,
        243, 212, 209, 172,  21, 237,  85,  10,  75, 117, 244,  82,  24,  20,  18,  76,
         67,  57, 157,  59, 198,  90,  22,   6,  49,  12,  95, 193, 118,  94,  40,  98,
        255, 169, 214,  83, 128, 219,  73, 247, 132, 202, 218, 154, 112, 131, 177, 111,
        144,  56,  39, 152,  48,  63,  25, 114,  38,  84,  99, 165, 126,  34,  69, 183,
        185,  52, 103,  36, 233,   3,  47, 141, 162, 232, 194, 253, 116,  27,  80,  46,
         89, 107, 189,  14, 225, 167, 140, 250, 188,  17,  29, 137, 133,  74, 178,  62,
        236,  31, 101,   9, 164, 200, 136, 159, 197, 216, 246, 134,   0,  97, 234, 166,
        204,  65,  60, 223, 122,   2,   4, 239, 249,  30, 252, 211, 124,  26,  23, 161,
         92, 138,  37, 227, 120, 153, 115, 151, 254, 173, 175, 108, 130, 251, 170, 158,
         11, 245, 190, 104, 217,   7,  78, 231, 155, 171,  55,  81, 143, 206,  70, 156,
         88,  45, 201, 182, 180,  16, 215, 230,  50, 149, 203, 168, 220, 187,  41,  61,
        238, 208, 224, 106, 205, 222,  42,  68, 127, 210,  77, 129, 213,  15, 102, 146,
         54,  35,  91,  19, 199,  32, 139, 150, 196, 125,  53, 100, 113, 110,  71, 191,
         58, 242, 248,  13, 184, 163, 147,  79,  93, 229, 228, 186, 207,   1,  66,  33,
        121,  96, 123, 179, 235, 241, 109, 142,  44,  86, 195, 174,  87, 105,  51, 221
    };

    // Append the 256-byte addon public key in the scatter order the client's parser expects.
    // 'key' must point at 256 bytes of modulus in natural order.
    inline void AppendAddonPublicKey(ByteBuffer& out, uint8 const* key)
    {
        for (uint32 i = 0; i < 256; ++i)
        {
            out << uint8(key[kAddonKeyWireOrder[i]]);
        }
    }
}

/**
 * @brief Party operation enumeration
 */
enum PartyOperation
{
    PARTY_OP_INVITE = 0, ///< Invite to party
    PARTY_OP_LEAVE = 2,  ///< Leave party
    PARTY_OP_SWAP = 4
};

/**
 * @brief Party result enumeration
 */
enum PartyResult
{
    ERR_PARTY_RESULT_OK = 0,                     ///< Success
    ERR_BAD_PLAYER_NAME_S = 1,                   ///< Bad player name
    ERR_TARGET_NOT_IN_GROUP_S = 2,               ///< Target not in group
    ERR_TARGET_NOT_IN_INSTANCE_S = 3,            ///<
    ERR_GROUP_FULL = 4,                          ///< Group full
    ERR_ALREADY_IN_GROUP_S = 5,                  ///< Already in group
    ERR_NOT_IN_GROUP = 6,                        ///< Not in group
    ERR_NOT_LEADER = 7,                          ///< Not leader
    ERR_PLAYER_WRONG_FACTION = 8,                ///< Player wrong faction
    ERR_IGNORING_YOU_S = 9,                      ///< Ignoring you
    ERR_LFG_PENDING = 12,                        ///<
    ERR_INVITE_RESTRICTED = 13,                  ///<
    ERR_GROUP_SWAP_FAILED = 14,                  ///< if (PartyOperation == PARTY_OP_SWAP) ERR_GROUP_SWAP_FAILED else ERR_INVITE_IN_COMBAT
    ERR_INVITE_UNKNOWN_REALM = 15,
    ERR_INVITE_NO_PARTY_SERVER = 16,
    ERR_INVITE_PARTY_BUSY = 17,
    ERR_PARTY_TARGET_AMBIGUOUS = 18,
    ERR_PARTY_LFG_INVITE_RAID_LOCKED = 19,
    ERR_PARTY_LFG_BOOT_LIMIT = 20,
    ERR_PARTY_LFG_BOOT_COOLDOWN_S = 21,
    ERR_PARTY_LFG_BOOT_IN_PROGRESS = 22,
    ERR_PARTY_LFG_BOOT_TOO_FEW_PLAYERS = 23,
    ERR_PARTY_LFG_BOOT_NOT_ELIGIBLE_S = 24,
    ERR_RAID_DISALLOWED_BY_LEVEL = 25,
    ERR_PARTY_LFG_BOOT_IN_COMBAT = 26,
    ERR_VOTE_KICK_REASON_NEEDED = 27,
    ERR_PARTY_LFG_BOOT_DUNGEON_COMPLETE = 28,
    ERR_PARTY_LFG_BOOT_LOOT_ROLLS = 29,
    ERR_PARTY_LFG_TELEPORT_IN_COMBAT = 30
};

/*
 * these have been moved to LFGMgr.h for dev21
 * delete from here once all is good with the move
enum LfgUpdateType
{
    LFG_UPDATE_JOIN     = 5,
    LFG_UPDATE_LEAVE    = 7,
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
*/

enum ChatRestrictionType
{
    ERR_CHAT_RESTRICTED = 0,
    ERR_CHAT_THROTTLED  = 1,
    ERR_USER_SQUELCHED  = 2,
    ERR_YELL_RESTRICTED = 3
};

/**
 * @brief Tutorial data state enumeration
 */
enum TutorialDataState
{
    TUTORIALDATA_UNCHANGED = 0, ///< Tutorial data unchanged
    TUTORIALDATA_CHANGED = 1,   ///< Tutorial data changed
    TUTORIALDATA_NEW = 2        ///< New tutorial data
};

/**
 * @brief Packet filter class
 *
 * Class to deal with packet processing.
 * Allows to determine if next packet is safe to be processed.
 */
class PacketFilter
{
    public:
        /**
         * @brief Constructor
         * @param pSession World session
         */
        explicit PacketFilter(WorldSession* pSession) : m_pSession(pSession) {}

        /**
         * @brief Virtual destructor
         */
        virtual ~PacketFilter() {}

        /**
         * @brief Process packet
         * @param packet World packet to process
         * @return True if processed successfully
         */
        virtual bool Process(WorldPacket* /*packet*/)
        {
            return true;
        }

        /**
         * @brief Process logout
         * @return True if logout processed
         */
        virtual bool ProcessLogout() const
        {
            return true;
        }

    protected:
        WorldSession* const m_pSession;
};
/**
 * @brief Map session filter class
 *
 * Process only thread-safe packets in Map::Update().
 */
class MapSessionFilter : public PacketFilter
{
    public:
        /**
         * @brief Constructor
         * @param pSession World session
         */
        explicit MapSessionFilter(WorldSession* pSession) : PacketFilter(pSession) {}

        /**
         * @brief Destructor
         */
        ~MapSessionFilter() {}

        /**
         * @brief Process packet
         * @param packet World packet to process
         * @return True if processed successfully
         */
        bool Process(WorldPacket* packet) override;

        /**
         * @brief Process logout
         *
         * In Map::Update() we do not process player logout.
         *
         * @return False (logout not processed)
         */
        bool ProcessLogout() const override
        {
            return false;
        }
};

/**
 * @brief World session filter class
 *
 * Class used to filter only thread-unsafe packets from queue.
 * Used in World::UpdateSessions().
 */
class WorldSessionFilter : public PacketFilter
{
    public:
        /**
         * @brief Constructor
         * @param pSession World session
         */
        explicit WorldSessionFilter(WorldSession* pSession) : PacketFilter(pSession) {}

        /**
         * @brief Destructor
         */
        ~WorldSessionFilter() {}

        /**
         * @brief Process packet
         * @param packet World packet to process
         * @return True if processed successfully
         */
        bool Process(WorldPacket* packet) override;
};

/**
 * @brief World session class
 *
 * Player session in the World.
 */
class WorldSession
{
        friend class CharacterHandler;

    public:
        /**
         * @brief Constructor
         * @param id Session ID
         * @param link How to talk back to this client (proto::IClientLink; the
         *             transport underneath -- ClientConnection, the net::
         *             engine -- is opaque to WorldSession)
         * @param sec Account security level
         * @param mute_time Mute time
         * @param locale Locale
         * @param sessionKey The account's canonical raw-40 session key (K),
         *             carried in from WorldGateway::LookupAccount rather than
         *             re-read: SendRedirectClient()'s HMAC seed and (once
         *             re-enabled) Warden both need it, and re-deriving it a
         *             second time from the DB is how a stale/second read
         *             desyncs from the key proto already proved the client
         *             holds.
         */
        WorldSession(uint32 id, std::shared_ptr<proto::IClientLink> link,
                     AccountTypes sec, uint8 expansion, time_t mute_time, LocaleConstant locale,
                     const uint8 (&sessionKey)[MopAuth::SESSION_KEY_LEN]);

        /**
         * @brief Destructor
         */
        ~WorldSession();

        /**
         * @brief Check if player is loading
         * @return True if loading
         */
        bool PlayerLoading() const
        {
            return m_playerLoading;
        }

        /**
         * @brief Check if player is logging out
         * @return True if logging out
         */
        bool PlayerLogout() const
        {
            return m_playerLogout;
        }

        /**
         * @brief Check if player is logging out with save
         * @return True if logging out with save
         */
        bool PlayerLogoutWithSave() const
        {
            return m_playerLogout && m_playerSave;
        }


        void SizeError(WorldPacket const& packet, uint32 size) const;

        void ReadAddonsInfo(ByteBuffer &data);
        void SendAddonsInfo();

        void SendPacket(WorldPacket const* packet, bool bypassSuppress = false);
        void SendNotification(const char* format, ...) ATTR_PRINTF(2, 3);
        void SendNotification(int32 string_id, ...);
        // Eluna exposes this historical API name. Keep only a source-compatible
        // forwarding facade; the legacy opcode and packet backend no longer exist.
        template <typename... Args>
        void SendAreaTriggerMessage(char const* format, Args... args)
        {
            SendNotification(format, args...);
        }
        void SendPetNameInvalid(uint32 error, const std::string& name, DeclinedName* declinedName);
        void SendLfgJoinResult(LfgJoinResult result, uint8 detail, partyForbidden const& lockedDungeons);
        void SendLfgUpdate(bool fallbackIsGroup, LFGPlayerStatus status);
        void SendLfgQueueStatus(LFGQueueStatus const& status);
        void SendLfgPlayerLockInfo();
        void SendLfgPartyLockInfo();
        void SendLfgRoleCheckUpdate(LFGRoleCheck const& roleCheck);
        void SendLfgRoleChosen(uint64 rawGuid, uint8 roles);
        void SendLfgProposalUpdate(LFGProposal const& proposal);
        void SendLfgTeleportError(uint8 error);
        void SendLfgOfferContinue(uint32 dungeonEntry);
        void SendLfgRewards(LFGRewards const& rewards);
        void SendLfgBootUpdate(LFGBoot const& boot);
        void SendPartyResult(PartyOperation operation, const std::string& member, PartyResult res);
        void SendGroupInvite(Player* player, bool alreadyInGroup = false);
        void SendGuildInvite(Player* player, bool alreadyInGuild = false);
        void SendTransferAborted(uint32 mapid, uint8 reason, uint8 arg = 0);
        void SendTransferRoot(uint32 counter);
        void SendSuspendToken();
        void SendSetPhaseShift(uint32 phaseMask, uint16 mapId = 0);
        void SendQueryTimeResponse();
        void SendRedirectClient(std::string& ip, uint16 port);

        /// The canonical raw-40 session key (K). Replaces
        /// WorldSocket::GetSessionKeyRaw() now that the crypt/proof live in
        /// proto::ClientConnection, not on a socket WorldSession could reach
        /// into: WorldGateway::Attach() carries K in via the constructor, so
        /// this is a plain accessor rather than a round trip through the link.
        const uint8* GetSessionKeyRaw() const { return m_sessionKey; }

        AccountTypes GetSecurity() const
        {
            return _security;
        }
        uint32 GetAccountId() const
        {
            return _accountId;
        }
        Player* GetPlayer() const
        {
            return _player;
        }
        char const* GetPlayerName() const;
        void SetSecurity(AccountTypes security)
        {
            _security = security;
        }
        std::string const& GetRemoteAddress()
        {
            return m_Address;
        }
        void SetPlayer(Player* plr)
        {
            _player = plr;
        }
        uint8 Expansion() const { return m_expansion; }

        // Warden
        void InitWarden(uint16 build, BigNumber* k, std::string const& os);

        /// Session in auth.queue currently
        void SetInQueue(bool state)
        {
            m_inQueue = state;
        }

        /// Is the user engaged in a log out process?
        bool isLogingOut() const
        {
            return _logoutTime || m_playerLogout;
        }

        /// Engage the logout process for the user
        void LogoutRequest(time_t requestTime)
        {
            _logoutTime = requestTime;
        }

        /// Is logout cooldown expired?
        bool ShouldLogOut(time_t currTime) const
        {
            return (_logoutTime > 0 && currTime >= _logoutTime + 20);
        }

        void LogoutPlayer(bool Save);
        void KickPlayer();

        /// Declare the in-memory character state untrustworthy, so that NO
        /// route persists it: Player::SaveToDB refuses outright once this is
        /// set. For use when a transaction's durable outcome could not be
        /// established and saving memory could therefore create or destroy
        /// player property. Costs the unsaved progress since the last save,
        /// which is the cheaper error. Intended to be followed by KickPlayer().
        void SuppressCharacterSave() { m_suppressCharacterSave = true; }
        bool IsCharacterSaveSuppressed() const { return m_suppressCharacterSave; }

        void QueuePacket(WorldPacket* new_packet);

        bool Update(PacketFilter& updater);

        /// Sends SMSG_AUTH_RESPONSE for the login queue (see MopAuthResponse.h).
        void SendAuthWaitQue(uint32 position);

        void SendNameQueryOpcode(Player* p);
        void SendNameQueryOpcodeFromDB(ObjectGuid guid);
        static void SendNameQueryOpcodeFromDBCallBack(QueryResult* result,
            uint32 accountId, uint64 requestedGuid);

        void SendTrainerList(ObjectGuid guid);
        void SendTrainerList(ObjectGuid guid, const std::string& strTitle);

        void SendListInventory(ObjectGuid guid);
        bool CheckBanker(ObjectGuid guid);
        void SendShowBank(ObjectGuid guid);
        bool CheckMailBox(ObjectGuid guid);
        bool CheckOpenedMailBox();
        void SendShowMailBox(ObjectGuid guid);
        void SendTabardVendorActivate(ObjectGuid guid);
        void SendSpiritResurrect();
        void SendBindPoint(Creature* npc);
        void SendGMTicketGetTicket(uint32 status, GMTicket* ticket = NULL);

        void SendAttackStop(Unit const* enemy);

        void SendBattlegGroundList(ObjectGuid guid, BattleGroundTypeId bgTypeId);

        void SendTradeStatus(TradeStatus status);
        void SendTradeStatus(TradeStatus status, MopTradePackets::StatusData const& statusData);
        void SendUpdateTrade(bool trader_state = true);
        void SendCancelTrade();

        void SendPetitionQueryOpcode(ObjectGuid petitionguid);

        // pet
        void SendPetNameQuery(ObjectGuid guid, uint64 petnumber);
        void SendStablePet(ObjectGuid guid);
        void SendStableResult(uint8 res);
        bool CheckStableMaster(ObjectGuid guid);

        // Account Data
        AccountData* GetAccountData(AccountDataType type) { return &m_accountData[type]; }
        void SetAccountData(AccountDataType type, time_t time_, const std::string& data);
        void SendAccountDataTimes(uint32 mask);
        void LoadGlobalAccountData();
        void LoadAccountData(QueryResult* result, uint32 mask);
        void LoadTutorialsData();
        void SendTutorialsData();
        void SaveTutorialsData();
        uint32 GetTutorialInt(uint32 intId)
        {
            return m_Tutorials[intId];
        }

        void SetTutorialInt(uint32 intId, uint32 value)
        {
            if (m_Tutorials[intId] != value)
            {
                m_Tutorials[intId] = value;
                if (m_tutorialState == TUTORIALDATA_UNCHANGED)
                {
                    m_tutorialState = TUTORIALDATA_CHANGED;
                }
            }
        }
        // used with item_page table
        bool SendItemInfo(uint32 itemid, WorldPacket data);

        // auction
        void SendAuctionHello(Unit* unit);
        void SendAuctionCommandResult(AuctionEntry* auc, AuctionAction Action, AuctionError ErrorCode, InventoryResult invError = EQUIP_ERR_OK);
        void SendAuctionSoldNotification(AuctionEntry* auction);
        void SendAuctionWonNotification(AuctionEntry* auction);
        void SendAuctionOutbidNotification(AuctionEntry* auction, uint64 newBidderGuid, uint64 newBid);
        void SendAuctionBidUpdateNotification(AuctionEntry* auction);
        void SendAuctionExpiredNotification(AuctionEntry* auction);
        static void SendAuctionOutbiddedMail(AuctionEntry* auction, Player* newBidder, uint64 newBid);
        void SendAuctionCancelledToBidderMail(AuctionEntry* auction);
        void BuildListAuctionItems(std::vector<AuctionEntry*> const& auctions, WorldPacket& data, std::wstring const& searchedname, uint32 listfrom, uint32 levelmin,
                                   uint32 levelmax, uint32 usable, uint32 inventoryType, uint32 itemClass, uint32 itemSubClass, uint32 quality, uint32& count, uint32& totalcount, bool isFull);

        AuctionHouseEntry const* GetCheckedAuctionHouseForAuctioneer(ObjectGuid guid);

        // Item Enchantment
        void SendEnchantmentLog(ObjectGuid targetGuid, ObjectGuid casterGuid, uint32 itemId, uint32 enchantId);
        void SendItemEnchantTimeUpdate(ObjectGuid playerGuid, ObjectGuid itemGuid, uint32 slot, uint32 duration);

        // Taxi
        void SendTaxiStatus(ObjectGuid guid);
        void SendTaxiMenu(Creature* unit);
        bool SendDoFlight(uint32 mountDisplayId, uint32 path, uint32 pathNode = 0, bool preserveTaxiRoute = false);
        bool SendLearnNewTaxiNode(Creature* unit);
        void SendActivateTaxiReply(ActivateTaxiReply reply);

        // Guild/Arena Team
        void SendGuildCommandResult(uint32 typecmd, const std::string& str, uint32 cmdresult);
        void SendPetitionShowList(ObjectGuid guid);
        void SendSaveGuildEmblem(uint32 msg);

        void BuildPartyMemberStatsChangedPacket(Player* player, WorldPacket* data);

        void DoLootRelease(ObjectGuid lguid);

        // Account mute time
        time_t m_muteTime;

        // Locales
        LocaleConstant GetSessionDbcLocale() const
        {
            return m_sessionDbcLocale;
        }
        int GetSessionDbLocaleIndex() const
        {
            return m_sessionDbLocaleIndex;
        }
        const char* GetMangosString(int32 entry) const;

        uint32 GetLatency() const
        {
            return m_latency;
        }
        void SetLatency(uint32 latency)
        {
            m_latency = latency;
        }
        uint32 getDialogStatus(Player* pPlayer, Object* questgiver, uint32 defstatus);

        // Misc
        void SetClientTimeDelay(uint32 delay) { m_clientTimeDelay = delay; }
        void ResetClientTimeDelay() { m_clientTimeDelay = 0; }

    public:                                                 // opcodes handlers

        // opcodes handlers
        void Handle_NULL(WorldPacket& recvPacket);          // not used
        void Handle_EarlyProccess(WorldPacket& recvPacket); // STATUS_NEVER stub; these opcodes are fully owned by proto::ClientConnection and must never reach here
        void Handle_ServerSide(WorldPacket& recvPacket);    // sever side only, can't be accepted from client
        void Handle_Deprecated(WorldPacket& recvPacket);    // never used anymore by client

        void HandleCharEnumOpcode(WorldPacket& recvPacket);
        void SendCharacterEnum();
        void HandleCharDeleteOpcode(WorldPacket& recvPacket);
        void HandleCharCreateOpcode(WorldPacket& recvPacket);
        void HandlePlayerLoginOpcode(WorldPacket& recvPacket);
        void HandleCharEnum(QueryResult* result);
        void HandlePlayerLogin(LoginQueryHolder* holder);
        void HandleReorderCharactersOpcode(WorldPacket& recvPacket);

        // played time
        void HandlePlayedTime(WorldPacket& recvPacket);

        // new
        void HandleMoveUnRootAck(WorldPacket& recvPacket);
        void HandleMoveRootAck(WorldPacket& recvPacket);

        // new inspect
        void HandleInspectOpcode(WorldPacket& recvPacket);

        // new party stats
        void HandleInspectHonorStatsOpcode(WorldPacket& recvPacket);

        void HandleMoveWaterWalkAck(WorldPacket& recvPacket);
        void HandleFeatherFallAck(WorldPacket& recv_data);

        void HandleMoveHoverAck(WorldPacket& recv_data);

        void HandleMountSpecialAnimOpcode(WorldPacket& recvdata);

        // character view
        void HandleShowingHelmOpcode(WorldPacket& recv_data);
        void HandleShowingCloakOpcode(WorldPacket& recv_data);

        // repair
        void HandleRepairItemOpcode(WorldPacket& recvPacket);

        // Knockback
        void HandleMoveKnockBackAck(WorldPacket& recvPacket);
        void SendKnockBack(float angle, float horizontalSpeed, float verticalSpeed);

        void HandleMoveTeleportAckOpcode(WorldPacket& recvPacket);
        void HandleForceSpeedChangeAckOpcodes(WorldPacket& recv_data);

        void HandlePingOpcode(WorldPacket& recvPacket);
        void HandleAuthSessionOpcode(WorldPacket& recvPacket);
        void HandleRequestCemeteryListOpcode(WorldPacket& recv_data);
        void HandleRepopRequestOpcode(WorldPacket& recvPacket);
        void HandleAutostoreLootItemOpcode(WorldPacket& recvPacket);
        void HandleLootMoneyOpcode(WorldPacket& recvPacket);

        /**
        * Method which handles the loot Opcode sent by the client, happens when the player is actually looting the object.
        * It generates required loot on purpose.
        */
        void HandleLootOpcode(WorldPacket& recvPacket);

        /**
        * Method which handles the loot release opcode sent by the client, happens when the player has end looting the object.
        * It will take care of the looting state of the object depending on the case.
        */
        void HandleLootReleaseOpcode(WorldPacket& recvPacket);
        void HandleLootMasterGiveOpcode(WorldPacket& recvPacket);
        void HandleWhoOpcode(WorldPacket& recvPacket);
        void HandleLogoutRequestOpcode(WorldPacket& recvPacket);
        void HandlePlayerLogoutOpcode(WorldPacket& recvPacket);
        void HandleLogoutCancelOpcode(WorldPacket& recvPacket);

        void HandleGMTicketGetTicketOpcode(WorldPacket& recvPacket);
        void HandleGMTicketCreateOpcode(WorldPacket& recvPacket);
        void HandleGMTicketSystemStatusOpcode(WorldPacket& recvPacket);
        void HandleGMUpdateTicketStatusOpcode(WorldPacket& recvPacket);
        void SendGMTicketStatusUpdate(GMTicketStatus statusCode);
        void HandleGMTicketDeleteTicketOpcode(WorldPacket& recvPacket);
        void HandleGMTicketUpdateTextOpcode(WorldPacket& recvPacket);

        void HandleGMTicketSurveySubmitOpcode(WorldPacket& recvPacket);
        void HandleGMResponseResolveOpcode(WorldPacket& recv_data);

        void HandleTogglePvP(WorldPacket& recvPacket);

        void HandleZoneUpdateOpcode(WorldPacket& recvPacket);
        void HandleSetTargetOpcode(WorldPacket& recvPacket);
        void HandleSetSelectionOpcode(WorldPacket& recvPacket);
        void HandleStandStateChangeOpcode(WorldPacket& recvPacket);
        void HandleEmoteOpcode(WorldPacket& recvPacket);
        void HandleContactListOpcode(WorldPacket& recvPacket);
        void HandleAddFriendOpcode(WorldPacket& recvPacket);
        static void HandleAddFriendOpcodeCallBack(QueryResult* result, uint32 accountId, std::string friendNote);
        void HandleDelFriendOpcode(WorldPacket& recvPacket);
        void HandleAddIgnoreOpcode(WorldPacket& recvPacket);
        static void HandleAddIgnoreOpcodeCallBack(QueryResult* result, uint32 accountId);
        void HandleDelIgnoreOpcode(WorldPacket& recvPacket);
        void HandleSetContactNotesOpcode(WorldPacket& recvPacket);
        void HandleBugOpcode(WorldPacket& recvPacket);
        void HandleSetAmmoOpcode(WorldPacket& recvPacket);

        void HandleAreaTriggerOpcode(WorldPacket& recvPacket);

        void HandleSetFactionAtWarOpcode(WorldPacket& recv_data);
        void HandleSetWatchedFactionOpcode(WorldPacket& recv_data);
        void HandleSetFactionInactiveOpcode(WorldPacket& recv_data);
        void HandleRequestForcedReactionsOpcode(WorldPacket& recv_data);

        void HandleUpdateAccountData(WorldPacket& recvPacket);
        void HandleRequestAccountData(WorldPacket& recvPacket);
        void HandleSetActionButtonOpcode(WorldPacket& recvPacket);

        void HandleGameObjectUseOpcode(WorldPacket& recPacket);
        void HandleGameobjectReportUse(WorldPacket& recvPacket);

        void HandleNameQueryOpcode(WorldPacket& recvPacket);

        void HandleRealmNameQueryOpcode(WorldPacket& recvPacket);

        void HandleQueryTimeOpcode(WorldPacket& recvPacket);

        void HandleCreatureQueryOpcode(WorldPacket& recvPacket);

        void HandleGameObjectQueryOpcode(WorldPacket& recvPacket);

        // Movement Handler
        void HandleSuspendTokenResponse(WorldPacket& recvPacket);
        void HandleMoveWorldportAckOpcode(WorldPacket& recvPacket);
        void HandleMoveWorldportAckOpcode();                // for server-side calls

        void HandleMovementOpcodes(WorldPacket& recvPacket);
        void HandleSetActiveMoverOpcode(WorldPacket& recv_data);
        void HandleMoveNotActiveMoverOpcode(WorldPacket& recv_data);
        void HandleMoveTimeSkippedOpcode(WorldPacket& recv_data);

        void HandleDismissControlledVehicle(WorldPacket& recvPacket);
        void HandleRequestVehicleExit(WorldPacket& recvPacket);
        void HandleRequestVehicleSwitchSeat(WorldPacket& recvPacket);
        void HandleChangeSeatsOnControlledVehicle(WorldPacket& recvPacket);
        void HandleRequestVehiclePrevSeat(WorldPacket& recv_data);
        void HandleRequestVehicleNextSeat(WorldPacket& recv_data);
        void HandleRideVehicleInteract(WorldPacket& recvPacket);
        void HandleEjectPassenger(WorldPacket& recvPacket);

        void HandleRequestRaidInfoOpcode(WorldPacket& recv_data);

        void HandleGroupInviteOpcode(WorldPacket& recvPacket);
        void HandleGroupInviteResponseOpcode(WorldPacket& recvPacket);
        void HandleGroupUninviteGuidOpcode(WorldPacket& recvPacket);
        void HandleGroupSetLeaderOpcode(WorldPacket& recvPacket);
        void HandleGroupDisbandOpcode(WorldPacket& recvPacket);
        void HandleOptOutOfLootOpcode(WorldPacket& recv_data);
        void HandleSetAllowLowLevelRaidOpcode(WorldPacket& recv_data);
        void HandleLootMethodOpcode(WorldPacket& recvPacket);
        void HandleLootRoll(WorldPacket& recv_data);
        void HandleRequestPartyMemberStatsOpcode(WorldPacket& recv_data);
        void HandleRaidTargetUpdateOpcode(WorldPacket& recv_data);
        void HandleRaidReadyCheckOpcode(WorldPacket& recv_data);
        void HandleRaidReadyCheckConfirmOpcode(WorldPacket& recv_data);
        void HandleRaidReadyCheckFinishedOpcode(WorldPacket& recv_data);
        void HandleGroupRaidConvertOpcode(WorldPacket& recv_data);
        void HandleGroupRequestJoinUpdates(WorldPacket& recv_data);
        void HandleGroupChangeSubGroupOpcode(WorldPacket& recv_data);
        void HandleGroupAssistantLeaderOpcode(WorldPacket& recv_data);
        void HandleGroupEveryoneIsAssistantOpcode(WorldPacket& recv_data);
        void HandleGroupSetRolesOpcode(WorldPacket& recv_data);
        void HandleGroupInitiateRolePollOpcode(WorldPacket& recv_data);
        void HandlePartyAssignmentOpcode(WorldPacket& recv_data);

        void HandlePetitionBuyOpcode(WorldPacket& recv_data);
        void HandlePetitionShowSignOpcode(WorldPacket& recv_data);
        void HandlePetitionQueryOpcode(WorldPacket& recv_data);
        void HandlePetitionSignOpcode(WorldPacket& recv_data);
        void HandleOfferPetitionOpcode(WorldPacket& recv_data);
        void HandleTurnInPetitionOpcode(WorldPacket& recv_data);

        void HandleGuildQueryOpcode(WorldPacket& recvPacket);
        void HandleGuildCreateOpcode(WorldPacket& recvPacket);
        void HandleGuildInviteOpcode(WorldPacket& recvPacket);
        void HandleGuildRemoveOpcode(WorldPacket& recvPacket);
        void HandleGuildAcceptOpcode(WorldPacket& recvPacket);
        void HandleGuildDeclineOpcode(WorldPacket& recvPacket);
        void HandleGuildEventLogQueryOpcode(WorldPacket& recvPacket);
        void HandleGuildRosterOpcode(WorldPacket& recvPacket);
        void HandleGuildPromoteOpcode(WorldPacket& recvPacket);
        void HandleGuildDemoteOpcode(WorldPacket& recvPacket);
        void HandleGuildSetRankOpcode(WorldPacket& recvPacket);
        void HandleGuildSwitchRankOpcode(WorldPacket& recvPacket);
        void HandleGuildLeaveOpcode(WorldPacket& recvPacket);
        void HandleGuildDisbandOpcode(WorldPacket& recvPacket);
        void HandleGuildLeaderOpcode(WorldPacket& recvPacket);
        void HandleGuildMOTDOpcode(WorldPacket& recvPacket);
        void HandleGuildSetNoteOpcode(WorldPacket& recvPacket);
        void HandleGuildRankOpcode(WorldPacket& recvPacket);
        void HandleGuildAddRankOpcode(WorldPacket& recvPacket);
        void HandleGuildDelRankOpcode(WorldPacket& recvPacket);
        void HandleGuildChangeInfoTextOpcode(WorldPacket& recvPacket);
        void HandleSaveGuildEmblemOpcode(WorldPacket& recvPacket);
        void HandleGuildQueryRanksOpcode(WorldPacket& recvPacket);
        void HandleGuildSetAchievementTracking(WorldPacket& recvPacket);
        void HandleGuildAutoDeclineToggleOpcode(WorldPacket& recvPacket);

        void HandleTaxiNodeStatusQueryOpcode(WorldPacket& recvPacket);
        void HandleTaxiQueryAvailableNodes(WorldPacket& recvPacket);
        void HandleActivateTaxiOpcode(WorldPacket& recvPacket);
        void HandleActivateTaxiExpressOpcode(WorldPacket& recvPacket);
        void HandleMoveSplineDoneOpcode(WorldPacket& recvPacket);

        void HandleTabardVendorActivateOpcode(WorldPacket& recvPacket);
        void HandleBankerActivateOpcode(WorldPacket& recvPacket);
        void HandleBuyBankSlotOpcode(WorldPacket& recvPacket);
        void HandleTrainerListOpcode(WorldPacket& recvPacket);
        void HandleTrainerBuySpellOpcode(WorldPacket& recvPacket);

        void HandlePetitionShowListOpcode(WorldPacket& recvPacket);
        void HandleGossipHelloOpcode(WorldPacket& recvPacket);
        void HandleGossipSelectOptionOpcode(WorldPacket& recvPacket);
        void HandleSpiritHealerActivateOpcode(WorldPacket& recvPacket);
        void HandleReturnToGraveyardOpcode(WorldPacket& recvPacket);
        void HandleNpcTextQueryOpcode(WorldPacket& recvPacket);
        void HandleBinderActivateOpcode(WorldPacket& recvPacket);
        void HandleListStabledPetsOpcode(WorldPacket& recvPacket);
        void HandleStablePet(WorldPacket& recvPacket);
        void HandleUnstablePet(WorldPacket& recvPacket);
        void HandleBuyStableSlot(WorldPacket& recvPacket);
        void HandleStableRevivePet(WorldPacket& recvPacket);
        void HandleStableSwapPet(WorldPacket& recvPacket);

        void HandleDuelAcceptedOpcode(WorldPacket& recvPacket);
        void HandleDuelCancelledOpcode(WorldPacket& recvPacket);

        void HandleAcceptTradeOpcode(WorldPacket& recvPacket);
        void HandleBeginTradeOpcode(WorldPacket& recvPacket);
        void HandleBusyTradeOpcode(WorldPacket& recvPacket);
        void HandleCancelTradeOpcode(WorldPacket& recvPacket);
        void HandleClearTradeItemOpcode(WorldPacket& recvPacket);
        void HandleIgnoreTradeOpcode(WorldPacket& recvPacket);
        void HandleInitiateTradeOpcode(WorldPacket& recvPacket);
        void HandleSetTradeGoldOpcode(WorldPacket& recvPacket);
        void HandleSetTradeItemOpcode(WorldPacket& recvPacket);
        void HandleUnacceptTradeOpcode(WorldPacket& recvPacket);

        void HandleAuctionHelloOpcode(WorldPacket& recvPacket);
        void HandleAuctionListItems(WorldPacket& recv_data);
        void HandleAuctionListBidderItems(WorldPacket& recv_data);
        void HandleAuctionSellItem(WorldPacket& recv_data);

        void HandleAuctionRemoveItem(WorldPacket& recv_data);
        void HandleAuctionListOwnerItems(WorldPacket& recv_data);
        void HandleAuctionPlaceBid(WorldPacket& recv_data);

        void AuctionBind(uint32 price, AuctionEntry * auction, Player * pl, Player* auction_owner);
        void HandleAuctionListPendingSales(WorldPacket& recv_data);

        void HandleGetMailList(WorldPacket& recv_data);
        void HandleSendMail(WorldPacket& recv_data);
        void HandleMailTakeMoney(WorldPacket& recv_data);
        void HandleMailTakeItem(WorldPacket& recv_data);
        void HandleMailMarkAsRead(WorldPacket& recv_data);
        void HandleMailReturnToSender(WorldPacket& recv_data);
        void HandleMailDelete(WorldPacket& recv_data);
        void HandleItemTextQuery(WorldPacket& recv_data);
        void HandleMailCreateTextItem(WorldPacket& recv_data);
        void HandleQueryNextMailTime(WorldPacket& recv_data);
        void HandleCancelChanneling(WorldPacket& recv_data);

        void SendItemPageInfo(ItemPrototype* itemProto);
        void HandleSplitItemOpcode(WorldPacket& recvPacket);
        void HandleSwapInvItemOpcode(WorldPacket& recvPacket);
        void HandleDestroyItemOpcode(WorldPacket& recvPacket);
        void HandleAutoEquipItemOpcode(WorldPacket& recvPacket);
        void HandleSellItemOpcode(WorldPacket& recvPacket);
        void HandleBuyItemOpcode(WorldPacket& recvPacket);
        void HandleListInventoryOpcode(WorldPacket& recvPacket);
        void HandleAutoStoreBagItemOpcode(WorldPacket& recvPacket);
        void HandleReadItemOpcode(WorldPacket& recvPacket);
        void HandleAutoEquipItemSlotOpcode(WorldPacket& recvPacket);
        void HandleSwapItem(WorldPacket& recvPacket);
        void HandleBuybackItem(WorldPacket& recvPacket);
        void HandleAutoBankItemOpcode(WorldPacket& recvPacket);
        void HandleAutoStoreBankItemOpcode(WorldPacket& recvPacket);
        void HandleWrapItemOpcode(WorldPacket& recvPacket);

        void HandleAttackSwingOpcode(WorldPacket& recvPacket);
        void HandleAttackStopOpcode(WorldPacket& recvPacket);
        void HandleSetSheathedOpcode(WorldPacket& recvPacket);

        void HandleUseItemOpcode(WorldPacket& recvPacket);
        void HandleOpenItemOpcode(WorldPacket& recvPacket);
        void HandleCastSpellOpcode(WorldPacket& recvPacket);
        void HandleRequestCategoryCooldowns(WorldPacket& recvPacket);
        void HandleCancelCastOpcode(WorldPacket& recvPacket);
        void HandleCancelAuraOpcode(WorldPacket& recvPacket);
        void HandleCancelGrowthAuraOpcode(WorldPacket& recvPacket);
        void HandleCancelAutoRepeatSpellOpcode(WorldPacket& recvPacket);

        void HandleLearnTalentOpcode(WorldPacket& recvPacket);
        void HandleLearnPreviewTalents(WorldPacket& recvPacket);
        void HandleTalentWipeConfirmOpcode(WorldPacket& recvPacket);
        void HandleUnlearnSkillOpcode(WorldPacket& recvPacket);

        void HandleQuestgiverStatusQueryOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverStatusMultipleQuery(WorldPacket& recvPacket);
        void HandleQuestgiverHelloOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverAcceptQuestOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverQueryQuestOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverChooseRewardOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverRequestRewardOpcode(WorldPacket& recvPacket);
        void HandleQuestQueryOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverCancel(WorldPacket& recv_data);
        void HandleQuestLogSwapQuest(WorldPacket& recv_data);
        void HandleQuestLogRemoveQuest(WorldPacket& recv_data);
        void HandleQuestConfirmAccept(WorldPacket& recv_data);
        void HandleQuestgiverCompleteQuest(WorldPacket& recv_data);
        bool CanInteractWithQuestGiver(ObjectGuid guid, char const* descr);

        void HandleQuestgiverQuestAutoLaunch(WorldPacket& recvPacket);
        void HandlePushQuestToParty(WorldPacket& recvPacket);
        void HandleQuestPushResult(WorldPacket& recvPacket);

        bool processChatmessageFurtherAfterSecurityChecks(std::string&, uint32);
        void SendPlayerNotFoundNotice(const std::string& name);
        void SendPlayerAmbiguousNotice(const std::string& name);
        void SendChatRestrictedNotice(ChatRestrictionType restriction);
        void HandleMessagechatOpcode(WorldPacket& recvPacket);
        void HandleAddonMessagechatOpcode(WorldPacket& recvPacket);
        void HandleUnregisterAddonPrefixesOpcode(WorldPacket& recvPacket);
        void HandleAddonRegisteredPrefixesOpcode(WorldPacket& recvPacket);
        bool IsAddonRegistered(std::string const& prefix) const;
        void HandleTextEmoteOpcode(WorldPacket& recvPacket);
        void HandleChatIgnoredOpcode(WorldPacket& recvPacket);

        void HandleReclaimCorpseOpcode(WorldPacket& recvPacket);
        void HandleCorpseQueryOpcode(WorldPacket& recvPacket);
        void HandleCorpseMapPositionQueryOpcode(WorldPacket& recvPacket);
        void HandleResurrectResponseOpcode(WorldPacket& recvPacket);
        void HandleReturnToGraveyard(WorldPacket& recvPacket);
        void HandleSummonResponseOpcode(WorldPacket& recv_data);

        void HandleJoinChannelOpcode(WorldPacket& recvPacket);
        void HandleLeaveChannelOpcode(WorldPacket& recvPacket);
        void HandleChannelListOpcode(WorldPacket& recvPacket);
        void HandleChannelPasswordOpcode(WorldPacket& recvPacket);
        void HandleChannelSetOwnerOpcode(WorldPacket& recvPacket);
        void HandleChannelOwnerOpcode(WorldPacket& recvPacket);
        void HandleChannelModeratorOpcode(WorldPacket& recvPacket);
        void HandleChannelUnmoderatorOpcode(WorldPacket& recvPacket);
        void HandleChannelMuteOpcode(WorldPacket& recvPacket);
        void HandleChannelUnmuteOpcode(WorldPacket& recvPacket);
        void HandleChannelInviteOpcode(WorldPacket& recvPacket);
        void HandleChannelKickOpcode(WorldPacket& recvPacket);
        void HandleChannelBanOpcode(WorldPacket& recvPacket);
        void HandleChannelUnbanOpcode(WorldPacket& recvPacket);
        void HandleChannelAnnouncementsOpcode(WorldPacket& recvPacket);
        void HandleChannelModerateOpcode(WorldPacket& recvPacket);
        void HandleChannelDisplayListQueryOpcode(WorldPacket& recvPacket);
        void HandleSetChannelWatchOpcode(WorldPacket& recvPacket);

        void HandleCompleteCinematic(WorldPacket& recvPacket);
        void HandleNextCinematicCamera(WorldPacket& recvPacket);

        void HandlePageTextQueryOpcode(WorldPacket& recvPacket);

        void HandleTutorialFlagOpcode(WorldPacket& recv_data);
        void HandleTutorialClearOpcode(WorldPacket& recv_data);
        void HandleTutorialResetOpcode(WorldPacket& recv_data);

        // Pet
        void HandleBattlePetRequestJournal(WorldPacket& recv_data);
        void HandlePetAction(WorldPacket& recv_data);
        void HandlePetStopAttack(WorldPacket& recv_data);
        void HandlePetNameQueryOpcode(WorldPacket& recv_data);
        void HandlePetSetAction(WorldPacket& recv_data);
        void HandlePetAbandon(WorldPacket& recv_data);
        void HandlePetRename(WorldPacket& recv_data);
        void HandlePetCancelAuraOpcode(WorldPacket& recvPacket);
        void HandlePetSpellAutocastOpcode(WorldPacket& recvPacket);
        void HandlePetCastSpellOpcode(WorldPacket& recvPacket);
        void HandlePetLearnTalent(WorldPacket& recvPacket);
        void HandleLearnPreviewTalentsPet(WorldPacket& recvPacket);
        void HandleDismissCritter(WorldPacket& recvData);

        void HandleSetActionBarTogglesOpcode(WorldPacket& recv_data);
        void HandleViolenceLevelOpcode(WorldPacket& recv_data);

        void HandleCharRenameOpcode(WorldPacket& recv_data);
        static void HandleChangePlayerNameOpcodeCallBack(QueryResult* result, uint32 accountId, std::string newname);
        void HandleSetPlayerDeclinedNamesOpcode(WorldPacket& recv_data);

        void HandleTotemDestroyed(WorldPacket& recv_data);

        // BattleGround
        void HandleBattlemasterHelloOpcode(WorldPacket& recv_data);
        void HandleBattlemasterJoinOpcode(WorldPacket& recv_data);
        void HandleBattleGroundPlayerPositionsOpcode(WorldPacket& recv_data);
        void HandlePVPLogDataOpcode(WorldPacket& recv_data);
        void HandleBattlefieldStatusOpcode(WorldPacket& recv_data);
        void HandleQueryCountdownTimerOpcode(WorldPacket& recv_data);
        void HandleBattleFieldPortOpcode(WorldPacket& recv_data);
        void HandleBattlefieldListOpcode(WorldPacket& recv_data);
        void HandleLeaveBattlefieldOpcode(WorldPacket& recv_data);
        void HandleReportPvPAFK(WorldPacket& recv_data);
        void HandleRequestPvPOptionsEnabledOpcode(WorldPacket& recv_data);
        void HandleRequestPvPRewardsOpcode(WorldPacket& recv_data);
        void HandleRequestRatedBGStatsOpcode(WorldPacket& recv_data);
        void HandleRequestConquestFormulaConstantsOpcode(WorldPacket& recv_data);

        void HandleWardenDataOpcode(WorldPacket& recv_data);
        void HandleWorldTeleportOpcode(WorldPacket& recv_data);
        void HandleMinimapPingOpcode(WorldPacket& recv_data);
        void HandleRandomRollOpcode(WorldPacket& recv_data);
        void HandleFarSightOpcode(WorldPacket& recv_data);
        void HandleSetDungeonDifficultyOpcode(WorldPacket& recv_data);
        void HandleSetRaidDifficultyOpcode(WorldPacket& recv_data);
        void HandleMoveSetCanFlyAckOpcode(WorldPacket& recv_data);
        void HandleLfrJoinOpcode(WorldPacket& recv_data);
        void HandleLfrLeaveOpcode(WorldPacket& recv_data);
        void HandleLfgJoinOpcode(WorldPacket& recv_data);
        void HandleLfgLeaveOpcode(WorldPacket& recv_data);
        void HandleLfgSetRolesOpcode(WorldPacket& recv_data);
        void HandleLfgProposalResponseOpcode(WorldPacket& recv_data);
        void HandleLfgGetStatusOpcode(WorldPacket& recv_data);
        void HandleLfgTeleportOpcode(WorldPacket& recv_data);
        void HandleLfgBootPlayerVoteOpcode(WorldPacket& recv_data);
        void HandleLfgLockInfoRequestOpcode(WorldPacket& recv_data);
        void HandleSetLfgCommentOpcode(WorldPacket& recv_data);
        void HandleSetTitleOpcode(WorldPacket& recv_data);
        void HandleRealmSplitOpcode(WorldPacket& recv_data);
        void HandleTimeSyncResp(WorldPacket& recv_data);
        void HandleTimeSyncResponseFailed(WorldPacket& recv_data);
        void HandleTimeSyncResponseDropped(WorldPacket& recv_data);
        void HandleDiscardedTimeSyncAcks(WorldPacket& recv_data);
        void HandleWhoisOpcode(WorldPacket& recv_data);
        void HandleResetInstancesOpcode(WorldPacket& recv_data);
        void HandleHearthandResurrect(WorldPacket& recv_data);

        void HandleAreaSpiritHealerQueryOpcode(WorldPacket& recv_data);
        void HandleAreaSpiritHealerQueueOpcode(WorldPacket& recv_data);
        void HandleCancelMountAuraOpcode(WorldPacket& recv_data);
        void HandleSelfResOpcode(WorldPacket& recv_data);
        void HandleComplainOpcode(WorldPacket& recv_data);
        void HandleRequestPetInfoOpcode(WorldPacket& recv_data);

        // Socket gem
        void HandleSocketOpcode(WorldPacket& recv_data);

        void HandleCancelTempEnchantmentOpcode(WorldPacket& recv_data);
        void HandleItemRefundInfoRequest(WorldPacket& recv_data);

        void HandleChannelVoiceOnOpcode(WorldPacket& recv_data);
        void HandleVoiceSessionEnableOpcode(WorldPacket& recv_data);
        void HandleSetActiveVoiceChannel(WorldPacket& recv_data);
        void HandleSetTaxiBenchmarkOpcode(WorldPacket& recv_data);

#ifdef ENABLE_PLAYERBOTS
        void HandleBotPackets();
#endif

        // for Warden
        uint16 GetClientBuild() const { return _build; }

        // Guild Bank
        void HandleGuildPermissions(WorldPacket& recv_data);
        void HandleGuildBankMoneyWithdrawn(WorldPacket& recv_data);
        void HandleGuildBankerActivate(WorldPacket& recv_data);
        void HandleGuildBankQueryTab(WorldPacket& recv_data);
        void HandleGuildBankLogQuery(WorldPacket& recv_data);
        void HandleGuildBankDepositMoney(WorldPacket& recv_data);
        void HandleGuildBankWithdrawMoney(WorldPacket& recv_data);
        void HandleGuildBankSwapItems(WorldPacket& recv_data);
        void HandleGuildBankUpdateTab(WorldPacket& recv_data);
        void HandleGuildBankBuyTab(WorldPacket& recv_data);
        void HandleQueryGuildBankTabText(WorldPacket& recv_data);
        void HandleSetGuildBankTabText(WorldPacket& recv_data);

        // Calendar
        void HandleCalendarGetCalendar(WorldPacket& recv_data);
        void HandleCalendarGetEvent(WorldPacket& recv_data);
        void HandleCalendarGuildFilter(WorldPacket& recv_data);
        void HandleCalendarEventSignup(WorldPacket& recvData);
        void HandleCalendarAddEvent(WorldPacket& recv_data);
        void HandleCalendarUpdateEvent(WorldPacket& recv_data);
        void HandleCalendarRemoveEvent(WorldPacket& recv_data);
        void HandleCalendarCopyEvent(WorldPacket& recv_data);
        void HandleCalendarEventInvite(WorldPacket& recv_data);
        void HandleCalendarEventRsvp(WorldPacket& recv_data);
        void HandleCalendarEventRemoveInvite(WorldPacket& recv_data);
        void HandleCalendarEventStatus(WorldPacket& recv_data);
        void HandleCalendarEventModeratorStatus(WorldPacket& recv_data);
        void HandleCalendarComplain(WorldPacket& recv_data);
        void HandleCalendarGetNumPending(WorldPacket& recv_data);

        // Hotfix handlers
        void HandleRequestHotfix(WorldPacket& recv_data);
        void SendItemDb2Reply(uint32 entry);
        void SendItemSparseDb2Reply(uint32 entry);
        void SendBroadcastTextDb2Reply(uint32 entry);
        void SendBattlePetEffectPropertiesDb2Reply(uint32 entry);

        void HandleObjectUpdateFailedOpcode(WorldPacket& recv_data);

        void HandleSpellClick(WorldPacket& recv_data);
        void HandleAlterAppearanceOpcode(WorldPacket& recv_data);
        void HandleRemoveGlyphOpcode(WorldPacket& recv_data);
        void HandleCharCustomizeOpcode(WorldPacket& recv_data);
        void HandleQueryInspectAchievementsOpcode(WorldPacket& recv_data);
        void HandleEquipmentSetSaveOpcode(WorldPacket& recv_data);
        void HandleEquipmentSetDeleteOpcode(WorldPacket& recv_data);
        void HandleEquipmentSetUseOpcode(WorldPacket& recv_data);
        void HandleUITimeRequestOpcode(WorldPacket& recv_data);
        void HandleReadyForAccountDataTimesOpcode(WorldPacket& recv_data);
        void HandleBattlePayGetPurchaseListOpcode(WorldPacket& recvPacket);
        void HandleBattlePayGetProductListOpcode(WorldPacket& recvPacket);
        void HandleRandomizeCharNameOpcode(WorldPacket& recvPacket);
        void HandleQuestPOIQueryOpcode(WorldPacket& recv_data);
        void HandleQuestNpcQueryOpcode(WorldPacket& recv_data);
        void HandleSetCurrencyFlagsOpcode(WorldPacket& recv_data);

        // Reforge
        void HandleReforgeItemOpcode(WorldPacket& recvData);
        void SendReforgeResult(bool success);

        void HandleLoadScreenOpcode(WorldPacket& recvPacket);
    private:
        friend class WorldGateway;

        /// Drop the session's reference to its link without closing it. Valid only before the
        /// session has been published to World: the connection must survive to deliver an
        /// auth-error response, which happens through proto::ClientConnection after
        /// WorldGateway::Attach() returns INVALID_SESSION_ID. Replaces the ACE-era
        /// AbandonUnpublishedSocket(), which had to release an extra AddReference() taken by
        /// the constructor; a shared_ptr needs no such bookkeeping -- resetting this session's
        /// own copy is the whole of it.
        void AbandonUnpublishedLink() noexcept;

        // private trade methods
        void moveItems(Item* myItems[], Item* hisItems[]);
        bool VerifyMovementInfo(MovementInfo const& movementInfo, ObjectGuid const& guid) const;
        bool VerifyMovementInfo(MovementInfo const& movementInfo) const;
        void HandleMoverRelocation(MovementInfo& movementInfo);

        void ExecuteOpcode(OpcodeHandler const& opHandle, WorldPacket* packet);

        // logging helper
        void LogUnexpectedOpcode(WorldPacket* packet, const char* reason);
        void LogUnprocessedTail(WorldPacket* packet);

        uint32 m_GUIDLow;                                   // set logged or recently logout player (while m_playerRecentlyLogout set)
        Player* _player;
        std::shared_ptr<proto::IClientLink> m_Socket;
        std::string m_Address;

        /// Canonical raw-40 session key (K); see GetSessionKeyRaw()'s doc comment.
        uint8 m_sessionKey[MopAuth::SESSION_KEY_LEN];

        AccountTypes _security;
        uint32 _accountId;
        uint8 m_expansion;

        // Warden
        Warden* _warden;                                    // Remains NULL if Warden system is not enabled by config
        uint16 _build;                                      // connected client build

        time_t _logoutTime;
        uint32 m_pendingTransferRootCounter;
        uint32 m_suspendTokenCounter;
        uint32 m_pendingSuspendToken;
        bool m_waitingForTransferRootAck;
        bool m_waitingForSuspendToken;
        bool m_inQueue;                                     // session wait in auth.queue
        bool m_playerLoading;                               // code processed in LoginPlayer
        bool m_suppressWorldSends;                          // PHASE 6c: silence Cata-format sends after enter-world (MoP port scaffold)
        bool m_playerLogout;                                // code processed in LogoutPlayer
        bool m_playerRecentlyLogout;
        bool m_playerSave;                                  // code processed in LogoutPlayer with save request
        bool m_suppressCharacterSave;                       // set by SuppressCharacterSave(); makes Player::SaveToDB refuse
        LocaleConstant m_sessionDbcLocale;
        int m_sessionDbLocaleIndex;
        uint32 m_latency;
        uint32 m_clientTimeDelay;
        ObjectGuid m_openMailboxGuid;
        AccountData m_accountData[NUM_ACCOUNT_DATA_TYPES];
        uint32 m_Tutorials[8];
        TutorialDataState m_tutorialState;
        AddonsList m_addonsList;
        std::vector<std::string> m_registeredAddonPrefixes;
        bool m_filterAddonMessages = true;
        MaNGOS::LockedQueue<WorldPacket*> _recvQueue;
};
#endif
/// @}
