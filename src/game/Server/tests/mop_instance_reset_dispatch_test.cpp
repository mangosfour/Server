/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 MaNGOS <https://www.getmangos.eu>
 */

#include "Opcodes.h"
#include "Player.h"
#include "WorldSession.h"
#include "Database/DatabaseEnv.h"

#include <cstdio>

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

    if (g_fail)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }

    std::printf("mop_instance_reset_dispatch: all checks passed\n");
    return 0;
}
