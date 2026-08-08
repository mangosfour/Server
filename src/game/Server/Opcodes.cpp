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
 * @file Opcodes.cpp
 * @brief Network opcode handler registration
 *
 * This file registers all network packet handlers for the world server.
 * It maps each opcode to its corresponding handler function in WorldSession,
 * along with session status requirements and processing mode.
 *
 * Opcode processing modes:
 * - PROCESS_INPLACE: Process immediately in network thread
 * - PROCESS_THREADUNSAFE: Process in world update thread
 *
 * Session status requirements:
 * - STATUS_NEVER: Never process (deprecated/debug opcodes)
 * - STATUS_LOGGEDIN: Require player to be logged in
 * - STATUS_UNHANDLED: No handler assigned
 *
 * @see Opcodes.h for opcode definitions
 * @see WorldSession for packet handler implementations
 */

#include "Opcodes.h"
#include "WorldSession.h"

#include <cstring>

/**
 * @brief Static integrity metadata for the Phase 1a login closure.
 *
 * Generated from out/phase1a_closure.txt + Four's real OPCODE() bindings.
 */
#include "opcode_closure.inc"

/// Correspondence between opcodes and their handlers, split by wire direction.
OpcodeHandler clientOpcodeTable[OPCODE_TABLE_SIZE];
OpcodeHandler serverOpcodeTable[OPCODE_TABLE_SIZE];

/// Which table slots a registration has already claimed, so that a second
/// claim on the same value is caught instead of silently replacing the first.
static bool clientOpcodeClaimed[OPCODE_TABLE_SIZE];
static bool serverOpcodeClaimed[OPCODE_TABLE_SIZE];

/**
 * @brief Register a client-received (inbound) opcode with its real handler.
 *
 * Two symbols sharing a value is a data error, not a runtime condition: the
 * second registration replaces the first and the displaced opcode simply stops
 * being dispatched. That happened with MSG_MOVE_WORLDPORT_ACK, whose inherited
 * 0x00E0 is CMSG_CHAR_ENUM in 18414 -- registering it hung every client on
 * "Retrieving character list", and nothing reported why. Fail loudly instead.
 */
static void DefC(uint16 v, char const* name, SessionStatus s, PacketProcessing p, void (WorldSession::*h)(WorldPacket&))
{
    MANGOS_ASSERT(v < OPCODE_TABLE_SIZE);
    if (clientOpcodeClaimed[v])
    {
        // Registering the same symbol twice is redundant but harmless; the
        // generated login closure and the manual block below it can overlap.
        // Two *different* symbols on one value is the bug this guards against.
        MANGOS_ASSERT(std::strcmp(name, clientOpcodeTable[v].name) == 0 &&
            "two client opcodes share one value: the second would silently "
            "displace the first from the dispatch table");
        sLog.outDetail("Opcodes: client opcode 0x%04X ('%s') registered twice", v, name);
    }
    clientOpcodeClaimed[v] = true;
    clientOpcodeTable[v] = OpcodeHandler{ name, s, p, h };
}

/**
 * @brief Register a server-sent (outbound) opcode name for logging metadata.
 */
static void DefS(uint16 v, char const* name)
{
    MANGOS_ASSERT(v < OPCODE_TABLE_SIZE);
    if (serverOpcodeClaimed[v])
    {
        MANGOS_ASSERT(std::strcmp(name, serverOpcodeTable[v].name) == 0 &&
            "two server opcodes share one value: the second would silently "
            "displace the first from the name table");
        sLog.outDetail("Opcodes: server opcode 0x%04X ('%s') registered twice", v, name);
    }
    serverOpcodeClaimed[v] = true;
    serverOpcodeTable[v] = OpcodeHandler{ name, STATUS_NEVER, PROCESS_INPLACE, &WorldSession::Handle_ServerSide };
}

/**
 * @brief Look up a dispatchable client opcode.
 * @return nullptr if out of range or not handled, otherwise the handler entry.
 */
OpcodeHandler const* LookupClientOpcode(uint16 value)
{
    if (value >= OPCODE_TABLE_SIZE)
    {
        return nullptr;
    }
    OpcodeHandler const& h = clientOpcodeTable[value];
    return h.status == STATUS_UNHANDLED ? nullptr : &h;
}

/// @brief Human-readable name of a client-direction opcode (greeting handled out-of-band).
char const* LookupClientOpcodeName(uint16 value)
{
    return value >= OPCODE_TABLE_SIZE ? (value == MSG_WOW_CONNECTION ? "MSG_WOW_CONNECTION" : "OUT_OF_RANGE") : clientOpcodeTable[value].name;
}

/// @brief Human-readable name of a server-direction opcode (greeting handled out-of-band).
char const* LookupServerOpcodeName(uint16 value)
{
    return value >= OPCODE_TABLE_SIZE ? (value == MSG_WOW_CONNECTION ? "MSG_WOW_CONNECTION" : "OUT_OF_RANGE") : serverOpcodeTable[value].name;
}

/// @brief Direction-aware opcode name lookup for human understandable logging.
char const* LookupOpcodeName(PacketDirection dir, uint16 value)
{
    return dir == DIR_CLIENT ? LookupClientOpcodeName(value) : LookupServerOpcodeName(value);
}

/**
 * @brief Verify the login closure registered exactly as its generated metadata expects.
 *
 * Confirms the greeting is rejected-but-named, every client opcode is dispatchable to
 * its real (or synthetic socket) handler, and server opcode names resolve.
 */
static void AssertLoginClosureIntegrity()
{
    for (auto const& c : kLoginClosure)
    {
        if (c.out_of_band)
        {
            MANGOS_ASSERT(LookupClientOpcode(uint16(c.value)) == nullptr);                       // greeting rejected, not aliased
            MANGOS_ASSERT(std::string(LookupClientOpcodeName(uint16(c.value))) == "MSG_WOW_CONNECTION");
            continue;
        }
        if (c.client)
        {
            OpcodeHandler const* actual = LookupClientOpcode(uint16(c.value));
            MANGOS_ASSERT(actual != nullptr);                                                     // dispatchable
            MANGOS_ASSERT(std::string(LookupClientOpcodeName(uint16(c.value))) == c.name);        // name matches
            MANGOS_ASSERT(actual->handler == c.handler);                                          // REAL/synthetic socket binding matches
        }
        else
        {
            MANGOS_ASSERT(std::string(LookupServerOpcodeName(uint16(c.value))) == c.name);        // metadata name
        }
    }
}

/**
 * @brief Initialize opcode handler metadata tables.
 *
 * Fills both direction tables with unhandled defaults, then registers the Phase 1a
 * login closure and asserts its integrity. The greeting (MSG_WOW_CONNECTION) is NOT
 * registered; it is handled out-of-band by proto::ClientConnection.
 */
