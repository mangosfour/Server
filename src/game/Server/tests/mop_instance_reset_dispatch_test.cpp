/*
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

#include "Opcodes.h"
#include "Player.h"
#include "WorldSession.h"
#include "Database/DatabaseEnv.h"

#include <cstdio>
#include <cstring>

// The server process owns these globals. Referencing the real opcode table pulls
// handler translation units from game.lib, so this standalone fixture supplies
// the same inert linker stubs used by the other session-level packet tests.
DatabaseType WorldDatabase;
DatabaseType CharacterDatabase;
DatabaseType LoginDatabase;
uint32 realmID = 0;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void CheckHandler(uint16 opcode,
                         void (WorldSession::*expected)(WorldPacket&))
{
    OpcodeHandler const* handler = LookupClientOpcode(opcode);
    CHECK(handler != nullptr);
    if (handler)
    {
        CHECK(handler->status == STATUS_LOGGEDIN);
        CHECK(handler->packetProcessing == PROCESS_THREADUNSAFE);
        CHECK(handler->handler == expected);
    }
}

int main(int /*argc*/, char** /*argv*/)
{
    InitializeOpcodes();

    CheckHandler(CMSG_SET_DUNGEON_DIFFICULTY,
                 &WorldSession::HandleSetDungeonDifficultyOpcode);
    CheckHandler(CMSG_SET_RAID_DIFFICULTY,
                 &WorldSession::HandleSetRaidDifficultyOpcode);
    CheckHandler(CMSG_RESET_INSTANCES,
                 &WorldSession::HandleResetInstancesOpcode);

    CHECK(MopCompactPackets::IsInstanceResetResult(SMSG_INSTANCE_RESET));
    CHECK(MopCompactPackets::IsInstanceResetResult(SMSG_INSTANCE_RESET_FAILED));
    CHECK(MopCompactPackets::IsInstanceResetResult(SMSG_RESET_FAILED_NOTIFY));
    CHECK(!MopCompactPackets::IsInstanceResetResult(SMSG_SET_DUNGEON_DIFFICULTY));

    CHECK(std::strcmp(LookupServerOpcodeName(SMSG_INSTANCE_RESET),
                      "SMSG_INSTANCE_RESET") == 0);
    CHECK(std::strcmp(LookupServerOpcodeName(SMSG_INSTANCE_RESET_FAILED),
                      "SMSG_INSTANCE_RESET_FAILED") == 0);
    CHECK(std::strcmp(LookupServerOpcodeName(SMSG_RESET_FAILED_NOTIFY),
                      "SMSG_RESET_FAILED_NOTIFY") == 0);

    if (g_fail)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }

    std::printf("mop_instance_reset_dispatch: all checks passed\n");
    return 0;
}
