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

#include "Common.h"
#include "DBCStores.h"
#include "Language.h"
#include "Database/DatabaseEnv.h"
#include "Database/DatabaseImpl.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "Log.h"
#include "Player.h"
#include "World.h"
#include "CinematicFlyover.h"
#include "GuildMgr.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "Auth/BigNumber.h"
#include "Auth/Sha1.h"
#include "UpdateData.h"
#include "LootMgr.h"
#include "Chat.h"
#include "ScriptMgr.h"
#include "zlib.h"
#include "ObjectAccessor.h"
#include "Object.h"
#include "BattleGround/BattleGround.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "Guild.h"
#include "Pet.h"
#include "SocialMgr.h"
#include "DBCEnums.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

/**
 * @file MiscHandlerInstance.cpp
 * @brief Cohesion split of MiscHandler.cpp -- instance reset and dungeon/raid difficulty opcode handlers. Same WorldSession class; no behaviour change. CMake file(GLOB) picks this file up automatically; WorldSession.h is unchanged.
 */

/**
 * @brief Resets the player's or group's saved instances.
 *
 * @param recv_data The received opcode packet.
 */
void WorldSession::HandleResetInstancesOpcode(WorldPacket& /*recv_data*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_RESET_INSTANCES");

    if (Group* pGroup = _player->GetGroup())
    {
        if (pGroup->IsLeader(_player->GetObjectGuid()))
        {
            pGroup->ResetInstances(INSTANCE_RESET_ALL, false, _player);
        }
    }
    else
    {
        _player->ResetInstances(INSTANCE_RESET_ALL, false);
    }
}

void WorldSession::HandleSetDungeonDifficultyOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_SET_DUNGEON_DIFFICULTY");

    // The client sends a RAW DifficultyID, not an internal mode.
    //
    // This read the value and used it as a Difficulty directly, so Normal (raw 1) would
    // have stored HEROIC, Heroic (raw 2) would have stored CHALLENGE, and Challenge
    // (raw 8) would have been rejected outright, since MAX_DUNGEON_DIFFICULTY is 3.
    // The registered handler must translate before touching saved binds; casting here
    // would silently invert the setting.
    uint32 clientDifficultyId;
    recv_data >> clientDifficultyId;

    // Checked against the DUNGEON key space specifically. A bare ToInternalDifficulty plus
    // a range test is not enough: raw 5 is 10-player heroic RAID, translates to internal 2,
    // passes `< MAX_DUNGEON_DIFFICULTY` and would set DUNGEON_DIFFICULTY_CHALLENGE -- which
    // is deliberately unreachable, since no spawn on a challenge map carries bit 2.
    int32 const internalMode = ToInternalDifficultyChecked(clientDifficultyId, false);
    if (internalMode < 0 || internalMode >= MAX_DUNGEON_DIFFICULTY)
    {
        sLog.outError("WorldSession::HandleSetDungeonDifficultyOpcode: player %d sent client difficulty %u, which is not a dungeon tier!",
                      _player->GetGUIDLow(), clientDifficultyId);
        return;
    }

    uint32 const mode = uint32(internalMode);

    if (Difficulty(mode) == _player->GetDungeonDifficulty())
    {
        return;
    }

    // cannot reset while in an instance
    Map* map = _player->GetMap();
    if (map && map->IsDungeon())
    {
        sLog.outError("WorldSession::HandleSetDungeonDifficultyOpcode: player %d tried to reset the instance while inside!", _player->GetGUIDLow());
        return;
    }

    // Exception to set mode to normal for low-level players
    if (_player->getLevel() < LEVELREQUIREMENT_HEROIC && mode > REGULAR_DIFFICULTY)
    {
        return;
    }

    if (Group* pGroup = _player->GetGroup())
    {
        if (pGroup->IsLeader(_player->GetObjectGuid()))
        {
            // the difficulty is set even if the instances can't be reset
            //_player->SendDungeonDifficulty(true);
            pGroup->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, false, _player);
            pGroup->SetDungeonDifficulty(Difficulty(mode));
        }
    }
    else
    {
        _player->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, false);
        _player->SetDungeonDifficulty(Difficulty(mode));
    }
}

void WorldSession::HandleSetRaidDifficultyOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_SET_RAID_DIFFICULTY");

    // RAW DifficultyID from the client, as with the dungeon handler above.
    uint32 clientDifficultyId;
    recv_data >> clientDifficultyId;

    // Checked against the RAID key space, for the mirror reason: raw 2 is 5-man heroic and
    // would otherwise translate to internal 1 and be accepted as a 25-player normal raid.
    int32 const internalMode = ToInternalDifficultyChecked(clientDifficultyId, true);
    if (internalMode < 0 || internalMode >= MAX_RAID_DIFFICULTY)
    {
        sLog.outError("WorldSession::HandleSetRaidDifficultyOpcode: player %d sent client difficulty %u, which is not a raid tier!",
                      _player->GetGUIDLow(), clientDifficultyId);
        return;
    }

    uint32 const mode = uint32(internalMode);

    if (Difficulty(mode) == _player->GetRaidDifficulty())
    {
        return;
    }

    // cannot reset while in an instance
    Map* map = _player->GetMap();
    if (map && map->IsDungeon())
    {
        sLog.outError("WorldSession::HandleSetRaidDifficultyOpcode: player %d tried to reset the instance while inside!", _player->GetGUIDLow());
        return;
    }

    // Exception to set mode to normal for low-level players
    if (_player->getLevel() < LEVELREQUIREMENT_HEROIC && mode > REGULAR_DIFFICULTY)
    {
        return;
    }

    if (Group* pGroup = _player->GetGroup())
    {
        if (pGroup->IsLeader(_player->GetObjectGuid()))
        {
            // the difficulty is set even if the instances can't be reset
            _player->SendDungeonDifficulty(true);
            pGroup->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, true, _player);
            pGroup->SetRaidDifficulty(Difficulty(mode));
        }
    }
    else
    {
        _player->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, true);
        _player->SetRaidDifficulty(Difficulty(mode));
    }
}