void InitializeOpcodes()
{
    for (int i = 0; i < OPCODE_TABLE_SIZE; ++i)
    {
        clientOpcodeTable[i] = OpcodeHandler{ "UNKNOWN", STATUS_UNHANDLED, PROCESS_INPLACE, &WorldSession::Handle_NULL };
        serverOpcodeTable[i] = OpcodeHandler{ "UNKNOWN", STATUS_NEVER, PROCESS_INPLACE, &WorldSession::Handle_ServerSide };
        clientOpcodeClaimed[i] = false;
        serverOpcodeClaimed[i] = false;
    }
#include "opcode_register.inc"     // login closure only (Phase 1a); greeting NOT registered
    AssertLoginClosureIntegrity();

    // --- Opcodes registered beyond the Phase 1a login closure (kept here so they survive
    //     regeneration of opcode_register.inc). ---

    // CMSG_CHAR_DELETE (0x04E2) / SMSG_CHAR_DELETE (0x0C9F): delete a character from char-select.
    // The handler already exists; MoP sends the GUID bit-packed (decoded in HandleCharDeleteOpcode).
    DefC(CMSG_CHAR_DELETE, "CMSG_CHAR_DELETE", STATUS_AUTHED, PROCESS_THREADUNSAFE, &WorldSession::HandleCharDeleteOpcode);
    DefS(SMSG_CHAR_DELETE, "SMSG_CHAR_DELETE");

    // Character customization request/response at character select. Both bodies use the
    // 18414 GUID bit/byte permutations recovered directly from Wow.exe.
    DefC(CMSG_CHAR_CUSTOMIZE, "CMSG_CHAR_CUSTOMIZE", STATUS_AUTHED, PROCESS_THREADUNSAFE, &WorldSession::HandleCharCustomizeOpcode);
    DefS(SMSG_CHAR_CUSTOMIZE, "SMSG_CHAR_CUSTOMIZE");

    // The direct 18414 writers prove the upload's three-u32/blob/3-bit-type body (0x0068) and the
    // download request's 3-bit type body (0x1D8A).
    DefC(CMSG_UPDATE_ACCOUNT_DATA, "CMSG_UPDATE_ACCOUNT_DATA", STATUS_AUTHED, PROCESS_THREADUNSAFE, &WorldSession::HandleUpdateAccountData);

    // CMSG_REQUEST_ACCOUNT_DATA (0x1D8A): the DOWNLOAD counterpart of the upload above. The client
    // sends it when its local cache is older than the server's stored account data (as reported by
    // SMSG_ACCOUNT_DATA_TIMES); without this it dispatched as UNKNOWN and saved macros/config were
    // never served back (Codex PR #15 finding).
    DefC(CMSG_REQUEST_ACCOUNT_DATA, "CMSG_REQUEST_ACCOUNT_DATA", STATUS_AUTHED, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestAccountData);
    // The direct 18414 reader sub_6F1A32 proves the complete 0x0AAE reply body, including the
    // non-empty per-character GUID permutation.
    DefS(SMSG_UPDATE_ACCOUNT_DATA, "SMSG_UPDATE_ACCOUNT_DATA");

    // Shipped UI C_PurchaseAPI.GetPurchaseList maps through the retained API table directly to
    // the empty 0x18B2 writer. Retail answers it at character select: 434 requests and 420
    // responses across the 18414 corpus, request always 0 bytes, response always exactly 7.
    // We have no Store backend, so we answer the same thing retail answers a player who has
    // bought nothing -- an empty list -- rather than dropping the request on the floor.
    DefC(CMSG_BATTLE_PAY_GET_PURCHASE_LIST, "CMSG_BATTLE_PAY_GET_PURCHASE_LIST", STATUS_AUTHED, PROCESS_INPLACE, &WorldSession::HandleBattlePayGetPurchaseListOpcode);
    // Its partner: the client sends both on login, one after the other. We answered
    // the purchase list and left this one to fall through as "UNKNOWN (0x0DE0)" in
    // the log, which is noise that masks a genuinely unrecognised opcode.
    DefC(CMSG_BATTLE_PAY_GET_PRODUCT_LIST, "CMSG_BATTLE_PAY_GET_PRODUCT_LIST", STATUS_AUTHED, PROCESS_INPLACE, &WorldSession::HandleBattlePayGetProductListOpcode);
    DefS(SMSG_BATTLE_PAY_GET_PURCHASE_LIST_RESPONSE, "SMSG_BATTLE_PAY_GET_PURCHASE_LIST_RESPONSE");

    // The character-creation randomise button. CharacterCreate.lua's RequestRandomName()
    // round-trips to the server, so without this the button is inert. STATUS_AUTHED because
    // it is character-select traffic with no player in world.
    DefC(CMSG_GENERATE_RANDOM_CHARACTER_NAME, "CMSG_GENERATE_RANDOM_CHARACTER_NAME", STATUS_AUTHED, PROCESS_THREADUNSAFE, &WorldSession::HandleRandomizeCharNameOpcode);
    DefS(SMSG_RANDOMIZE_CHAR_NAME, "SMSG_RANDOMIZE_CHAR_NAME");

    // Login refusals. Every CHAR_LOGIN_* response code exists in ResponseCodes but has zero send
    // sites, so a refused login still tells the client nothing: HandlePlayerLoginOpcode returns
    // silently on a duplicate login and on a query-holder failure, and HandlePlayerLogin kicks the
    // connection when LoadFromDB fails.
    //
    // Registering the opcode here is a prerequisite for fixing that, NOT the fix. It is named so
    // the dispatcher knows it and it stops counting as unregistered; nothing sends it yet, and the
    // three branches above are unchanged.
    //
    // The sender is deliberately not written on this branch because the body shape is unproven:
    //
    //   1. Zero observations of 0x1A0B in the 18414 corpus. Retail sniffs capture successful
    //      logins, so a refusal packet would not appear even if it exists.
    //   2. The one-byte reader that looks like its parser, sub_6BB6E9, is reached only from
    //      sub_6C3D99 -- a constructor called from twelve distinct sites -- so it is a generic
    //      single-byte message reader and does not bind this opcode to that shape.
    //
    // NOT a reason, despite looking like one: the absence of a 0x1A0B literal in the
    // disassembly. That test does not discriminate. SMSG_SET_TIME_ZONE_INFORMATION is 0x19C1,
    // is certainly correct (817 corpus observations at build 18414, and a live client acts on
    // it), and likewise has no dword occurrence in either the 32- or 64-bit image. MoP client
    // SMSG opcodes are not stored as searchable constants, so absence proves nothing about the
    // value. Do not resurrect that argument.
    //
    // The client-side CHAR_LOGIN_* display path is also no help in binding this: all ten callers
    // of the response-name lookup sub_A64ADB are local. Four pass literal CHAR_LOGIN_* codes
    // (84, 86, 87, 91) after reading local character flags at dword_1095DD0+380, and the dynamic
    // ones are fed by local name validators. No observed path carries a server byte into that
    // display, so the in-game message is generated before anything is sent.
    //
    // Guessing wrong here is not cheap: a malformed body reaching the 18414 client can crash it,
    // which is the same reason the enter-world admission list refuses unconverted senders.
    DefS(SMSG_CHARACTER_LOGIN_FAILED, "SMSG_CHARACTER_LOGIN_FAILED");

    // Wave 2 server messages whose 5.4.8 bodies are encoded by MopCompactPackets.
    DefS(SMSG_ATTACKSWING_ERROR, "SMSG_ATTACKSWING_ERROR");
    DefS(SMSG_MOVE_SET_SWIM_SPEED, "SMSG_MOVE_SET_SWIM_SPEED");
    DefS(SMSG_MOVE_SET_RUN_SPEED, "SMSG_MOVE_SET_RUN_SPEED");
    DefS(SMSG_MOVE_SET_WALK_SPEED, "SMSG_MOVE_SET_WALK_SPEED");
    DefS(SMSG_SPLINE_MOVE_SET_RUN_SPEED, "SMSG_SPLINE_MOVE_SET_RUN_SPEED");
    DefS(SMSG_MOVE_SET_RUN_BACK_SPEED, "SMSG_MOVE_SET_RUN_BACK_SPEED");
    DefS(SMSG_MOVE_SET_FLIGHT_SPEED, "SMSG_MOVE_SET_FLIGHT_SPEED");
    DefS(SMSG_SPLINE_MOVE_SET_WALK_SPEED, "SMSG_SPLINE_MOVE_SET_WALK_SPEED");
    DefS(SMSG_SPLINE_MOVE_SET_RUN_BACK_SPEED, "SMSG_SPLINE_MOVE_SET_RUN_BACK_SPEED");
    DefS(SMSG_SPLINE_MOVE_SET_SWIM_SPEED, "SMSG_SPLINE_MOVE_SET_SWIM_SPEED");
    DefS(SMSG_SPLINE_MOVE_SET_FLIGHT_SPEED, "SMSG_SPLINE_MOVE_SET_FLIGHT_SPEED");
    // The corpus carries no observation of these four, so they have no retail
    // fixture. They are admitted on binary proof: each is built from its client
    // reader, and both mask order and byte order are pinned by test. Sending a
    // reader-correct body beats suppressing the packet entirely.
    DefS(SMSG_SPLINE_MOVE_SET_SWIM_BACK_SPEED, "SMSG_SPLINE_MOVE_SET_SWIM_BACK_SPEED");
    DefS(SMSG_SPLINE_MOVE_SET_TURN_RATE, "SMSG_SPLINE_MOVE_SET_TURN_RATE");
    DefS(SMSG_SPLINE_MOVE_SET_FLIGHT_BACK_SPEED, "SMSG_SPLINE_MOVE_SET_FLIGHT_BACK_SPEED");
    DefS(SMSG_SPLINE_MOVE_SET_PITCH_RATE, "SMSG_SPLINE_MOVE_SET_PITCH_RATE");
    // Their DIRECT counterparts. Admitting the spline halves alone told every
    // observer about a speed change while the mover's own client heard nothing,
    // which is a worse desync than dropping both.
    DefS(SMSG_MOVE_SET_SWIM_BACK_SPEED, "SMSG_MOVE_SET_SWIM_BACK_SPEED");
    DefS(SMSG_MOVE_SET_TURN_RATE, "SMSG_MOVE_SET_TURN_RATE");
    DefS(SMSG_MOVE_SET_FLIGHT_BACK_SPEED, "SMSG_MOVE_SET_FLIGHT_BACK_SPEED");
    DefS(SMSG_MOVE_SET_PITCH_RATE, "SMSG_MOVE_SET_PITCH_RATE");
    DefS(SMSG_RANDOM_ROLL, "SMSG_RANDOM_ROLL");
    DefS(SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, "SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT");
    DefS(SMSG_SET_RAID_DIFFICULTY, "SMSG_SET_RAID_DIFFICULTY");
    DefS(SMSG_SET_DUNGEON_DIFFICULTY, "SMSG_SET_DUNGEON_DIFFICULTY");
    DefS(SMSG_INSTANCE_RESET, "SMSG_INSTANCE_RESET");
    DefS(SMSG_INSTANCE_RESET_FAILED, "SMSG_INSTANCE_RESET_FAILED");
    DefS(SMSG_RESET_FAILED_NOTIFY, "SMSG_RESET_FAILED_NOTIFY");
    DefS(SMSG_TRAINER_BUY_FAILED, "SMSG_TRAINER_BUY_FAILED");
    DefS(SMSG_GM_TICKET_UPDATE, "SMSG_GM_TICKET_UPDATE");
    DefC(CMSG_GMTICKET_CREATE, "CMSG_GMTICKET_CREATE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGMTicketCreateOpcode);
    DefC(CMSG_GMTICKET_GETTICKET, "CMSG_GMTICKET_GETTICKET", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGMTicketGetTicketOpcode);
    DefS(SMSG_GMTICKET_GETTICKET, "SMSG_GMTICKET_GETTICKET");
    DefC(CMSG_GMTICKET_SYSTEMSTATUS, "CMSG_GMTICKET_SYSTEMSTATUS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGMTicketSystemStatusOpcode);
    DefS(SMSG_GMTICKET_SYSTEMSTATUS, "SMSG_GMTICKET_SYSTEMSTATUS");
    DefC(CMSG_GM_UPDATE_TICKET_STATUS, "CMSG_GM_UPDATE_TICKET_STATUS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGMUpdateTicketStatusOpcode);
    DefS(SMSG_GM_TICKET_CASE_STATUS, "SMSG_GM_TICKET_CASE_STATUS");
    DefS(SMSG_LOGIN_VERIFY_WORLD, "SMSG_LOGIN_VERIFY_WORLD");
    DefS(SMSG_NEW_WORLD, "SMSG_NEW_WORLD");
    DefS(SMSG_SUSPEND_TOKEN, "SMSG_SUSPEND_TOKEN");
    DefC(CMSG_SUSPEND_TOKEN_RESPONSE, "CMSG_SUSPEND_TOKEN_RESPONSE", STATUS_TRANSFER, PROCESS_THREADUNSAFE, &WorldSession::HandleSuspendTokenResponse);
    DefS(SMSG_TRANSFER_PENDING, "SMSG_TRANSFER_PENDING");
    DefS(SMSG_TRANSFER_ABORTED, "SMSG_TRANSFER_ABORTED");
    DefS(SMSG_LOGIN_SETTIMESPEED, "SMSG_LOGIN_SETTIMESPEED");
    DefS(SMSG_TIME_SYNC_REQ, "SMSG_TIME_SYNC_REQ");
    DefC(CMSG_TIME_SYNC_RESP, "CMSG_TIME_SYNC_RESP", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTimeSyncResp);
    DefC(CMSG_TIME_SYNC_RESPONSE_FAILED, "CMSG_TIME_SYNC_RESPONSE_FAILED", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTimeSyncResponseFailed);
    DefC(CMSG_TIME_SYNC_RESPONSE_DROPPED, "CMSG_TIME_SYNC_RESPONSE_DROPPED", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTimeSyncResponseDropped);
    DefC(CMSG_DISCARDED_TIME_SYNC_ACKS, "CMSG_DISCARDED_TIME_SYNC_ACKS", STATUS_LOGGEDIN_OR_RECENTLY_LOGGEDOUT, PROCESS_THREADUNSAFE, &WorldSession::HandleDiscardedTimeSyncAcks);
    DefS(SMSG_TRIGGER_CINEMATIC, "SMSG_TRIGGER_CINEMATIC");
    DefS(SMSG_WORLD_SERVER_INFO, "SMSG_WORLD_SERVER_INFO");
    DefS(SMSG_MOTD, "SMSG_MOTD");
    DefS(SMSG_CORPSE_RECLAIM_DELAY, "SMSG_CORPSE_RECLAIM_DELAY");
    DefC(CMSG_REQUEST_FORCED_REACTIONS, "CMSG_REQUEST_FORCED_REACTIONS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestForcedReactionsOpcode);
    DefS(SMSG_SET_FORCED_REACTIONS, "SMSG_SET_FORCED_REACTIONS");
    DefS(SMSG_SET_FACTION_STANDING, "SMSG_SET_FACTION_STANDING");
    DefS(SMSG_SET_FACTION_VISIBLE, "SMSG_SET_FACTION_VISIBLE");
    DefS(SMSG_TITLE_EARNED, "SMSG_TITLE_EARNED");
    DefS(SMSG_TITLE_LOST, "SMSG_TITLE_LOST");
    DefS(SMSG_PVP_CREDIT, "SMSG_PVP_CREDIT");
    DefS(SMSG_CROSSED_INEBRIATION_THRESHOLD, "SMSG_CROSSED_INEBRIATION_THRESHOLD");
    DefS(SMSG_INIT_WORLD_STATES, "SMSG_INIT_WORLD_STATES");
    DefS(SMSG_UPDATE_WORLD_STATE, "SMSG_UPDATE_WORLD_STATE");
    DefS(SMSG_ITEM_TIME_UPDATE, "SMSG_ITEM_TIME_UPDATE");
    DefS(SMSG_ITEM_ENCHANT_TIME_UPDATE, "SMSG_ITEM_ENCHANT_TIME_UPDATE");
    // NotifyInspect writes one packed target GUID; the paired result returns
    // equipment, glyph, talent, specialization, and optional guild records.
    DefC(CMSG_INSPECT, "CMSG_INSPECT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleInspectOpcode);
    DefS(SMSG_INSPECT_RESULTS, "SMSG_INSPECT_RESULTS");
    DefS(SMSG_MOVE_TELEPORT, "SMSG_MOVE_TELEPORT");
    DefS(SMSG_CLIENT_CONTROL_UPDATE, "SMSG_CLIENT_CONTROL_UPDATE");
    DefS(SMSG_MOVE_SET_ACTIVE_MOVER, "SMSG_MOVE_SET_ACTIVE_MOVER");
    DefS(SMSG_UPDATE_CURRENCY, "SMSG_UPDATE_CURRENCY");
    DefS(SMSG_SETUP_CURRENCY, "SMSG_SETUP_CURRENCY");
    DefS(SMSG_WEEKLY_RESET_CURRENCIES, "SMSG_WEEKLY_RESET_CURRENCIES");
    DefS(SMSG_SET_CURRENCY_WEEK_LIMIT, "SMSG_SET_CURRENCY_WEEK_LIMIT");
    DefS(SMSG_SPELL_EXECUTE_LOG, "SMSG_SPELL_EXECUTE_LOG");
    DefS(SMSG_SPELL_PERIODIC_AURA_LOG, "SMSG_SPELL_PERIODIC_AURA_LOG");
    DefS(SMSG_SPELLDISPELLOG, "SMSG_SPELLDISPELLOG");
    DefS(SMSG_SPELLINTERRUPTLOG, "SMSG_SPELLINTERRUPTLOG");
    DefS(SMSG_SPELLINSTAKILLLOG, "SMSG_SPELLINSTAKILLLOG");
    DefS(SMSG_SPELLENERGIZELOG, "SMSG_SPELLENERGIZELOG");
    DefS(SMSG_SPELLHEALLOG, "SMSG_SPELLHEALLOG");
    DefS(SMSG_SPELLDAMAGESHIELD, "SMSG_SPELLDAMAGESHIELD");
    DefS(SMSG_SPELLLOGMISS, "SMSG_SPELLLOGMISS");
    DefS(SMSG_AURA_UPDATE, "SMSG_AURA_UPDATE");
    DefS(SMSG_UPDATE_OBJECT, "SMSG_UPDATE_OBJECT");
    DefS(SMSG_DESTROY_OBJECT, "SMSG_DESTROY_OBJECT");
    DefS(SMSG_MESSAGECHAT, "SMSG_MESSAGECHAT");
    DefS(SMSG_CHAT_PLAYER_NOT_FOUND, "SMSG_CHAT_PLAYER_NOT_FOUND");
    DefS(SMSG_CHAT_PLAYER_AMBIGUOUS, "SMSG_CHAT_PLAYER_AMBIGUOUS");
    DefS(SMSG_CHAT_RESTRICTED, "SMSG_CHAT_RESTRICTED");
    DefC(CMSG_UNREGISTER_ALL_ADDON_PREFIXES, "CMSG_UNREGISTER_ALL_ADDON_PREFIXES", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleUnregisterAddonPrefixesOpcode);
    DefC(CMSG_ADDON_REGISTERED_PREFIXES, "CMSG_ADDON_REGISTERED_PREFIXES", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAddonRegisteredPrefixesOpcode);
    // 18414 /say requests carry a uint32 language followed by the bit-packed message body.
    DefC(CMSG_MESSAGECHAT_SAY, "CMSG_MESSAGECHAT_SAY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_AFK, "CMSG_MESSAGECHAT_AFK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);

    // The rest of the chat channels. Only say and afk were registered, so
    // everything else a player types was dropped without a trace -- including
    // GM commands, which ride the chat opcode, and which is why a typed
    // .revive appeared to do nothing at all.
    //
    // HandleMessagechatOpcode switches on all thirteen types and reads a uint32
    // language followed by an 8-bit message length and the raw string.
    //
    // These originally read a NINE-bit length, admitted on the strength of body
    // sizes agreeing with capture. Size could not tell the two apart -- a
    // six-byte minimum is consistent with both -- and the nine-bit read
    // consumed the length byte together with the first character byte, so every
    // message arrived silently missing its first letter. Decoded corpus bodies
    // and the client's own writer, which consumes exactly eight bits, settled
    // it. mop_chat_packets_source_mutation_inline_length_width pins the width
    // at all ten inline sites.
    DefC(CMSG_MESSAGECHAT_YELL, "CMSG_MESSAGECHAT_YELL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_EMOTE, "CMSG_MESSAGECHAT_EMOTE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_DND, "CMSG_MESSAGECHAT_DND", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_WHISPER, "CMSG_MESSAGECHAT_WHISPER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_CHANNEL, "CMSG_MESSAGECHAT_CHANNEL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_GUILD, "CMSG_MESSAGECHAT_GUILD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_OFFICER, "CMSG_MESSAGECHAT_OFFICER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_PARTY, "CMSG_MESSAGECHAT_PARTY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_RAID, "CMSG_MESSAGECHAT_RAID", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_RAID_WARNING, "CMSG_MESSAGECHAT_RAID_WARNING", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_INSTANCE, "CMSG_MESSAGECHAT_INSTANCE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMessagechatOpcode);

    // Addon traffic rides its own six opcodes and its own handler. Every channel
    // uses an EIGHT-bit message length and a five-bit prefix length, but they
    // disagree on the order of the two lengths and on the order of the two
    // strings, so each has its own decode and its own fixture:
    //
    //   instance  prefix-5, message-8   -> message, prefix
    //   raid      prefix-5, message-8   -> message, prefix
    //   party     message-8, prefix-5   -> message, prefix
    //   guild     message-8, prefix-5   -> prefix, message
    //   officer   message-8, prefix-5   -> prefix, message
    //   whisper   target-9, message-8, prefix-5 -> target, prefix, message
    //
    // Five are decoded from capture. Officer has zero corpus observations and is
    // taken from the client writer sub_C888C4 instead, which gives it the guild
    // layout; an earlier revision inferred it from raid and had it backwards.
    DefC(CMSG_MESSAGECHAT_ADDON_RAID, "CMSG_MESSAGECHAT_ADDON_RAID", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAddonMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_ADDON_PARTY, "CMSG_MESSAGECHAT_ADDON_PARTY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAddonMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_ADDON_INSTANCE, "CMSG_MESSAGECHAT_ADDON_INSTANCE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAddonMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_ADDON_GUILD, "CMSG_MESSAGECHAT_ADDON_GUILD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAddonMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_ADDON_OFFICER, "CMSG_MESSAGECHAT_ADDON_OFFICER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAddonMessagechatOpcode);
    DefC(CMSG_MESSAGECHAT_ADDON_WHISPER, "CMSG_MESSAGECHAT_ADDON_WHISPER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAddonMessagechatOpcode);

    // Reporting a message as spam. Its handler already decodes the 18414 guid
    // with ReadGuidMask/ReadGuidBytes rather than a raw read.
    DefC(CMSG_CHAT_IGNORED, "CMSG_CHAT_IGNORED", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleChatIgnoredOpcode);

    // CMSG_MESSAGECHAT_BATTLEGROUND stays dormant: its value is inherited from
    // 4.3.4 and unverified for 5.4.8, and at 0x2156 it exceeds the thirteen
    // bits the 18414 header gives an opcode, so it cannot be what the client
    // sends. HandleMessagechatOpcode has no case for it either.
    DefC(CMSG_TEXT_EMOTE, "CMSG_TEXT_EMOTE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTextEmoteOpcode);
    DefC(CMSG_EMOTE, "CMSG_EMOTE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleEmoteOpcode);
    DefS(SMSG_TEXT_EMOTE, "SMSG_TEXT_EMOTE");
    DefS(SMSG_EMOTE, "SMSG_EMOTE");
    DefS(SMSG_NOTIFICATION, "SMSG_NOTIFICATION");
    DefS(SMSG_TRADE_STATUS, "SMSG_TRADE_STATUS");
    DefS(SMSG_TRADE_STATUS_EXTENDED, "SMSG_TRADE_STATUS_EXTENDED");

    // 18414 tutorial state requests: one uint32 flag index, then empty clear/reset controls.
    DefC(CMSG_TUTORIAL_FLAG, "CMSG_TUTORIAL_FLAG", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTutorialFlagOpcode);
    DefC(CMSG_TUTORIAL_CLEAR, "CMSG_TUTORIAL_CLEAR", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTutorialClearOpcode);
    DefC(CMSG_TUTORIAL_RESET, "CMSG_TUTORIAL_RESET", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTutorialResetOpcode);

    // Wave 5 regular initial UI/input envelope messages.
    DefS(SMSG_INITIAL_SPELLS, "SMSG_INITIAL_SPELLS");
    DefS(SMSG_SEND_UNLEARN_SPELLS, "SMSG_SEND_UNLEARN_SPELLS");
    DefS(SMSG_ACTION_BUTTONS, "SMSG_ACTION_BUTTONS");
    DefS(SMSG_INITIALIZE_FACTIONS, "SMSG_INITIALIZE_FACTIONS");
    DefS(SMSG_ALL_ACHIEVEMENT_DATA, "SMSG_ALL_ACHIEVEMENT_DATA");
    DefS(SMSG_CRITERIA_UPDATE, "SMSG_CRITERIA_UPDATE");
    DefC(CMSG_REQUEST_CATEGORY_COOLDOWNS, "CMSG_REQUEST_CATEGORY_COOLDOWNS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestCategoryCooldowns);
    DefS(SMSG_CATEGORY_COOLDOWN, "SMSG_CATEGORY_COOLDOWN");
    DefS(SMSG_BINDPOINTUPDATE, "SMSG_BINDPOINTUPDATE");
    DefS(SMSG_SET_PROFICIENCY, "SMSG_SET_PROFICIENCY");
    DefS(SMSG_WEATHER, "SMSG_WEATHER");

    // CMSG_LOGOUT_REQUEST (0x0643) is the manual "logout" API route; CMSG_LOGOUT_REQUEST_IDLE
    // (0x1349) is the distinct automatic-idle route. Both have empty bodies and use the existing logout
    // flow. CMSG_LOGOUT_CANCEL remains 0x06C1. STATUS_LOGGEDIN -- all require an in-world player
    // (the handlers dereference GetPlayer()).
    // The replies (SMSG_LOGOUT_RESPONSE/CANCEL_ACK/COMPLETE) pass the enter-world suppression via
    // IsEnterWorldConverted(); their 18414 bodies are simple (response = uint32 reason + instant bit;
    // cancel-ack = empty; complete = leading bit plus zero GUID mask, 80 00). On the open-world start
    // map logout is the non-instant 20s-timer path (instant only in rest areas / for GMs).
    DefC(CMSG_LOGOUT_REQUEST, "CMSG_LOGOUT_REQUEST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLogoutRequestOpcode);
    DefC(CMSG_LOGOUT_REQUEST_IDLE, "CMSG_LOGOUT_REQUEST_IDLE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLogoutRequestOpcode);
    DefC(CMSG_LOGOUT_CANCEL, "CMSG_LOGOUT_CANCEL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLogoutCancelOpcode);
    DefS(SMSG_LOGOUT_RESPONSE, "SMSG_LOGOUT_RESPONSE");
    DefS(SMSG_LOGOUT_CANCEL_ACK, "SMSG_LOGOUT_CANCEL_ACK");
    DefS(SMSG_LOGOUT_COMPLETE, "SMSG_LOGOUT_COMPLETE");

    // Live-log worklist batch 1. Client constructors and body writers were
    // verified directly in the IDA 9.4 18414 Wow.exe database.
    // Authed rather than logged-in: the client's first hotfix batch arrives
    // immediately after CMSG_PLAYER_LOGIN, roughly ninety packets before
    // SMSG_LOGIN_VERIFY_WORLD, so the player is not in the world and often does
    // not exist yet. STATUS_LOGGEDIN drops it either way -- logged when _player
    // is null, silently when the player exists but IsInWorld() is false -- which
    // is why mid-session requests were answered and the whole login batch was
    // not. Hotfix is session state, not world state, and none of the reply
    // builders touch _player.
    DefC(CMSG_REQUEST_HOTFIX, "CMSG_REQUEST_HOTFIX", STATUS_AUTHED, PROCESS_INPLACE, &WorldSession::HandleRequestHotfix);
    DefC(CMSG_JOIN_CHANNEL, "CMSG_JOIN_CHANNEL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleJoinChannelOpcode);
    DefS(SMSG_CHANNEL_NOTIFY, "SMSG_CHANNEL_NOTIFY");
    DefS(SMSG_CHANNEL_LIST, "SMSG_CHANNEL_LIST");

    // Listing a channel's members. Joining was registered but listing was not, so
    // /chatlist did nothing. The reply is already registered and already admitted by
    // the in-world send gate above, and is built by MopChannelPackets::BuildList, so
    // this adds no new outbound surface.
    //
    // The request is a seven-bit name length then the raw name. Retail bodies:
    //
    //   12 6F 71 63 68 61 6E 6E 65 6C                  0x12 >> 1 == 9  "oqchannel"
    //   32 "General - The Storm Peaks"                 0x32 >> 1 == 25
    //   3C "LocalDefense - The Storm Peaks"            0x3C >> 1 == 30
    //
    // That is the same seven-bit channel-name length the already-working join path
    // reads in MopChannelPackets::ReadJoinChannelRequest. The handler read eight bits,
    // which returns double, and survived only because the name occupies the rest of
    // the payload and ReadString clamps to the buffer end. With any field after the
    // name the oversized count would swallow it into the channel name and misalign
    // everything following, so it is corrected here.
    DefC(CMSG_CHANNEL_LIST, "CMSG_CHANNEL_LIST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleChannelListOpcode);
    DefC(CMSG_CANCEL_TRADE, "CMSG_CANCEL_TRADE", STATUS_LOGGEDIN_OR_RECENTLY_LOGGEDOUT, PROCESS_THREADUNSAFE, &WorldSession::HandleCancelTradeOpcode);

    // The rest of the trade conversation. Only cancel was registered, so a player
    // could abort a trade they had no way to start: every other step was dropped.
    //
    // These add no new outbound surface. The exchange answers through
    // SMSG_TRADE_STATUS via SendTradeStatus, and item and money changes additionally
    // answer through SMSG_TRADE_STATUS_EXTENDED via TradeData::Update. Both are
    // already registered, already admitted by the in-world send gate and already
    // covered by mop_trade_packets, which is why this batch is the request side only.
    //
    // CMSG_ACCEPT_TRADE is deliberately NOT registered, so a trade can be set up here
    // but not completed. That is the honest state: completing it safely needs work
    // this batch does not do.
    //
    // Its uint32 is not padding. It varies across retail requests and tracks the
    // trade state the client currently has displayed, and retail uses it to reject an
    // accept aimed at an offer that has since changed. Discarding it looked safe on
    // the argument that we already clear BOTH sides' accepted flags on every offer
    // mutation, so a changed offer cannot carry an old accept. That argument is
    // wrong, because it assumes the clear happens after the accept is recorded.
    // Sessions have their own FIFOs and the world drains them independently, so:
    //
    //   A sends ACCEPT(V1), which sits queued.
    //   B sends SET_ITEM(V2) then ACCEPT(V2).
    //   B's session drains first: the mutation clears both flags, then B accepts V2.
    //   A's older packet drains: the token is discarded and A is marked accepted
    //   against the CURRENT offer, which is now V2.
    //   Both sides read as accepted and V2 is finalized. A never accepted V2.
    //
    // The clear cannot invalidate an accept that does not exist yet, so the token is
    // the actual protection against that interleaving, not a cross-check on top of
    // one. Registering accept without consuming it would hand out a way to complete a
    // trade against an offer the other party never agreed to.
    //
    // It returns once the token is derived and validated, which needs the
    // SMSG_TRADE_STATUS_EXTENDED header semantics worked out first -- those fields are
    // currently hardcoded 0, 7, 0, 7.
    //
    // Each handler's reads match the retail body, and every one of these bodies is
    // fixed-width in the corpus:
    //
    //   BEGIN_TRADE      reads nothing            retail 0 bytes
    //   UNACCEPT_TRADE   reads nothing            retail 0 bytes
    //   CLEAR_TRADE_ITEM uint8                    retail 1 byte
    //   SET_TRADE_ITEM   three uint8              retail 3 bytes
    //   SET_TRADE_GOLD   uint64                   retail 8 bytes
    //   INITIATE_TRADE   one bit-packed guid      retail 6 bytes
    //
    // INITIATE_TRADE needed its reader corrected first. The inherited permutation
    // decoded a different byte set than the client sends -- see the note on
    // MopTradePackets::ReadInitiateTrade, whose orders come from the client's own
    // send serializer sub_69238D rather than from any fork.
    DefC(CMSG_BEGIN_TRADE, "CMSG_BEGIN_TRADE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleBeginTradeOpcode);
    DefC(CMSG_INITIATE_TRADE, "CMSG_INITIATE_TRADE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleInitiateTradeOpcode);
    DefC(CMSG_UNACCEPT_TRADE, "CMSG_UNACCEPT_TRADE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleUnacceptTradeOpcode);
    DefC(CMSG_SET_TRADE_GOLD, "CMSG_SET_TRADE_GOLD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetTradeGoldOpcode);
    DefC(CMSG_SET_TRADE_ITEM, "CMSG_SET_TRADE_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetTradeItemOpcode);
    DefC(CMSG_CLEAR_TRADE_ITEM, "CMSG_CLEAR_TRADE_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleClearTradeItemOpcode);
    DefC(CMSG_UI_TIME_REQUEST, "CMSG_UI_TIME_REQUEST", STATUS_LOGGEDIN, PROCESS_INPLACE, &WorldSession::HandleUITimeRequestOpcode);
    DefC(CMSG_LOAD_SCREEN, "CMSG_LOAD_SCREEN", STATUS_AUTHED, PROCESS_THREADUNSAFE, &WorldSession::HandleLoadScreenOpcode);
    DefC(CMSG_QUERY_COUNTDOWN_TIMER, "CMSG_QUERY_COUNTDOWN_TIMER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQueryCountdownTimerOpcode);
    DefS(SMSG_UI_TIME, "SMSG_UI_TIME");
    DefS(SMSG_DB_REPLY, "SMSG_DB_REPLY");
    DefS(SMSG_START_TIMER, "SMSG_START_TIMER");

    // Live-log movement control requests. Client writers were verified directly
    // in the IDA 9.4 18414 Wow.exe database.
    DefC(CMSG_MOVE_TIME_SKIPPED, "CMSG_MOVE_TIME_SKIPPED", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveTimeSkippedOpcode);
    DefC(CMSG_SET_ACTIVE_MOVER, "CMSG_SET_ACTIVE_MOVER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetActiveMoverOpcode);

    // Live-log client preference toggles. The 18414 writers emit one byte for
    // action bars and two bits for the voice/microphone flags.
    DefC(CMSG_SET_ACTIONBAR_TOGGLES, "CMSG_SET_ACTIONBAR_TOGGLES", STATUS_AUTHED, PROCESS_THREADUNSAFE, &WorldSession::HandleSetActionBarTogglesOpcode);
    DefC(CMSG_VIOLENCE_LEVEL, "CMSG_VIOLENCE_LEVEL", STATUS_AUTHED, PROCESS_INPLACE, &WorldSession::HandleViolenceLevelOpcode);
    DefC(CMSG_VOICE_SESSION_ENABLE, "CMSG_VOICE_SESSION_ENABLE", STATUS_LOGGEDIN, PROCESS_INPLACE, &WorldSession::HandleVoiceSessionEnableOpcode);

    // Live-log player state requests. The 18414 client writers emit a uint32
    // plus one presence bit for sheath state, a packed selection GUID, and one
    // uint32 for stand state. The paired stand-state response is one byte.
    DefC(CMSG_SETSHEATHED, "CMSG_SETSHEATHED", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetSheathedOpcode);
    DefC(CMSG_SET_SELECTION, "CMSG_SET_SELECTION", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetSelectionOpcode);
    DefC(CMSG_STANDSTATECHANGE, "CMSG_STANDSTATECHANGE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleStandStateChangeOpcode);
    // The client reporting that it could not build an object we sent a VALUES
    // update for. Body is a packed GUID; the 18414 writer is the packet class at
    // off_D65304, whose header virtual sub_690E2A writes 4193 and whose body
    // virtual sub_694863 emits mask 3,5,6,0,1,2,7,4 then bytes 0,6,5,7,2,1,3,4.
    // Not present anywhere in the corpus, because a retail server does not
    // provoke it. Ours does, so it is the only signal naming a broken object.
    DefC(CMSG_OBJECT_UPDATE_FAILED, "CMSG_OBJECT_UPDATE_FAILED", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleObjectUpdateFailedOpcode);
    DefS(SMSG_STANDSTATE_UPDATE, "SMSG_STANDSTATE_UPDATE");
    DefC(CMSG_ATTACKSWING, "CMSG_ATTACKSWING", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAttackSwingOpcode);
    // Body recovered from decoded 18414 corpus payloads, not from size
    // agreement: the action leads, then the position, then sixteen presence
    // bits interleaved across the pet and target GUIDs.
    DefC(CMSG_PET_ACTION, "CMSG_PET_ACTION", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandlePetAction);
    DefC(CMSG_PET_STOP_ATTACK, "CMSG_PET_STOP_ATTACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandlePetStopAttack);
    DefC(CMSG_PET_SET_ACTION, "CMSG_PET_SET_ACTION", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandlePetSetAction);
    // Request and response both recovered from decoded 18414 payloads. One
    // response answers a request decoded separately and echoes its pet number,
    // which ties the pair together.
    DefC(CMSG_PET_NAME_QUERY, "CMSG_PET_NAME_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandlePetNameQueryOpcode);
    DefS(SMSG_PET_NAME_QUERY_RESPONSE, "SMSG_PET_NAME_QUERY_RESPONSE");
    DefS(SMSG_MOVE_SET_CAN_FLY, "SMSG_MOVE_SET_CAN_FLY");
    DefS(SMSG_MOVE_UNSET_CAN_FLY, "SMSG_MOVE_UNSET_CAN_FLY");
    DefS(SMSG_SPLINE_MOVE_SET_FLYING, "SMSG_SPLINE_MOVE_SET_FLYING");
    DefS(SMSG_SPLINE_MOVE_UNSET_FLYING, "SMSG_SPLINE_MOVE_UNSET_FLYING");
    DefC(CMSG_ATTACKSTOP, "CMSG_ATTACKSTOP", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAttackStopOpcode);
    DefS(SMSG_ATTACKSTART, "SMSG_ATTACKSTART");
    DefS(SMSG_ATTACKSTOP, "SMSG_ATTACKSTOP");
    DefS(SMSG_ATTACKERSTATEUPDATE, "SMSG_ATTACKERSTATEUPDATE");
    DefS(SMSG_PARTYKILLLOG, "SMSG_PARTYKILLLOG");
    DefS(SMSG_DUEL_OUTOFBOUNDS, "SMSG_DUEL_OUTOFBOUNDS");
    DefS(SMSG_DUEL_INBOUNDS, "SMSG_DUEL_INBOUNDS");
    DefS(SMSG_DUEL_COMPLETE, "SMSG_DUEL_COMPLETE");
    DefS(SMSG_DUEL_COUNTDOWN, "SMSG_DUEL_COUNTDOWN");
    DefS(SMSG_DUEL_REQUESTED, "SMSG_DUEL_REQUESTED");
    DefS(SMSG_DUEL_WINNER, "SMSG_DUEL_WINNER");
    DefS(SMSG_START_MIRROR_TIMER, "SMSG_START_MIRROR_TIMER");
    DefS(SMSG_STOP_MIRROR_TIMER, "SMSG_STOP_MIRROR_TIMER");
    DefS(SMSG_CHANNEL_START, "SMSG_CHANNEL_START");
    DefS(SMSG_CHANNEL_UPDATE, "SMSG_CHANNEL_UPDATE");
    DefS(SMSG_RESYNC_RUNES, "SMSG_RESYNC_RUNES");
    DefS(SMSG_ADD_RUNE_POWER, "SMSG_ADD_RUNE_POWER");
    DefS(SMSG_CONVERT_RUNE, "SMSG_CONVERT_RUNE");
    DefS(SMSG_THREAT_UPDATE, "SMSG_THREAT_UPDATE");
    DefS(SMSG_HIGHEST_THREAT_UPDATE, "SMSG_HIGHEST_THREAT_UPDATE");
    DefS(SMSG_THREAT_CLEAR, "SMSG_THREAT_CLEAR");
    DefS(SMSG_THREAT_REMOVE, "SMSG_THREAT_REMOVE");
    DefS(SMSG_DISMOUNT, "SMSG_DISMOUNT");
    DefS(SMSG_PRE_RESURRECT, "SMSG_PRE_RESURRECT");
    DefS(SMSG_UPDATE_COMBO_POINTS, "SMSG_UPDATE_COMBO_POINTS");
    DefS(SMSG_ACHIEVEMENT_EARNED, "SMSG_ACHIEVEMENT_EARNED");
    // Staging UI packets admitted together after their 18414 bodies were
    // re-verified. These rows provide logging metadata; admission is the
    // separate IsEnterWorldConverted policy.
    DefS(SMSG_SHOW_BANK, "SMSG_SHOW_BANK");
    DefS(SMSG_SHOW_MAILBOX, "SMSG_SHOW_MAILBOX");
    DefS(SMSG_SERVER_MESSAGE, "SMSG_SERVER_MESSAGE");
    DefS(SMSG_RECEIVED_MAIL, "SMSG_RECEIVED_MAIL");
    DefS(SMSG_GM_TICKET_STATUS_UPDATE, "SMSG_GM_TICKET_STATUS_UPDATE");
    DefS(SMSG_CALENDAR_RAID_LOCKOUT_REMOVED, "SMSG_CALENDAR_RAID_LOCKOUT_REMOVED");
    DefS(SMSG_CANCEL_COMBAT, "SMSG_CANCEL_COMBAT");
    DefS(SMSG_CANCEL_AUTO_REPEAT, "SMSG_CANCEL_AUTO_REPEAT");
    DefS(SMSG_AI_REACTION, "SMSG_AI_REACTION");
    // Unit_C reader sub_72B5D8 proves a 21-bit record count, packed unit GUID,
    // and repeated power-selector/value records on the 18414 route.
    DefS(SMSG_POWER_UPDATE, "SMSG_POWER_UPDATE");
    // Directly verified 18414 sound readers: packed source GUID, two packed
    // object GUIDs, and the single-ID music form respectively.
    DefS(SMSG_PLAY_SOUND, "SMSG_PLAY_SOUND");
    DefS(SMSG_PLAY_OBJECT_SOUND, "SMSG_PLAY_OBJECT_SOUND");
    DefS(SMSG_PLAY_MUSIC, "SMSG_PLAY_MUSIC");
    DefS(SMSG_PET_ACTION_SOUND, "SMSG_PET_ACTION_SOUND");
    DefS(SMSG_PET_ACTION_FEEDBACK, "SMSG_PET_ACTION_FEEDBACK");
    DefS(SMSG_PET_MODE, "SMSG_PET_MODE");
    DefS(SMSG_PET_SPELLS, "SMSG_PET_SPELLS");

    // Single quest-giver marker query and its packed-GUID status response.
    DefC(CMSG_QUESTGIVER_STATUS_QUERY, "CMSG_QUESTGIVER_STATUS_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestgiverStatusQueryOpcode);
    DefS(SMSG_QUESTGIVER_STATUS, "SMSG_QUESTGIVER_STATUS");
    DefC(CMSG_GOSSIP_HELLO, "CMSG_GOSSIP_HELLO", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGossipHelloOpcode);
    DefS(SMSG_GOSSIP_MESSAGE, "SMSG_GOSSIP_MESSAGE");
    // The 18414 client echoes the selected option, active menu and packed
    // source GUID; POI uses the dynamic gossip reader installed at slot 229.
    DefC(CMSG_GOSSIP_SELECT_OPTION, "CMSG_GOSSIP_SELECT_OPTION", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGossipSelectOptionOpcode);
    DefS(SMSG_GOSSIP_POI, "SMSG_GOSSIP_POI");
    DefC(CMSG_LIST_INVENTORY, "CMSG_LIST_INVENTORY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleListInventoryOpcode);
    DefS(SMSG_LIST_INVENTORY, "SMSG_LIST_INVENTORY");
    // The 18414 client writes the count followed by interleaved item/vendor
    // GUIDs and reads the paired packed-GUID result around an 8-bit status.
    DefC(CMSG_SELL_ITEM, "CMSG_SELL_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSellItemOpcode);
    // Writer sub_68BC3D sends the logical slot before one packed vendor GUID;
    // success is represented by the ordinary private player/item updates.
    DefC(CMSG_BUYBACK_ITEM, "CMSG_BUYBACK_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleBuybackItem);
    DefS(SMSG_SELL_ITEM, "SMSG_SELL_ITEM");
    // Writer sub_68E11F proves the request. The response names retain reference
    // provenance; their 18414 readers and terminals directly prove purchase
    // stock updates and concrete failure feedback on these wire routes.
    DefC(CMSG_BUY_ITEM, "CMSG_BUY_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleBuyItemOpcode);
    DefS(SMSG_BUY_ITEM, "SMSG_BUY_ITEM");
    DefS(SMSG_BUY_FAILED, "SMSG_BUY_FAILED");
    DefS(SMSG_ITEM_PUSH_RESULT, "SMSG_ITEM_PUSH_RESULT");

    // Directly verified 18414 flight-master status request/reply pair.
    DefC(CMSG_TAXINODE_STATUS_QUERY, "CMSG_TAXINODE_STATUS_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTaxiNodeStatusQueryOpcode);
    DefS(SMSG_TAXINODE_STATUS, "SMSG_TAXINODE_STATUS");
    // The menu request and reply use distinct 18414 packed-GUID layouts;
    // discovery is the client's empty notification before the learned status.
    DefC(CMSG_TAXIQUERYAVAILABLENODES, "CMSG_TAXIQUERYAVAILABLENODES", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTaxiQueryAvailableNodes);
    DefS(SMSG_SHOWTAXINODES, "SMSG_SHOWTAXINODES");
    DefS(SMSG_NEW_TAXI_PATH, "SMSG_NEW_TAXI_PATH");
    // Normal route selection sends destination/source scalars before the
    // packed flight-master GUID and receives one four-bit result.
    DefC(CMSG_ACTIVATETAXI, "CMSG_ACTIVATETAXI", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleActivateTaxiOpcode);
    DefC(CMSG_ACTIVATETAXIEXPRESS, "CMSG_ACTIVATETAXIEXPRESS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleActivateTaxiExpressOpcode);
    // Completion is admitted only through the exact 18414 movement body and
    // the active same-map flight ledger. Cross-map transition remains dormant.
    DefC(CMSG_MOVE_SPLINE_DONE, "CMSG_MOVE_SPLINE_DONE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveSplineDoneOpcode);
    DefS(SMSG_ACTIVATETAXIREPLY, "SMSG_ACTIVATETAXIREPLY");

    // Directly verified 18414 inventory-movement requests. Each handler
    // decodes the packed request before reusing the established item logic.
    DefC(CMSG_SWAP_INV_ITEM, "CMSG_SWAP_INV_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSwapInvItemOpcode);
    DefC(CMSG_SWAP_ITEM, "CMSG_SWAP_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSwapItem);
    DefC(CMSG_AUTOEQUIP_ITEM, "CMSG_AUTOEQUIP_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAutoEquipItemOpcode);
    DefC(CMSG_AUTOSTORE_BAG_ITEM, "CMSG_AUTOSTORE_BAG_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAutoStoreBagItemOpcode);
    DefC(CMSG_SPLIT_ITEM, "CMSG_SPLIT_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSplitItemOpcode);
    // Writer sub_6919D8 emits count, slot and bag; the live zero-count request
    // deletes the complete stack from the selected backpack position.
    DefC(CMSG_DESTROY_ITEM, "CMSG_DESTROY_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleDestroyItemOpcode);
    DefS(SMSG_INVENTORY_CHANGE_FAILURE, "SMSG_INVENTORY_CHANGE_FAILURE");

    // Directly verified 18414 loot-window requests and replies. The handlers
    // validate the packed view identity before reusing authoritative loot state.
    DefC(CMSG_LOOT, "CMSG_LOOT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLootOpcode);
    DefC(CMSG_AUTOSTORE_LOOT_ITEM, "CMSG_AUTOSTORE_LOOT_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAutostoreLootItemOpcode);
    DefC(CMSG_LOOT_MONEY, "CMSG_LOOT_MONEY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLootMoneyOpcode);
    DefC(CMSG_LOOT_RELEASE, "CMSG_LOOT_RELEASE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLootReleaseOpcode);
    DefS(SMSG_LOOT_RESPONSE, "SMSG_LOOT_RESPONSE");
    DefS(SMSG_LOOT_RELEASE_RESPONSE, "SMSG_LOOT_RELEASE_RESPONSE");
    DefS(SMSG_LOOT_REMOVED, "SMSG_LOOT_REMOVED");
    DefS(SMSG_LOOT_MONEY_NOTIFY, "SMSG_LOOT_MONEY_NOTIFY");
    DefS(SMSG_LOOT_CLEAR_MONEY, "SMSG_LOOT_CLEAR_MONEY");
    // The shipped FrameXML group-roll state machine uses this five-packet
    // handshake; every body is direct-reader/writer verified for 18414.
    DefC(CMSG_LOOT_ROLL, "CMSG_LOOT_ROLL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLootRoll);
    DefS(SMSG_LOOT_START_ROLL, "SMSG_LOOT_START_ROLL");
    DefS(SMSG_LOOT_ROLL, "SMSG_LOOT_ROLL");
    DefS(SMSG_LOOT_ROLL_WON, "SMSG_LOOT_ROLL_WON");
    DefS(SMSG_LOOT_ALL_PASSED, "SMSG_LOOT_ALL_PASSED");

    // Directly verified 18414 GameObject use/report requests and the
    // type-dependent animation/page packets sent by established gameplay.
    DefC(CMSG_GAMEOBJ_USE, "CMSG_GAMEOBJ_USE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGameObjectUseOpcode);
    DefC(CMSG_GAMEOBJ_REPORT_USE, "CMSG_GAMEOBJ_REPORT_USE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGameobjectReportUse);
    DefS(SMSG_GAMEOBJECT_CUSTOM_ANIM, "SMSG_GAMEOBJECT_CUSTOM_ANIM");
    DefS(SMSG_GAMEOBJECT_DESPAWN_ANIM, "SMSG_GAMEOBJECT_DESPAWN_ANIM");
    DefS(SMSG_GAMEOBJECT_PAGETEXT, "SMSG_GAMEOBJECT_PAGETEXT");

    // The direct 18414 opener takes no body. The appearance request writes four
    // uint32 fields; the one-uint32 result fires BARBER_SHOP_SUCCESS or an error.
    DefS(SMSG_ENABLE_BARBER_SHOP, "SMSG_ENABLE_BARBER_SHOP");
    DefC(CMSG_ALTER_APPEARANCE, "CMSG_ALTER_APPEARANCE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAlterAppearanceOpcode);
    DefS(SMSG_BARBER_SHOP_RESULT, "SMSG_BARBER_SHOP_RESULT");

    // The direct 18414 terminal leaves consume no payload and display the
    // matching ERR_FISH_ESCAPED / ERR_FISH_NOT_HOOKED client errors.
    DefS(SMSG_FISH_ESCAPED, "SMSG_FISH_ESCAPED");
    DefS(SMSG_FISH_NOT_HOOKED, "SMSG_FISH_NOT_HOOKED");

    // Directly verified 18414 world/quest interactions. Area-trigger reports
    // distinguish enter from leave; the quest marker reply batches packed GUIDs.
    DefC(CMSG_AREATRIGGER, "CMSG_AREATRIGGER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAreaTriggerOpcode);
    DefS(SMSG_AREA_TRIGGER_NO_CORPSE, "SMSG_AREA_TRIGGER_NO_CORPSE");
    DefS(SMSG_EXPLORATION_EXPERIENCE, "SMSG_EXPLORATION_EXPERIENCE");
    DefS(SMSG_LOG_XPGAIN, "SMSG_LOG_XPGAIN");
    DefS(SMSG_LEVELUP_INFO, "SMSG_LEVELUP_INFO");
    DefC(CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY, "CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestgiverStatusMultipleQuery);
    DefS(SMSG_QUESTGIVER_STATUS_MULTIPLE, "SMSG_QUESTGIVER_STATUS_MULTIPLE");

    // Directly verified 18414 quest acquisition flow. These three packed
    // requests drive the exact list and details readers used by QuestFrame.
    DefC(CMSG_QUESTGIVER_HELLO, "CMSG_QUESTGIVER_HELLO", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestgiverHelloOpcode);
    DefS(SMSG_QUESTGIVER_QUEST_LIST, "SMSG_QUESTGIVER_QUEST_LIST");
    DefC(CMSG_QUESTGIVER_QUERY_QUEST, "CMSG_QUESTGIVER_QUERY_QUEST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestgiverQueryQuestOpcode);
    DefS(SMSG_QUESTGIVER_QUEST_DETAILS, "SMSG_QUESTGIVER_QUEST_DETAILS");
    DefC(CMSG_QUESTGIVER_ACCEPT_QUEST, "CMSG_QUESTGIVER_ACCEPT_QUEST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestgiverAcceptQuestOpcode);
    // The direct 18414 empty response closes the quest/gossip frame after acceptance.
    DefS(SMSG_GOSSIP_COMPLETE, "SMSG_GOSSIP_COMPLETE");

    // Directly verified 18414 quest failure feedback: nullable custom text plus
    // reason, quest ID plus InventoryResult, an empty log-full event, and one timer ID.
    DefS(SMSG_QUESTGIVER_QUEST_INVALID, "SMSG_QUESTGIVER_QUEST_INVALID");
    DefS(SMSG_QUESTGIVER_QUEST_FAILED, "SMSG_QUESTGIVER_QUEST_FAILED");
    DefS(SMSG_QUESTLOG_FULL, "SMSG_QUESTLOG_FULL");
    DefS(SMSG_QUESTUPDATE_FAILEDTIMER, "SMSG_QUESTUPDATE_FAILEDTIMER");

    // Direct 18414 reader proof: uint16 progress, objective type, quest ID,
    // uint16 target count, object template ID, then a packed credited GUID.
    DefS(SMSG_QUESTUPDATE_ADD_KILL, "SMSG_QUESTUPDATE_ADD_KILL");

    // The 18414 abandon action carries exactly one quest-log slot byte.
    // Clearing the player's quest slot supplies the client-visible object update;
    // the client action does not require a dedicated response packet.
    DefC(CMSG_QUESTLOG_REMOVE_QUEST, "CMSG_QUESTLOG_REMOVE_QUEST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestLogRemoveQuest);
    DefC(CMSG_QUEST_POI_QUERY, "CMSG_QUEST_POI_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestPOIQueryOpcode);
    DefC(CMSG_QUEST_NPC_QUERY, "CMSG_QUEST_NPC_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestNpcQueryOpcode);
    DefS(SMSG_QUEST_POI_QUERY_RESPONSE, "SMSG_QUEST_POI_QUERY_RESPONSE");
    DefS(SMSG_QUEST_NPC_QUERY_RESPONSE, "SMSG_QUEST_NPC_QUERY_RESPONSE");

    // Directly verified 18414 quest turn-in and reward flow. The client sends
    // a reward item ID, which the handler resolves back to the configured
    // reward-choice slot before applying the existing quest gameplay logic.
    DefC(CMSG_QUESTGIVER_COMPLETE_QUEST, "CMSG_QUESTGIVER_COMPLETE_QUEST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestgiverCompleteQuest);
    DefS(SMSG_QUESTGIVER_REQUEST_ITEMS, "SMSG_QUESTGIVER_REQUEST_ITEMS");
    DefC(CMSG_QUESTGIVER_REQUEST_REWARD, "CMSG_QUESTGIVER_REQUEST_REWARD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestgiverRequestRewardOpcode);
    DefS(SMSG_QUESTGIVER_OFFER_REWARD, "SMSG_QUESTGIVER_OFFER_REWARD");
    DefC(CMSG_QUESTGIVER_CHOOSE_REWARD, "CMSG_QUESTGIVER_CHOOSE_REWARD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestgiverChooseRewardOpcode);
    DefS(SMSG_QUESTGIVER_QUEST_COMPLETE, "SMSG_QUESTGIVER_QUEST_COMPLETE");
    DefS(SMSG_QUESTUPDATE_COMPLETE, "SMSG_QUESTUPDATE_COMPLETE");

    // Quest log metadata is fetched through the client questcache.wdb path
    // after acquisition. The 18414 request and success/absent reply bodies
    // are independently reconstructed from the Wow.exe writer and reader.
    DefC(CMSG_QUEST_QUERY, "CMSG_QUEST_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestQueryOpcode);
    DefS(SMSG_QUEST_QUERY_RESPONSE, "SMSG_QUEST_QUERY_RESPONSE");

    // Build 18414 resolves NPC text through BroadcastText.db2. Rel23.02 stores
    // those IDs explicitly; unmapped legacy rows return an honest cache miss.
    DefC(CMSG_NPC_TEXT_QUERY, "CMSG_NPC_TEXT_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleNpcTextQueryOpcode);
    DefS(SMSG_NPC_TEXT_UPDATE, "SMSG_NPC_TEXT_UPDATE");

    // Empty 18414 status refresh request. The handler replies through the
    // already-converted unified SMSG_LFG_UPDATE_STATUS body.
    DefC(CMSG_LFG_SET_ROLES, "CMSG_LFG_SET_ROLES", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfgSetRolesOpcode);
    DefC(CMSG_LFG_PROPOSAL_RESPONSE, "CMSG_LFG_PROPOSAL_RESPONSE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfgProposalResponseOpcode);
    DefC(CMSG_LFG_GET_STATUS, "CMSG_LFG_GET_STATUS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfgGetStatusOpcode);

    // Direct 18414 LFR-browser request and empty full-replacement response.
    DefC(CMSG_LFG_LFR_JOIN, "CMSG_LFG_LFR_JOIN", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfrJoinOpcode);
    DefC(CMSG_LFG_LFR_LEAVE, "CMSG_LFG_LFR_LEAVE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfrLeaveOpcode);
    DefS(SMSG_LFG_UPDATE_SEARCH, "SMSG_LFG_UPDATE_SEARCH");

    // Unified 18414 lock-info request: 0x7F byte then one player/party bit.
    DefC(CMSG_LFG_LOCK_INFO_REQUEST, "CMSG_LFG_LOCK_INFO_REQUEST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfgLockInfoRequestOpcode);
    DefS(SMSG_LFG_PLAYER_INFO, "SMSG_LFG_PLAYER_INFO");
    DefS(SMSG_LFG_PARTY_INFO, "SMSG_LFG_PARTY_INFO");

    // Empty 18414 raid-lock query and its 20-bit-count, packed-GUID response.
    DefC(CMSG_REQUEST_RAID_INFO, "CMSG_REQUEST_RAID_INFO", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestRaidInfoOpcode);
    DefS(SMSG_RAID_INSTANCE_INFO, "SMSG_RAID_INSTANCE_INFO");

    // Wave 6 creature query request and response.
    DefC(CMSG_CREATURE_QUERY, "CMSG_CREATURE_QUERY", STATUS_LOGGEDIN, PROCESS_INPLACE, &WorldSession::HandleCreatureQueryOpcode);
    DefS(SMSG_CREATURE_QUERY_RESPONSE, "SMSG_CREATURE_QUERY_RESPONSE");

    // Wave 8 game-object query request and response.
    DefC(CMSG_GAMEOBJECT_QUERY, "CMSG_GAMEOBJECT_QUERY", STATUS_LOGGEDIN, PROCESS_INPLACE, &WorldSession::HandleGameObjectQueryOpcode);
    DefS(SMSG_GAMEOBJECT_QUERY_RESPONSE, "SMSG_GAMEOBJECT_QUERY_RESPONSE");

    // The GameObject page packet triggers this shared item/GameObject cache
    // lookup. Both bodies are directly reconstructed from the 18414 client.
    DefC(CMSG_PAGE_TEXT_QUERY, "CMSG_PAGE_TEXT_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandlePageTextQueryOpcode);
    DefS(SMSG_PAGE_TEXT_QUERY_RESPONSE, "SMSG_PAGE_TEXT_QUERY_RESPONSE");

    // Reading a mail letter. The 18414 request writer (sub_600693, the
    // itemtextcache.wdb cache-miss callback) emits a RAW little-endian uint64
    // item GUID - no packed mask, no XOR - and the reply reader immediately
    // after it takes uint8 found (0 = has text), then the same raw uint64,
    // then a null-terminated string. HandleItemTextQuery already builds
    // exactly that; only the registration and admission were missing, so
    // right-clicking a letter did nothing at all.
    DefC(CMSG_ITEM_TEXT_QUERY, "CMSG_ITEM_TEXT_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleItemTextQuery);
    DefS(SMSG_ITEM_TEXT_QUERY_RESPONSE, "SMSG_ITEM_TEXT_QUERY_RESPONSE");

    // Wave 34 corpse location and transport map-position queries.
    DefC(CMSG_CORPSE_QUERY, "CMSG_CORPSE_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleCorpseQueryOpcode);
    DefS(SMSG_CORPSE_QUERY_RESPONSE, "SMSG_CORPSE_QUERY_RESPONSE");

    // Coming back from death. Every handler below already existed; only the
    // registrations were missing, so a character could die and become a ghost
    // -- once PLAYER_FLAGS reached the client -- and then had no way back. The
    // client asked for its corpse and fell silent, because the reply to that
    // is a reclaim it could not send.
    //
    // Release, return to a graveyard, reclaim at the corpse, or accept an
    // assisted resurrection. The 18414 request and response bodies are
    // converted together so none of the legacy raw-GUID readers are exposed.
    DefC(CMSG_REPOP_REQUEST, "CMSG_REPOP_REQUEST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRepopRequestOpcode);
    DefC(CMSG_RETURN_TO_GRAVEYARD, "CMSG_RETURN_TO_GRAVEYARD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleReturnToGraveyard);
    DefC(CMSG_RECLAIM_CORPSE, "CMSG_RECLAIM_CORPSE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleReclaimCorpseOpcode);
    DefS(SMSG_RESURRECT_REQUEST, "SMSG_RESURRECT_REQUEST");
    DefC(CMSG_RESURRECT_RESPONSE, "CMSG_RESURRECT_RESPONSE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleResurrectResponseOpcode);
    DefS(SMSG_SPIRIT_HEALER_CONFIRM, "SMSG_SPIRIT_HEALER_CONFIRM");
    DefC(CMSG_SPIRIT_HEALER_ACTIVATE, "CMSG_SPIRIT_HEALER_ACTIVATE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSpiritHealerActivateOpcode);

    // The 0x0360 collision is settled. Both names claimed that value with
    // binary provenance, and CMSG_SELF_RES was the correct one:
    //
    //   Lua UseSoulstone               -> sub_CCB17C -> packet class sub_686F88
    //     -> vtable off_D64B1C slot 2 = sub_686A4C, which writes opcode 864 = 0x0360
    //   Lua HearthAndResurrectFromArea -> sub_91064C -> packet class sub_6868A5
    //     -> vtable off_D64914 slot 2 = sub_6865BD, which writes opcode 835 = 0x0343
    //
    // CMSG_HEARTH_AND_RESURRECT is therefore 0x0343, and its 0x0360 entry in
    // Opcodes.h has been corrected. Note the sniff catalogue NAMES 0x0360 as
    // CMSG_HEARTH_AND_RESURRECT; that name is shipped-map metadata rather than
    // wire truth, and the binary overrules it.
    //
    // Both packets share body writer nullsub_2, i.e. no body at all, which is
    // why HandleSelfResOpcode ignores recv_data and why the 13 observed 0x0360
    // packets in the corpus all carry a zero-length payload.
    DefC(CMSG_SELF_RES, "CMSG_SELF_RES", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSelfResOpcode);

    // Still dormant: SMSG_RESURRECT_FAILED (0x1253) carries no client leaf, so
    // its value is inherited rather than confirmed, and it has no reader to
    // admit it to. CMSG_HEARTH_AND_RESURRECT now has a settled value AND a
    // handler (WorldSession::HandleHearthandResurrect), but is left unregistered
    // here deliberately: it is its own opcode and deserves its own change with
    // its own gates, rather than riding along on this one.
    DefC(CMSG_CORPSE_MAP_POSITION_QUERY, "CMSG_CORPSE_MAP_POSITION_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleCorpseMapPositionQueryOpcode);
    DefS(SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE, "SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE");

    // Wave 35 spirit-healer location state.
    DefS(SMSG_DEATH_RELEASE_LOC, "SMSG_DEATH_RELEASE_LOC");
    DefS(SMSG_DURABILITY_DAMAGE_DEATH, "SMSG_DURABILITY_DAMAGE_DEATH");

    // Binary-proven scheduled cemetery-list refresh.
    DefC(CMSG_REQUEST_CEMETERY_LIST, "CMSG_REQUEST_CEMETERY_LIST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestCemeteryListOpcode);
    DefS(SMSG_REQUEST_CEMETERY_LIST_RESPONSE, "SMSG_REQUEST_CEMETERY_LIST_RESPONSE");

    // Wave 36 quest-sharing requests, confirmation prompt, and split result paths.
    DefC(CMSG_PUSHQUESTTOPARTY, "CMSG_PUSHQUESTTOPARTY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandlePushQuestToParty);
    DefC(CMSG_QUEST_CONFIRM_ACCEPT, "CMSG_QUEST_CONFIRM_ACCEPT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestConfirmAccept);
    DefC(CMSG_QUEST_PUSH_RESULT, "CMSG_QUEST_PUSH_RESULT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQuestPushResult);
    DefS(SMSG_QUEST_CONFIRM_ACCEPT, "SMSG_QUEST_CONFIRM_ACCEPT");
    DefS(SMSG_QUEST_PUSH_RESULT, "SMSG_QUEST_PUSH_RESULT");
    DefS(SMSG_INITIAL_SETUP, "SMSG_INITIAL_SETUP");
    DefS(SMSG_SET_QUEST_COMPLETED_BIT, "SMSG_SET_QUEST_COMPLETED_BIT");
    DefS(SMSG_CLEAR_QUEST_COMPLETED_BIT, "SMSG_CLEAR_QUEST_COMPLETED_BIT");
    DefS(SMSG_CLEAR_QUEST_COMPLETED_BITS, "SMSG_CLEAR_QUEST_COMPLETED_BITS");

    // The client also sends name queries before worldport ACK while its Player is out of world.
    DefC(CMSG_NAME_QUERY, "CMSG_NAME_QUERY", STATUS_LOGGEDIN_OR_TRANSFER, PROCESS_THREADUNSAFE, &WorldSession::HandleNameQueryOpcode);
    DefS(SMSG_NAME_QUERY_RESPONSE, "SMSG_NAME_QUERY_RESPONSE");

    // The /who list. Both bodies were rebuilt for 18414 -- see MopWhoPackets for the
    // layouts and the captures they were verified against. HandleWhoOpcode has existed
    // all along but was never registered, so /who has always been silent; registering
    // it against the old 3.3.5 reader would have been worse than silence, which is why
    // the reader was rewritten first.
    DefC(CMSG_WHO, "CMSG_WHO", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleWhoOpcode);
    DefS(SMSG_WHO, "SMSG_WHO");

    // Realm-name query. The 18414 client fires this from its name-cache path when a
    // queried character's realm is not yet in its RealmCache; until it is answered the
    // client parks the queried name and never commits it (the name shows "Unknown").
    // CMSG value client-confirmed live (0x1A16, body = uint32 realmId); response
    // contract RE-verified against the client handler sub_1403073A0.
    DefC(CMSG_REALM_NAME_QUERY, "CMSG_REALM_NAME_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRealmNameQueryOpcode);
    DefS(SMSG_REALM_NAME_QUERY_RESPONSE, "SMSG_REALM_NAME_QUERY_RESPONSE");

    // Wave 7 compact time query requests and responses.
    DefC(CMSG_QUERY_TIME, "CMSG_QUERY_TIME", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQueryTimeOpcode);
    DefS(SMSG_QUERY_TIME_RESPONSE, "SMSG_QUERY_TIME_RESPONSE");
    DefC(CMSG_PLAYED_TIME, "CMSG_PLAYED_TIME", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandlePlayedTime);
    DefS(SMSG_PLAYED_TIME, "SMSG_PLAYED_TIME");

    // Wave 10 core 5.4.8 player movement and server relay.
    // Teleport acknowledgements. Both handlers already existed but were never
    // registered, so the acks from the client reached nothing and the teleport
    // semaphore was never cleared. Player::Update skips the visibility observer
    // sweep while IsBeingTeleported(), so one same-map teleport stopped all
    // object creation for the rest of the session while movement and combat
    // broadcasts kept flowing. Retail 18414 captures pair SMSG_MOVE_TELEPORT
    // 1:1 with CMSG_MOVE_TELEPORT_ACK, 1,522 of each, so the ack always comes.
    DefC(CMSG_MOVE_TELEPORT_ACK, "CMSG_MOVE_TELEPORT_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveTeleportAckOpcode);
    // The worldport ack is 0x1FAD in 18414, not the inherited 0x00E0 -- that
    // value is CMSG_CHAR_ENUM here, and registering it there overwrote the
    // char-enum slot and hung every client on "Retrieving character list".
    // 0x1FAD was taken from a live cross-map teleport that hung on the loading
    // screen, then checked against the corpus before being registered: it
    // occurs 2,022 times, always CMSG, always zero-length, against 2,022
    // SMSG_NEW_WORLD -- an exact 1:1 pairing, 1,968 of them four or five
    // records after the NEW_WORLD. It is claimed by nothing else, so unlike
    // 0x00E0 this cannot displace an existing handler.
    DefC(MSG_MOVE_WORLDPORT_ACK, "MSG_MOVE_WORLDPORT_ACK", STATUS_TRANSFER, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveWorldportAckOpcode);
    DefC(MSG_MOVE_HEARTBEAT, "MSG_MOVE_HEARTBEAT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_START_FORWARD, "CMSG_MOVE_START_FORWARD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_START_BACKWARD, "CMSG_MOVE_START_BACKWARD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_STOP, "CMSG_MOVE_STOP", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_SET_FACING, "CMSG_MOVE_SET_FACING", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_FALL_LAND, "CMSG_MOVE_FALL_LAND", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_START_STRAFE_LEFT, "CMSG_MOVE_START_STRAFE_LEFT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_START_STRAFE_RIGHT, "CMSG_MOVE_START_STRAFE_RIGHT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_STOP_STRAFE, "CMSG_MOVE_STOP_STRAFE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_JUMP, "CMSG_MOVE_JUMP", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_START_TURN_LEFT, "CMSG_MOVE_START_TURN_LEFT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_START_TURN_RIGHT, "CMSG_MOVE_START_TURN_RIGHT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_STOP_TURN, "CMSG_MOVE_STOP_TURN", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    // Retail sends this as the initial transition from world movement to a
    // transport GUID/local-offset movement block, before regular heartbeats.
    DefC(CMSG_MOVE_CHNG_TRANSPORT, "CMSG_MOVE_CHNG_TRANSPORT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    // Flight-input snapshots use the same relocation path as ground movement,
    // but their 18414 writers have distinct 72-element layouts. Register the
    // four only with their matching MovementStructures.h readers.
    DefC(CMSG_MOVE_SET_FLY, "CMSG_MOVE_SET_FLY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_START_ASCEND, "CMSG_MOVE_START_ASCEND", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_STOP_ASCEND, "CMSG_MOVE_STOP_ASCEND", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    DefC(CMSG_MOVE_START_DESCEND, "CMSG_MOVE_START_DESCEND", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMovementOpcodes);
    // Knockback is one protocol transaction: the owner request, client ack,
    // and observer relay become reachable only with all three 18414 layouts.
    DefS(SMSG_MOVE_KNOCK_BACK, "SMSG_MOVE_KNOCK_BACK");
    DefC(CMSG_MOVE_KNOCK_BACK_ACK, "CMSG_MOVE_KNOCK_BACK_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveKnockBackAck);
    DefS(SMSG_MOVE_UPDATE_KNOCK_BACK, "SMSG_MOVE_UPDATE_KNOCK_BACK");
    DefC(CMSG_FORCE_SWIM_SPEED_CHANGE_ACK, "CMSG_FORCE_SWIM_SPEED_CHANGE_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleForceSpeedChangeAckOpcodes);
    // Forced-state acknowledgements are parsed and validated against the active
    // mover, but never author movement state or trigger an observer relay.
    DefC(CMSG_FORCE_MOVE_ROOT_ACK, "CMSG_FORCE_MOVE_ROOT_ACK", STATUS_LOGGEDIN_OR_TRANSFER, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveRootAck);
    DefC(CMSG_FORCE_MOVE_UNROOT_ACK, "CMSG_FORCE_MOVE_UNROOT_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveUnRootAck);
    DefC(CMSG_MOVE_WATER_WALK_ACK, "CMSG_MOVE_WATER_WALK_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveWaterWalkAck);
    DefS(SMSG_FORCE_MOVE_ROOT, "SMSG_FORCE_MOVE_ROOT");
    DefS(SMSG_FORCE_MOVE_UNROOT, "SMSG_FORCE_MOVE_UNROOT");
    DefS(SMSG_MOVE_WATER_WALK, "SMSG_MOVE_WATER_WALK");
    DefS(SMSG_MOVE_LAND_WALK, "SMSG_MOVE_LAND_WALK");
    DefS(SMSG_PLAYER_MOVE, "SMSG_PLAYER_MOVE");
    DefS(SMSG_MONSTER_MOVE, "SMSG_MONSTER_MOVE");
    DefS(SMSG_SPLINE_MOVE_SET_NORMAL_FALL, "SMSG_SPLINE_MOVE_SET_NORMAL_FALL");
    DefS(SMSG_SPLINE_MOVE_SET_RUN_MODE, "SMSG_SPLINE_MOVE_SET_RUN_MODE");
    DefS(SMSG_SPLINE_MOVE_SET_WALK_MODE, "SMSG_SPLINE_MOVE_SET_WALK_MODE");
    DefS(SMSG_SPLINE_MOVE_SET_WATER_WALK, "SMSG_SPLINE_MOVE_SET_WATER_WALK");

    // Names for the packet log. DefS is logging metadata, NOT a gate -- these
    // four transmit either way. Without a row they appear as
    // "OPCODE: UNKNOWN (0x159F)", which is what they did during the live test
    // that confirmed this family, and it made them invisible to an
    // opcode-name search of the capture.
    DefS(SMSG_MOVE_GRAVITY_DISABLE, "SMSG_MOVE_GRAVITY_DISABLE");
    DefS(SMSG_MOVE_GRAVITY_ENABLE, "SMSG_MOVE_GRAVITY_ENABLE");
    DefS(SMSG_SPLINE_MOVE_GRAVITY_DISABLE, "SMSG_SPLINE_MOVE_GRAVITY_DISABLE");
    DefS(SMSG_SPLINE_MOVE_GRAVITY_ENABLE, "SMSG_SPLINE_MOVE_GRAVITY_ENABLE");
    DefS(SMSG_SPLINE_MOVE_SET_FEATHER_FALL, "SMSG_SPLINE_MOVE_SET_FEATHER_FALL");
    DefS(SMSG_SPLINE_MOVE_SET_LAND_WALK, "SMSG_SPLINE_MOVE_SET_LAND_WALK");

    // Binary-proven 18414 integrated spell-cast request.
    DefC(CMSG_CAST_SPELL, "CMSG_CAST_SPELL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleCastSpellOpcode);
    DefC(CMSG_USE_ITEM, "CMSG_USE_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleUseItemOpcode);
    DefC(CMSG_CANCEL_AURA, "CMSG_CANCEL_AURA", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleCancelAuraOpcode);
    DefS(SMSG_CAST_FAILED, "SMSG_CAST_FAILED");
    DefS(SMSG_PET_CAST_FAILED, "SMSG_PET_CAST_FAILED");
    DefS(SMSG_SPELL_START, "SMSG_SPELL_START");
    DefS(SMSG_SPELL_GO, "SMSG_SPELL_GO");
    DefS(SMSG_SPELL_COOLDOWN, "SMSG_SPELL_COOLDOWN");
    DefS(SMSG_CLEAR_COOLDOWNS, "SMSG_CLEAR_COOLDOWNS");
    DefS(SMSG_COOLDOWN_EVENT, "SMSG_COOLDOWN_EVENT");
    DefS(SMSG_ITEM_COOLDOWN, "SMSG_ITEM_COOLDOWN");
    DefS(SMSG_CLEAR_TARGET, "SMSG_CLEAR_TARGET");
    DefS(SMSG_LEARNED_SPELL, "SMSG_LEARNED_SPELL");
    DefS(SMSG_REMOVED_SPELL, "SMSG_REMOVED_SPELL");
    DefS(SMSG_SUPERCEDED_SPELL, "SMSG_SUPERCEDED_SPELL");
    DefS(SMSG_PET_LEARNED_SPELL, "SMSG_PET_LEARNED_SPELL");
    DefS(SMSG_PET_REMOVED_SPELL, "SMSG_PET_REMOVED_SPELL");

    // Live 18414 guild invite: 9-bit player-name length followed by raw bytes.
    DefC(CMSG_GUILD_INVITE, "CMSG_GUILD_INVITE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGuildInviteOpcode);

    // Guild event packets split from the pre-MoP generic guild-event packet.
    DefS(SMSG_GUILD_EVENT_MOTD, "SMSG_GUILD_EVENT_MOTD");
    DefS(SMSG_GUILD_EVENT_PLAYER_JOINED, "SMSG_GUILD_EVENT_PLAYER_JOINED");
    DefS(SMSG_GUILD_EVENT_PRESENCE_CHANGE, "SMSG_GUILD_EVENT_PRESENCE_CHANGE");
    DefS(SMSG_GUILD_EVENT_PLAYER_LEFT, "SMSG_GUILD_EVENT_PLAYER_LEFT");
    DefS(SMSG_GUILD_RANKS_UPDATE, "SMSG_GUILD_RANKS_UPDATE");
    DefS(SMSG_GUILD_EVENT_NEW_LEADER, "SMSG_GUILD_EVENT_NEW_LEADER");
    DefS(SMSG_GUILD_EVENT_DISBANDED, "SMSG_GUILD_EVENT_DISBANDED");
    DefS(SMSG_GUILD_COMMAND_RESULT, "SMSG_GUILD_COMMAND_RESULT");

    // Guild bank permissions query. The request carries no body at all (2,089 corpus
    // observations, every one of them zero bytes) so there is no reader to get wrong,
    // and the reply is now byte-exact against retail: mop_guild_packets compares the
    // generated packet against capture-000006 seq 1959 in full. All 2,080 corpus
    // observations of the reply are exactly 83 bytes.
    //
    // Note that body size alone would NOT have been sufficient warrant here. The
    // inherited body also totalled 83 bytes while being wrong three ways over -- the
    // field order, a 23-bit tab count where the client reads 21, and the tab pairs
    // written rights-first instead of slots-first. 21 and 23 bits both round to the
    // same three bytes, which is exactly why the fixture compares bytes and not length.
    //
    // Guild info query, the heaviest member of the family at 30,939 observations and
    // the last one to land. Both halves are now proven against retail.
    //
    // The request reader takes its two interleaved guid orders from the client's own
    // send serializer sub_665EE4 and is fixture-locked against two captures. The reply
    // is byte-exact: mop_guild_packets rebuilds capture-000004 seq 39473 in full, all
    // 133 bytes of a four-rank guild.
    //
    // The inherited reply was not a variant of the right packet, it was a different one
    // -- a raw ObjectGuid, null-terminated strings and always ten ranks. The 18414 body
    // is a guid bit, a has-data bit, a 21-bit rank count, four guid bits, a 7-bit name
    // length per rank, four more guid bits, a 7-bit guild-name length, seven more guid
    // bits, a flush, the byte block, and then the guid's present bytes A SECOND TIME in
    // a different order. That duplication is real; the capture carries both copies and
    // they are identical.
    DefC(CMSG_GUILD_QUERY, "CMSG_GUILD_QUERY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGuildQueryOpcode);
    DefS(SMSG_GUILD_QUERY_RESPONSE, "SMSG_GUILD_QUERY_RESPONSE");
    DefC(CMSG_GUILD_PERMISSIONS, "CMSG_GUILD_PERMISSIONS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGuildPermissions);
    DefS(SMSG_GUILD_PERMISSIONS, "SMSG_GUILD_PERMISSIONS");

    // Guild rank query. The reader takes its guid order from the client's own send
    // serializer sub_C860F3, and the reply is now byte-exact against retail:
    // mop_guild_packets rebuilds capture-000019 seq 185 in full, all 447 bytes of a
    // five-rank guild carrying the stock MoP rank names.
    //
    // The inherited reply was wrong in ways no length check could see. It wrote an
    // 18-bit rank count where the client reads 17, and ordered each rank as index,
    // tabs, money, rights, name, id where the client reads index, money, tabs, name,
    // id, rights. Both orderings total 80 bytes plus the name, so the packet came out
    // the right length and the wrong shape. The capture settles it: the first name
    // begins 72 bytes into the body, which is index plus money plus the eight tab
    // pairs, and an 18-bit count would have claimed ten ranks in a 447-byte packet
    // that cannot hold more than five.
    DefC(CMSG_GUILD_QUERY_RANKS, "CMSG_GUILD_QUERY_RANKS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGuildQueryRanksOpcode);
    DefS(SMSG_GUILD_QUERY_RANKS_RESULT, "SMSG_GUILD_QUERY_RANKS_RESULT");

    // Full client-side guild-achievement tracking snapshot. The core has no
    // matching backend, so the handler validates and consumes it without state.
    DefC(CMSG_GUILD_SET_ACHIEVEMENT_TRACKING, "CMSG_GUILD_SET_ACHIEVEMENT_TRACKING", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGuildSetAchievementTracking);

    // Guild roster. The reader takes its two interleaved guid orders from the client's
    // send serializer sub_C85E7C, and the reply is byte-exact against retail:
    // mop_guild_packets rebuilds capture-000019 seq 923 in full, all 235 bytes of a
    // two-member guild.
    //
    // The inherited reply was wrong in every dimension. It wrote the MOTD length
    // before the member count where the client reads the count first, at 11 and 18
    // bits where the client reads 17 and 10, an info length of 12 bits where the
    // client reads 11, a 7-bit name length where the client reads 6, and a different
    // order again for both the per-member bit block and the member byte data and the
    // guild-wide tail. The capture is unambiguous: the first 17 bits read 2 and the
    // packet carries exactly two member names, and the next 10 bits read 24 against a
    // 24-character MOTD.
    DefC(CMSG_GUILD_ROSTER, "CMSG_GUILD_ROSTER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGuildRosterOpcode);
    DefS(SMSG_GUILD_ROSTER, "SMSG_GUILD_ROSTER");

    // Live-log guild-bank withdrawal allowance query. The 18414 request is
    // empty and its response contains one uint64 remaining allowance.
    DefC(CMSG_GUILD_BANK_MONEY_WITHDRAWN, "CMSG_GUILD_BANK_MONEY_WITHDRAWN", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGuildBankMoneyWithdrawn);
    DefS(SMSG_GUILD_BANK_MONEY_WITHDRAWN, "SMSG_GUILD_BANK_MONEY_WITHDRAWN");

    // Wave 32 tabard-vendor interaction and guild-emblem save.
    DefC(CMSG_TABARD_VENDOR_ACTIVATE, "CMSG_TABARD_VENDOR_ACTIVATE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTabardVendorActivateOpcode);
    DefS(SMSG_TABARD_VENDOR_ACTIVATE, "SMSG_TABARD_VENDOR_ACTIVATE");
    DefC(CMSG_SAVE_GUILD_EMBLEM, "CMSG_SAVE_GUILD_EMBLEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSaveGuildEmblemOpcode);
    DefS(SMSG_SAVE_GUILD_EMBLEM, "SMSG_SAVE_GUILD_EMBLEM");

    // Wave 33 innkeeper bind confirmation and completion.
    DefC(CMSG_BINDER_ACTIVATE, "CMSG_BINDER_ACTIVATE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleBinderActivateOpcode);
    DefS(SMSG_BINDER_CONFIRM, "SMSG_BINDER_CONFIRM");
    DefS(SMSG_PLAYERBOUND, "SMSG_PLAYERBOUND");

    // Wave 22 LFG boot-vote update, binary-named LFG_BOOT_PLAYER.
    DefS(SMSG_LFG_BOOT_PLAYER, "SMSG_LFG_BOOT_PLAYER");

    // Wave 23 unified 5.4.8 LFG player/party queue status.
    DefS(SMSG_LFG_UPDATE_STATUS, "SMSG_LFG_UPDATE_STATUS");

    // Direct 18414 leaf: periodic queue wait estimates and role vacancies.
    DefS(SMSG_LFG_QUEUE_STATUS, "SMSG_LFG_QUEUE_STATUS");
    DefS(SMSG_LFG_JOIN_RESULT, "SMSG_LFG_JOIN_RESULT");
    // 4-byte body, a single packed dungeon entry -- see WorldSession::SendLfgOfferContinue.
    DefS(SMSG_LFG_OFFER_CONTINUE, "SMSG_LFG_OFFER_CONTINUE");
    DefS(SMSG_LFG_PROPOSAL_UPDATE, "SMSG_LFG_PROPOSAL_UPDATE");
    DefS(SMSG_LFG_ROLE_CHECK_UPDATE, "SMSG_LFG_ROLE_CHECK_UPDATE");
    DefS(SMSG_LFG_TELEPORT_DENIED, "SMSG_LFG_TELEPORT_DENIED");
    // Body is a single MSB-first bit (0x80 out, 0x00 in) -- see HandleLfgTeleportOpcode.
    DefC(CMSG_LFG_TELEPORT, "CMSG_LFG_TELEPORT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfgTeleportOpcode);
    // Vote on an in-progress kick. Body is a single MSB-first bit and nothing else:
    // the 18414 writer sub_688B4B (packet class vtable 0xD63364, header virtual
    // sub_661F56 writing 6078) is exactly WriteBit(agree) + FlushBits. The client
    // does not say WHICH boot -- the session identifies the voter and the voter's
    // group identifies the vote.
    DefC(CMSG_LFG_BOOT_PLAYER_VOTE, "CMSG_LFG_BOOT_PLAYER_VOTE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfgBootPlayerVoteOpcode);
    // The role-check confirmation each member receives as others answer. Layout from
    // the 18414 reader sub_6E921A (handler 0x985605): nine mask bits whose SIXTH is
    // `accepted` rather than a guid bit, then guid bytes 0,3,6, the roles dword, then
    // 5,1,4,2,7. Confirmed against eight corpus packets across seven captures.
    DefS(SMSG_ROLE_CHOSEN, "SMSG_ROLE_CHOSEN");
    // Completion reward. Meanings from the consumer at 0x989771 and Lua sub_986CDD;
    // field order from 13 corpus payloads that decode with zero leftover.
    DefS(SMSG_LFG_PLAYER_REWARD, "SMSG_LFG_PLAYER_REWARD");

    // Wave 13 talent-respec confirmation request and prompt.
    DefC(CMSG_CONFIRM_RESPEC_WIPE, "CMSG_CONFIRM_RESPEC_WIPE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTalentWipeConfirmOpcode);
    DefS(SMSG_RESPEC_WIPE_CONFIRM, "SMSG_RESPEC_WIPE_CONFIRM");

    // Wave 14 party-member statistics request and shared delta/full response.
    DefC(CMSG_REQUEST_PARTY_MEMBER_STATS, "CMSG_REQUEST_PARTY_MEMBER_STATS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestPartyMemberStatsOpcode);
    DefS(SMSG_PARTY_MEMBER_STATS, "SMSG_PARTY_MEMBER_STATS");

    // Wave 20 full party roster/update request and response.
    DefC(CMSG_GROUP_REQUEST_JOIN_UPDATES, "CMSG_GROUP_REQUEST_JOIN_UPDATES", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupRequestJoinUpdates);
    DefS(SMSG_GROUP_LIST, "SMSG_GROUP_LIST");
    DefC(CMSG_GROUP_INVITE_RESPONSE, "CMSG_GROUP_INVITE_RESPONSE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupInviteResponseOpcode);

    // The invite request and its popup are promoted TOGETHER, which is the whole
    // point of the pairing rule: the request is the only path by which a group
    // can be created at all, and it is worthless unless the invitee's client can
    // be told about it. Until now the response half was registered while the ask
    // half was not, so an invite was dropped before reaching its handler and the
    // invitee saw nothing.
    //
    // SMSG_GROUP_INVITE is admitted in IsEnterWorldConverted alongside this, on
    // a body rebuilt from the client reader and proved byte-exact against real
    // captured popups. The inherited builder it replaces could not have been
    // admitted safely -- its ceiling was 43 bytes against an observed minimum
    // of 56.
    //
    // Known gap, deliberately not hidden: SMSG_PARTY_COMMAND_RESULT is still
    // unadmitted, so a REFUSED invite ("no such player", "already in a group")
    // produces no message to the inviter. A successful invite is unaffected.
    // That reply is the next pair in this wave.
    DefC(CMSG_GROUP_INVITE, "CMSG_GROUP_INVITE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupInviteOpcode);
    DefS(SMSG_GROUP_INVITE, "SMSG_GROUP_INVITE");

    // Leaving a group. The 18414 client sends 0x1798 for "Leave Party" --
    // observed live, logged as an unhandled opcode -- and its reply
    // SMSG_GROUP_DESTROYED 0x1B27 is an EMPTY body, which the corpus confirms
    // at min = max = 0 bytes over 100 packets. Group.cpp already initialises it
    // to zero length, so there is no layout to derive and nothing to misparse.
    //
    // The request body is likewise not a risk: HandleGroupDisbandOpcode ignores
    // it entirely, so the single byte the client sends cannot be misread. The
    // corpus records exactly 1 byte across 393 packets.
    //
    // Registration and admission are paired here for the same reason as the
    // invite: leaving is worthless if the client is never told the group is
    // gone, and it would sit in a party frame it no longer belongs to.
    DefC(CMSG_GROUP_DISBAND, "CMSG_GROUP_DISBAND", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupDisbandOpcode);
    DefS(SMSG_GROUP_DESTROYED, "SMSG_GROUP_DESTROYED");

    // The shared result message for EVERY group operation -- invite, uninvite,
    // leader change, disband. Until now it was built and dropped, so a refused
    // operation told the player nothing.
    //
    // Unusually, its inherited flat body is already correct at 18414: this
    // opcode never went bit-packed. Verified against two captured bodies,
    //
    //   21 B  01 00 00 00 | 00 | 1A 00 00 00 | 00000000 | 8x00
    //         op=1, name="", result=26
    //   29 B  02 00 00 00 | "Jazharka" 00 | 00 00 00 00 | 00000000 | 8x00
    //         op=2, name="Jazharka", result=0
    //
    // which is exactly uint32 operation, NUL-terminated name, uint32 result,
    // uint32 LFD cooldown, ObjectGuid -- the layout SendPartyResult already
    // writes, sizes included. It is admitted unchanged; do NOT "convert" it to
    // a packed body, because the client reads it flat.
    DefS(SMSG_PARTY_COMMAND_RESULT, "SMSG_PARTY_COMMAND_RESULT");

    // Removing a member. The 18414 client sends 0x0CE1 -- observed live -- and
    // NOT CMSG_GROUP_UNINVITE 0x1076, which is a stale pre-18414 enum the client
    // never emits. Its reply SMSG_GROUP_UNINVITE 0x1313 is an empty body,
    // confirmed by the corpus at min = max = 0 over 86 packets.
    //
    // The reader was rebuilt for this: the legacy one took a RAW ObjectGuid and
    // skipped a std::string, while the real body is a 0x7F marker, a bit-packed
    // GUID and an 8-bit-length reason string. Registering it against the old
    // reader would have misparsed rather than failed.
    DefC(CMSG_GROUP_UNINVITE_GUID, "CMSG_GROUP_UNINVITE_GUID", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupUninviteGuidOpcode);
    DefS(SMSG_GROUP_UNINVITE, "SMSG_GROUP_UNINVITE");

    // Loot rules. One opcode gates three separate leader controls -- the loot
    // method submenu, "set as master looter", and the loot threshold submenu --
    // plus five slash commands, all of which did nothing.
    //
    // Its reply needs no admission of its own: the handler answers with
    // Group::SendUpdate, and SMSG_GROUP_LIST already carries lootMethod,
    // lootThreshold and the master-looter GUID, so the display half has been
    // working all along with nothing able to change it.
    //
    // The reader was rebuilt: the legacy one took uint32 + raw ObjectGuid +
    // uint32 with no marker, so it folded the 0x7F marker and the method byte
    // into a single bogus 32-bit loot method.
    DefC(CMSG_LOOT_METHOD, "CMSG_LOOT_METHOD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLootMethodOpcode);

    // Promotion. Both requests use the same 0x7F marker plus packed GUID family
    // as uninvite and loot rules, on readers rebuilt from their client writers.
    // The assistant flag is a NINTH BIT inside the mask, not a trailing byte.
    //
    // SMSG_GROUP_SET_LEADER is admitted with them and its body was rebuilt: the
    // inherited sender wrote a NUL-terminated name, while the client reads a
    // byte then a SIX-BIT length then the raw name. Three captured bodies whose
    // second byte is exactly (length << 2) pin it.
    //
    // Assistant answers over the already-admitted SMSG_GROUP_LIST, so it needs
    // no admission of its own.
    DefC(CMSG_GROUP_SET_LEADER, "CMSG_GROUP_SET_LEADER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupSetLeaderOpcode);
    DefS(SMSG_GROUP_SET_LEADER, "SMSG_GROUP_SET_LEADER");
    DefC(CMSG_GROUP_ASSISTANT_LEADER, "CMSG_GROUP_ASSISTANT_LEADER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupAssistantLeaderOpcode);

    // /roll and /random. Everything except the request constant was already in
    // place: the handler validates its own range, and SMSG_RANDOM_ROLL 0x141A is
    // a converted body that is already admitted. The value we HAD, MSG_RANDOM_ROLL
    // 0x0905, is a 4.3.4 carry-over the 18414 client never sends -- registering
    // that would have bound the handler to an opcode nothing emits, which looks
    // like a working feature that simply never fires.
    DefC(CMSG_RANDOM_ROLL, "CMSG_RANDOM_ROLL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRandomRollOpcode);

    // Moving a raid member between subgroups. Same family as the others, but
    // the subgroup number LEADS and the 0x7F marker is second. Answers over the
    // already-admitted SMSG_GROUP_LIST, so no reply admission is needed.
    //
    // The legacy reader took a std::string first, so it would have read the
    // subgroup byte as a string length.
    DefC(CMSG_GROUP_CHANGE_SUB_GROUP, "CMSG_GROUP_CHANGE_SUB_GROUP", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupChangeSubGroupOpcode);

    // Converting between party and raid. ONE opcode carries BOTH directions,
    // distinguished by a single bit -- 0x80 to raid, 0x00 to party -- which is
    // why this was held until the handler stopped discarding its body. The
    // polarity comes from the client's own Lua natives, identical apart from
    // that value (sub_9056D2 ConvertToRaid sets it, sub_905736 ConvertToParty
    // clears it).
    //
    // Group::ConvertToParty is new; only the raid direction existed before, so
    // "Convert to Party" had nothing to call even once the bit was read.
    // Answers over the already-admitted SMSG_GROUP_LIST and
    // SMSG_PARTY_COMMAND_RESULT.
    DefC(CMSG_GROUP_RAID_CONVERT, "CMSG_GROUP_RAID_CONVERT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupRaidConvertOpcode);

    // Main tank and main assist. The value we had, MSG_PARTY_ASSIGNMENT 0x0424,
    // is a 4.3.4 carry-over the 18414 client never sends -- the third stale
    // value found in this surface, after CMSG_GROUP_UNINVITE and
    // MSG_RANDOM_ROLL. The real one is CMSG_SET_PARTY_ASSIGNMENT 0x1802.
    //
    // Reader rebuilt: the apply flag is a ninth mask bit, not the second byte,
    // which the legacy reader took it for before failing to find the GUID at
    // all. Answers over the already-admitted SMSG_GROUP_LIST.
    DefC(CMSG_SET_PARTY_ASSIGNMENT, "CMSG_SET_PARTY_ASSIGNMENT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandlePartyAssignmentOpcode);

    // The raid "Everyone is Assistant" toggle. Body is a 0x7F marker and one
    // bit. This one has ZERO corpus traffic at 18414, so unlike the rest of
    // this wave its layout rests on the client writer alone -- a rare action
    // rather than an invented value, since the writer is in the binary and the
    // UI exposes the toggle. Answers over the already-admitted SMSG_GROUP_LIST.
    DefC(CMSG_SET_EVERYONE_IS_ASSISTANT, "CMSG_SET_EVERYONE_IS_ASSISTANT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupEveryoneIsAssistantOpcode);

    // Roles and the role check. Both were UNDECLARED -- the client sends them
    // and they were logged as unhandled UNKNOWN opcodes.
    //
    // SMSG_GROUP_LIST has always carried a per-member role byte, but nothing
    // filled it, so every member reported "no role" no matter what they picked.
    // Storing the choice is what makes the roster meaningful, which is why no
    // separate reply needs admitting.
    DefC(CMSG_GROUP_SET_ROLES, "CMSG_GROUP_SET_ROLES", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupSetRolesOpcode);
    // The role check is now complete: the request is parsed and authorised, and
    // SMSG_GROUP_ROLE_POLL_INFORM 0x1007 carries the prompt to every member.
    // Its body was recovered from two reference implementations that agree and
    // verified byte-exact against three captured bodies.
    DefC(CMSG_GROUP_INITIATE_ROLE_POLL, "CMSG_GROUP_INITIATE_ROLE_POLL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGroupInitiateRolePollOpcode);
    DefS(SMSG_GROUP_ROLE_POLL_INFORM, "SMSG_GROUP_ROLE_POLL_INFORM");

    // The client sends raw Difficulty.dbc ids. The handlers translate those ids
    // into the core's separate dungeon/raid key spaces before resetting binds.
    // The complete reset-result family is now recovered and admitted atomically,
    // so a successful or refused reset is never made invisible by the send gate.
    DefC(CMSG_SET_DUNGEON_DIFFICULTY, "CMSG_SET_DUNGEON_DIFFICULTY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetDungeonDifficultyOpcode);
    DefC(CMSG_SET_RAID_DIFFICULTY, "CMSG_SET_RAID_DIFFICULTY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetRaidDifficultyOpcode);
    DefC(CMSG_RESET_INSTANCES, "CMSG_RESET_INSTANCES", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleResetInstancesOpcode);

    // Wave 15 stable-pet list request, list response, and operation result.
    DefC(CMSG_REQUEST_STABLED_PETS, "CMSG_REQUEST_STABLED_PETS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleListStabledPetsOpcode);
    DefS(SMSG_PET_STABLE_LIST, "SMSG_PET_STABLE_LIST");
    DefS(SMSG_STABLE_RESULT, "SMSG_STABLE_RESULT");

    // The 18414 client clears its local journal before this empty request.
    // Return a binary-safe empty, writable journal until collection persistence exists.
    DefC(CMSG_BATTLE_PET_REQUEST_JOURNAL, "CMSG_BATTLE_PET_REQUEST_JOURNAL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleBattlePetRequestJournal);
    DefS(SMSG_BATTLE_PET_JOURNAL, "SMSG_BATTLE_PET_JOURNAL");

    // Wave 16 ready-check exchange. All five values and bodies are recovered
    // directly from the 18414 client; server-side state/recipient policy is
    // deliberately kept in Group and GroupHandler.
    DefC(CMSG_DO_READY_CHECK, "CMSG_DO_READY_CHECK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRaidReadyCheckOpcode);
    DefC(CMSG_RAID_READY_CHECK_CONFIRM, "CMSG_RAID_READY_CHECK_CONFIRM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRaidReadyCheckConfirmOpcode);
    DefS(SMSG_RAID_READY_CHECK, "SMSG_RAID_READY_CHECK");
    DefS(SMSG_RAID_READY_CHECK_CONFIRM, "SMSG_RAID_READY_CHECK_CONFIRM");
    DefS(SMSG_RAID_READY_CHECK_COMPLETED, "SMSG_RAID_READY_CHECK_COMPLETED");

    // Wave 27 minimap ping and raid target markers. The inbound serializers
    // and all three outbound readers are recovered directly from Wow.exe.
    DefC(CMSG_MINIMAP_PING, "CMSG_MINIMAP_PING", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMinimapPingOpcode);
    DefC(CMSG_RAID_TARGET_UPDATE, "CMSG_RAID_TARGET_UPDATE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRaidTargetUpdateOpcode);
    DefS(SMSG_MINIMAP_PING, "SMSG_MINIMAP_PING");
    DefS(SMSG_RAID_TARGET_UPDATE_ALL, "SMSG_RAID_TARGET_UPDATE_ALL");
    DefS(SMSG_RAID_TARGET_UPDATE_SINGLE, "SMSG_RAID_TARGET_UPDATE_SINGLE");

    // Wave 28 auction hello plus the merged sold/expired owner notification.
    // All three bodies and both client receive routes are recovered directly
    // from Wow.exe; 0x1A8E selects sold (1) versus expired (0).
    DefC(CMSG_AUCTION_HELLO, "CMSG_AUCTION_HELLO", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleAuctionHelloOpcode);
    DefS(SMSG_AUCTION_HELLO, "SMSG_AUCTION_HELLO");
    DefS(SMSG_AUCTION_COMMAND_RESULT, "SMSG_AUCTION_COMMAND_RESULT");
    DefS(SMSG_AUCTION_OWNER_NOTIFICATION, "SMSG_AUCTION_OWNER_NOTIFICATION");
    DefS(SMSG_AUCTION_WON_NOTIFICATION, "SMSG_AUCTION_WON_NOTIFICATION");
    DefS(SMSG_AUCTION_OUTBID_NOTIFICATION, "SMSG_AUCTION_OUTBID_NOTIFICATION");
    DefS(SMSG_AUCTION_BID_UPDATE_NOTIFICATION, "SMSG_AUCTION_BID_UPDATE_NOTIFICATION");

    // Wave 17 next-mail-time query and result.
    DefC(CMSG_MAIL_QUERY_NEXT_TIME, "CMSG_MAIL_QUERY_NEXT_TIME", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleQueryNextMailTime);
    DefS(SMSG_MAIL_QUERY_NEXT_TIME_RESULT, "SMSG_MAIL_QUERY_NEXT_TIME_RESULT");

    // Wave 18 rated-battleground self statistics. The inspect exchange is a
    // separate protocol and is deliberately not registered here.
    DefC(CMSG_BATTLEFIELD_STATUS, "CMSG_BATTLEFIELD_STATUS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleBattlefieldStatusOpcode);
    DefC(CMSG_REQUEST_RATED_BG_STATS, "CMSG_REQUEST_RATED_BG_STATS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestRatedBGStatsOpcode);
    DefS(SMSG_BATTLEFIELD_RATED_INFO, "SMSG_BATTLEFIELD_RATED_INFO");
    DefS(SMSG_BATTLEFIELD_STATUS, "SMSG_BATTLEFIELD_STATUS");
    DefS(SMSG_BATTLEFIELD_STATUS_QUEUED, "SMSG_BATTLEFIELD_STATUS_QUEUED");
    DefS(SMSG_BATTLEFIELD_STATUS_NEEDCONFIRMATION, "SMSG_BATTLEFIELD_STATUS_NEEDCONFIRMATION");
    DefS(SMSG_BATTLEFIELD_STATUS_ACTIVE, "SMSG_BATTLEFIELD_STATUS_ACTIVE");
    DefS(SMSG_BATTLEFIELD_STATUS_FAILED, "SMSG_BATTLEFIELD_STATUS_FAILED");

    // Live-log conquest formula request and its directly paired response.
    // Wow.exe proves the empty request and five-field response reader.
    DefC(CMSG_REQUEST_CONQUEST_FORMULA_CONSTANTS, "CMSG_REQUEST_CONQUEST_FORMULA_CONSTANTS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestConquestFormulaConstantsOpcode);
    DefS(SMSG_CONQUEST_FORMULA_CONSTANTS, "SMSG_CONQUEST_FORMULA_CONSTANTS");

    // Wave 19 calendar update bodies. Names come through the 5.4.7 bridge;
    // values and layouts are proved by the 18414 receive routes.
    DefS(SMSG_CALENDAR_EVENT_INITIAL_INVITE, "SMSG_CALENDAR_EVENT_INITIAL_INVITE");
    DefS(SMSG_CALENDAR_EVENT_INVITE_STATUS, "SMSG_CALENDAR_EVENT_INVITE_STATUS");
    DefS(SMSG_CALENDAR_EVENT_MODERATOR_STATUS, "SMSG_CALENDAR_EVENT_MODERATOR_STATUS");

    // Shipped OpenCalendar/CalendarOpenEvent APIs reach these empty/uint64
    // requests; the paired response values and readers are proved in 18414.
    DefC(CMSG_CALENDAR_GET_CALENDAR, "CMSG_CALENDAR_GET_CALENDAR", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleCalendarGetCalendar);
    DefC(CMSG_CALENDAR_GET_EVENT, "CMSG_CALENDAR_GET_EVENT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleCalendarGetEvent);
    DefS(SMSG_CALENDAR_SEND_CALENDAR, "SMSG_CALENDAR_SEND_CALENDAR");
    DefS(SMSG_CALENDAR_SEND_EVENT, "SMSG_CALENDAR_SEND_EVENT");

    // Live-log calendar pending-count pair. The 18414 client sends an empty
    // request and consumes exactly one uint32 from the response.
    DefC(CMSG_CALENDAR_GET_NUM_PENDING, "CMSG_CALENDAR_GET_NUM_PENDING", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleCalendarGetNumPending);
    DefS(SMSG_CALENDAR_SEND_NUM_PENDING, "SMSG_CALENDAR_SEND_NUM_PENDING");

    // Empty-bodied client actions. Every one is observed in the 18414 corpus with a
    // zero-length body in every single observation, and every handler reads nothing
    // at all -- no `>>`, no bit reads -- so there is no reader to get wrong and no
    // inherited body shape to inherit. That is the reason they are grouped: the
    // failure that ran through the guild query family cannot apply to a body that
    // does not exist.
    //
    // What is NOT claimed here is that these handlers are silent. Registering a
    // request only puts the client's existing trigger back in reach; the side
    // effects downstream are ordinary game lifecycle traffic and were already
    // reachable by other means:
    //
    //   REQUEST_PET_INFO       sends nothing.
    //   COMPLETE_CINEMATIC,    can produce visibility and update-object traffic.
    //   NEXT_CINEMATIC_CAMERA
    //   REQUEST_VEHICLE_EXIT   produces aura and spline traffic on unboarding.
    //   LEAVE_BATTLEFIELD      produces battleground status and teleport traffic.
    //                          SMSG_BATTLEGROUND_PLAYER_LEFT is reachable and is
    //                          NOT currently admitted by the in-world send gate, so
    //                          that particular notification is still dropped. It is
    //                          a pre-existing gap, not one opened here.
    //   GUILD_ACCEPT           reaches SMSG_GUILD_EVENT_PLAYER_JOINED, already
    //                          registered, already admitted and already live via the
    //                          invite path, so it adds no new outbound surface.
    //
    DefC(CMSG_REQUEST_PET_INFO, "CMSG_REQUEST_PET_INFO", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestPetInfoOpcode);
    DefC(CMSG_LEAVE_BATTLEFIELD, "CMSG_LEAVE_BATTLEFIELD", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLeaveBattlefieldOpcode);
    DefC(CMSG_COMPLETE_CINEMATIC, "CMSG_COMPLETE_CINEMATIC", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleCompleteCinematic);
    DefC(CMSG_NEXT_CINEMATIC_CAMERA, "CMSG_NEXT_CINEMATIC_CAMERA", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleNextCinematicCamera);
    DefC(CMSG_REQUEST_VEHICLE_EXIT, "CMSG_REQUEST_VEHICLE_EXIT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleRequestVehicleExit);
    DefC(CMSG_GUILD_ACCEPT, "CMSG_GUILD_ACCEPT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGuildAcceptOpcode);

    // Three multi-byte scalar setters. A packed GUID hides its mask and byte
    // order from size evidence -- every permutation of one GUID has the same
    // length -- which is why the guild queries needed their reader taken from
    // the client binary. None of these three carries a GUID, and each retail
    // body is a single fixed width that the handler consumes exactly:
    //
    //   SET_TITLE             int32              4 bytes     18 observed, min 4 max 4
    //   SET_WATCHED_FACTION   int32              4 bytes      3 observed, min 4 max 4
    //   SET_CURRENCY_FLAGS    uint32 + uint32    8 bytes      4 observed, min 8 max 8
    //
    // Width alone would not be enough -- see the far-sight note below -- so each
    // is corroborated by decoding real bodies. Titles decode to indices 202/107/99
    // and watched factions to 118/106/99, all plausible little-endian indices. The
    // currency bodies are decisive about field order, which size cannot be:
    //
    //   04 00 00 00 88 01 00 00     flags 4, currency 392
    //   04 00 00 00 8B 01 00 00     flags 4, currency 395
    //   04 00 00 00 8C 01 00 00     flags 4, currency 396
    //
    // 4 is exactly PLAYERCURRENCY_FLAG_SHOW_IN_BACKPACK, so the handler's
    // `>> flags >> currencyId` is packet-proven despite the reversed declaration.
    //
    // None of the three invokes an opcode-specific response serializer, which is
    // the hazard that held the guild queries back. They mark ordinary player
    // fields or internal currency state, and the resulting SMSG_UPDATE_OBJECT
    // traffic is already registered and already driven continuously by movement.
    //
    DefC(CMSG_SET_TITLE, "CMSG_SET_TITLE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetTitleOpcode);
    DefC(CMSG_SET_WATCHED_FACTION, "CMSG_SET_WATCHED_FACTION", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetWatchedFactionOpcode);
    DefC(CMSG_SET_CURRENCY_FLAGS, "CMSG_SET_CURRENCY_FLAGS", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSetCurrencyFlagsOpcode);

    // Far sight, once its reader was corrected. This was held back from the batch
    // above because its one-byte body is an MSB-first bit rather than a uint8
    // boolean: every sampled 18414 body is 0x80 or 0x00, and the inherited
    // `switch (op) case 0/case 1` matched neither, so enabling far sight silently
    // did nothing. HandleFarSightOpcode now accepts only the exact canonical
    // one-bit byte and resolves the far-sight object only on enable, so a reset
    // no longer depends on that object still being in scope. Fixtures pin both
    // legal bodies and reject non-zero padding or byte tails.
    DefC(CMSG_FAR_SIGHT, "CMSG_FAR_SIGHT", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleFarSightOpcode);

    // Showing helm, once its reader was corrected, for the same bit-versus-byte
    // reason as far sight: its three sampled bodies are 0x80, 0x00, 0x80.
    // HandleShowingHelmOpcode previously ignored the packet entirely and toggled
    // PLAYER_FLAGS_HIDE_HELM, so the helm inverted for the rest of the session the
    // first time client and server disagreed. It now assigns the bit the client
    // sent. Fixtures pin both bodies in mop_showing_helm_packets.
    //
    // CMSG_SHOWING_CLOAK (0x02F2) carried the identical defect and its handler
    // has been repaired the same way, from the same kind of evidence rather than
    // by symmetry: sub_4095E0 reads PLAYER_FLAGS & 0x800 and hands it to the same
    // one-bit serializer as the helm.
    //
    // It was previously left dormant because its ENCODING was proven but its
    // OCCURRENCE was not -- no observation showed the client sending it. That is
    // no longer sufficient grounds: an unobserved opcode is a gap in what was
    // recorded, not evidence the client stays silent. The binary shows the route
    // exists, the handler touches one PLAYER_FLAGS bit and nothing else, and a
    // dropped request leaves the player unable to toggle their cloak at all.
    DefC(CMSG_SHOWING_HELM, "CMSG_SHOWING_HELM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleShowingHelmOpcode);
    DefC(CMSG_SHOWING_CLOAK, "CMSG_SHOWING_CLOAK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleShowingCloakOpcode);

    // CMSG_LFG_JOIN's inherited reader was the 3.3.5 shape and shared no field
    // with 18414; fed a real body it decoded a zero dungeon count and left
    // sixteen bytes unread. The 18414 layout is in MopCompactPackets::ReadLfgJoin
    // and its 22-bit dungeon count is bounded against the body's own remaining
    // length before anything is allocated from it.
    DefC(CMSG_LFG_JOIN, "CMSG_LFG_JOIN", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfgJoinOpcode);

    // Leaving the queue. Registered WITH the join being wired, because a
    // player who can queue and cannot cancel is worse off than one who
    // cannot queue at all. Body is a ticket echo verified byte-exact against
    // four captured bodies, including the 17-byte zero-mask form.
    DefC(CMSG_LFG_LEAVE, "CMSG_LFG_LEAVE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleLfgLeaveOpcode);

    // The mailbox family and the guild-bank tab query. Each inherited reader took
    // a raw ObjectGuid first; at 18414 all four pack it and the scalars lead. The
    // readers and their fixtures are in place for all four.
    //
    // MARK_AS_READ and TAKE_ITEM are registered. MARK_AS_READ owes no command
    // result at all; TAKE_ITEM was promoted together with SMSG_SEND_MAIL_RESULT
    // once that reply was rebuilt to its 18414 body and admitted.
    //
    // GET_MAIL_LIST is promoted with its rebuilt 18414 two-pass reply.
    // The 18414 CMSG_GUILD_BANKER_ACTIVATE request body is resolved, but the
    // wave remains held solely because the first uint32 in each present-item
    // SMSG_GUILD_BANK_LIST record maps to the client's +48 dynamic-flags field,
    // whose server-side meaning and state are not yet modelled. Activate/query
    // registration, reply registration/admission, and all three reference rows
    // therefore remain dormant as one source-enforced wave.
    DefC(CMSG_GET_MAIL_LIST, "CMSG_GET_MAIL_LIST", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleGetMailList);
    DefS(SMSG_MAIL_LIST_RESULT, "SMSG_MAIL_LIST_RESULT");
    DefC(CMSG_MAIL_MARK_AS_READ, "CMSG_MAIL_MARK_AS_READ", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMailMarkAsRead);

    // CMSG_MAIL_TAKE_ITEM is promoted WITH its reply, which is the whole point of
    // the pairing rule: the request commits an irreversible item and cash-on-
    // delivery transaction, so it may only go live once the client can be told
    // the outcome. SMSG_SEND_MAIL_RESULT is now the real 18414 body and admitted.
    DefC(CMSG_MAIL_TAKE_ITEM, "CMSG_MAIL_TAKE_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMailTakeItem);
    DefC(CMSG_MAIL_TAKE_MONEY, "CMSG_MAIL_TAKE_MONEY", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMailTakeMoney);
    // The remaining mail UI actions use their client-writer-proven 18414
    // bodies. SEND and RETURN publish no in-memory asset change or success
    // before one direct database commit; DELETE revalidates the opened mailbox
    // and refuses mail with value; CREATE_TEXT_ITEM persists its replay flag
    // with the new inventory item.
    DefC(CMSG_SEND_MAIL, "CMSG_SEND_MAIL", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleSendMail);
    DefC(CMSG_MAIL_DELETE, "CMSG_MAIL_DELETE", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMailDelete);
    DefC(CMSG_MAIL_RETURN_TO_SENDER, "CMSG_MAIL_RETURN_TO_SENDER", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMailReturnToSender);
    DefC(CMSG_MAIL_CREATE_TEXT_ITEM, "CMSG_MAIL_CREATE_TEXT_ITEM", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMailCreateTextItem);
    DefS(SMSG_SEND_MAIL_RESULT, "SMSG_SEND_MAIL_RESULT");

    // Both send no reply, so neither carries the unpaired-response hazard that
    // holds the rest of the mail family back. Each is a plain byte, a mask byte
    // and a packed value; the inherited readers took raw scalars.
    //
    // CMSG_SET_ACTION_BUTTON is additionally proven against the client's own
    // writer sub_669CAE, and its action field is a full 32 bits where the
    // inherited macro cut it at 24.
    DefC(CMSG_TOTEM_DESTROYED, "CMSG_TOTEM_DESTROYED", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleTotemDestroyed);

    // The run-back speed acknowledgement. Of the nine members of this family,
    // four put the speed ahead of the movement block and the handler reads it as
    // a leading float. This is the one of those four with real traffic; swim is
    // already registered, and the other two are registered just below.
    //
    // The remaining five carry the speed INSIDE the movement sequence. That was
    // once inexpressible here and the reason they stayed unregistered; MSESpeedFloat
    // now expresses it, and three of the five are registered further down. The
    // handler branches on which group an opcode belongs to.
    DefC(CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK, "CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleForceSpeedChangeAckOpcodes);
    // The other two speed-leading members. Neither has observed traffic, so
    // neither has a capture fixture, but their sequences come from the client's
    // own writers and absence from the corpus is a gap in what was recorded.
    DefC(CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK, "CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleForceSpeedChangeAckOpcodes);
    DefC(CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK, "CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleForceSpeedChangeAckOpcodes);
    // The three whose speed sits INSIDE the movement block rather than ahead of
    // it. They needed a new MSESpeedFloat element to be expressible at all, and
    // until they were registered the speed anti-cheat never ran for walk, run or
    // flight -- run being the one a speed hack actually abuses. The remaining two,
    // turn rate and pitch rate, have no observed bodies and are not registered.
    DefC(CMSG_FORCE_WALK_SPEED_CHANGE_ACK, "CMSG_FORCE_WALK_SPEED_CHANGE_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleForceSpeedChangeAckOpcodes);
    DefC(CMSG_FORCE_RUN_SPEED_CHANGE_ACK, "CMSG_FORCE_RUN_SPEED_CHANGE_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleForceSpeedChangeAckOpcodes);
    DefC(CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK, "CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleForceSpeedChangeAckOpcodes);

    // The flight acknowledgement, now that SMSG_MOVE_SET_CAN_FLY is admitted and
    // the client is therefore sending this.
    //
    // It stayed dormant for a specific reason, not inertia: MovementSetCanFlyAckSequence
    // was wrong in eleven ways and read 18 bits where the client writes 42. The
    // sequence was already routed, so a DefC line on its own would have armed a
    // reader that parsed every body three bytes out of phase -- worse than
    // dropping the packet, because a garbage GUID that happens to match applies a
    // garbage flag word to the mover. The sequence is rebuilt from the client's
    // writer sub_674EA6 and pinned to four real bodies before this line was added.
    //
    // THREADUNSAFE because the handler mutates Unit::m_movementInfo, matching the
    // speed acknowledgements above.
    //
    // What it buys: Player::SetCanFly does not set MOVEFLAG_CAN_FLY server-side,
    // so without this the server's CanFly() stays false until the client's next
    // ordinary movement packet happens to carry the flag. Anything consulting it
    // in that window -- spline building, spell checks -- reads a stale value.
    DefC(CMSG_MOVE_SET_CAN_FLY_ACK, "CMSG_MOVE_SET_CAN_FLY_ACK", STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleMoveSetCanFlyAckOpcode);

    // CMSG_SET_ACTION_BUTTON is HELD. Its body is proven -- the reader matches
    // the client's writer sub_669CAE element for element and its fixtures stand
    // -- but the handler's type allowlist is narrower than the client's. The
    // client tests at least six high-nibble families and two of them, 0x10 and
    // 0x50, have no entry in ActionButtonType, so a button of either kind would
    // reach the default branch and be silently dropped. Losing a legitimate
    // button is a misparse by another name. Promote once those two are
    // identified and accepted.

    // CMSG_CONTACT_LIST (0x0BB4, 4,122 observed) is deliberately NOT registered,
    // and this note exists because it looks safe and is not.
    //
    // Its inbound side is one uint32 whose width matches every observed
    // four-byte body. The reply is what blocks it: registering the CMSG makes
    // HandleContactListOpcode call PlayerSocial::SendSocialList for a player who
    // IS in world, which builds SMSG_CONTACT_LIST (0x1F22) from a serializer
    // that is missing two fields.
    //
    // Two gates therefore stand between here and a visible defect, and both
    // matter. SMSG_CONTACT_LIST is also absent from IsEnterWorldConverted, so
    // even once built the reply is dropped in-world. Neither gate on its own is
    // the reason to stay dormant: the reason is that the body is wrong.
    //
    // Note the login path does NOT build this packet. Player::
    // SendInitialPacketsBeforeAddToMap calls SendSocialList before Map::Add, and
    // SendSocialList resolves its player through ObjectAccessor::FindPlayer with
    // inWorld=true, so at that point it returns early and writes nothing.
    //
    // The trap for anyone checking it is that the empty case agrees byte for
    // byte. Retail's empty list is 07 00 00 00 00 00 00 00, exactly the
    // uint32(7) + uint32(0) header SendSocialList writes, so a check that stops
    // at "sizes and header match" passes. A populated list does not agree:
    //
    //   07 00 00 00  02 00 00 00                          flags 7, count 2
    //   68 D1 19 07 00 00 00 06  19 00 01 03 16 00 06 03  04 00 00 00 00
    //   E5 FE 23 07 00 00 00 06  19 00 01 03 16 00 06 03  02 00 00 00 00
    //
    // The client's reader is sub_A6AAB5 (0x00A6AAB5, asserts "FriendList.cpp"),
    // and it is entirely byte-aligned -- this packet carries no bit-packing:
    //
    //   uint32  listFlags                  1 friends, 2 ignore, 4 mute present
    //   uint32  count
    //   count * {
    //       uint64  guid                   raw LE, not packed, not XOR'd
    //       uint32  realmAddrA             0x03010019 in both entries above
    //       uint32  realmAddrB             0x03060016 in both
    //       uint32  typeFlags              1 friend, 2 ignored, 4 muted
    //       cstring note                   NUL-terminated, <= 512
    //       if (typeFlags & 1) {
    //           uint8 status               0 = offline
    //           if (status) { uint32 areaId; uint32 level; uint32 classId; }
    //       }
    //   }
    //
    // So 21 bytes is not a property of the packet -- it is one entry with the
    // friend bit clear and an empty note. A friend entry is 22 offline and 34
    // online. Corpus entries of 8, 30, 42, 50 and 100 bytes all consume exactly.
    //
    // Our delta is precisely two uint32s: SendSocialList writes no counterpart
    // for either realm address. Reading the retail entry above under the field
    // sequence SendSocialList writes therefore loses alignment immediately after
    // the GUID -- 0x03010019 lands where the type flags are expected, 0x16
    // begins the note, and a level of 4,276,420,608 comes out -- running on into
    // the following entry. Everything else it writes, the raw GUID, the uint32
    // type flags, the NUL-terminated note, the status byte and the online-only
    // area/level/class triple in that order, is 18414-correct.
    //
    // It returns once SendSocialList carries the two realm addresses, populated
    // cases have byte-exact fixtures, and SMSG_CONTACT_LIST is admitted to
    // IsEnterWorldConverted.
}
