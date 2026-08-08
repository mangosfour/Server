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
 * @file WorldSession.cpp
 * @brief World session implementation
 *
 * This file implements WorldSession which manages a player's connection
 * to the world server. It handles:
 *
 * - Packet processing and opcode dispatch
 * - Player authentication and login
 * - Character management
 * - Movement and action handling
 * - Chat and social interactions
 * - Warden anti-cheat integration
 *
 * The session filters packets based on thread safety and context:
 * - Map::Update() context: Only process thread-safe packets
 * - World::UpdateSessions() context: Process all packets
 *
 * @see WorldSession for the session class
 * @see proto::ClientConnection for the network connection
 * @see Opcodes.cpp for opcode registration
 */

#include "IClientLink.h"
#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Util.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "Group.h"
#include "CinematicFlyover.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "World.h"
#include "ObjectAccessor.h"
#include "BattleGround/BattleGroundMgr.h"
#include "MapManager.h"
#include "MopLogoutPackets.h"
#include "MopNotificationPackets.h"
#include "SocialMgr.h"
#include "Auth/AuthCrypt.h"
#include "Auth/HMACSHA1.h"
#include "Auth/MopAuthKey.h"
#include "MopAuthResponse.h"
#include "zlib.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /*ENABLE_ELUNA*/
#ifdef ENABLE_PLAYERBOTS
//#include "playerbot.h"
#endif

// Warden
#include "WardenWin.h"
#include "WardenMac.h"
#include <cstring>
#include <mutex>
#include <utility>

// inet_addr() for SendRedirectClient(). Used to arrive transitively through
// the ACE includes in the old Common.h; with that header gone it has to be
// named here (matches the pattern in realmd/Realm/RealmList.cpp).
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#endif

/**
 * @brief Helper for Map session filtering
 * @param session World session
 * @param opHandle Opcode handler
 * @return True if packet can be processed in Map::Update
 *
 * Determines if an opcode can be safely processed in the Map::Update
 * thread context based on thread safety requirements.
 */
static bool MapSessionFilterHelper(WorldSession* session, OpcodeHandler const& opHandle)
{
    // we do not process thread-unsafe packets
    if (opHandle.packetProcessing == PROCESS_THREADUNSAFE)
    {
        return false;
    }

    // we do not process not loggined player packets
    Player* plr = session->GetPlayer();
    if (!plr)
    {
        return false;
    }

    // in Map::Update() we do not process packets where player is not in world!
    return plr->IsInWorld();
}


/**
 * @brief Process packet in Map context
 * @param packet Packet to process
 * @return True if packet should be processed
 *
 * Filters packets for processing in Map::Update context.
 * Only processes thread-safe packets when player is in world.
 */
bool MapSessionFilter::Process(WorldPacket* packet)
{
    OpcodeHandler const* opHandle = LookupClientOpcode(packet->GetOpcode());
    if (!opHandle || opHandle->packetProcessing == PROCESS_INPLACE)
    {
        return true;
    }

    // let's check if our opcode can be really processed in Map::Update()
    return MapSessionFilterHelper(m_pSession, *opHandle);
}

/**
 * @brief Process packet in World context
 * @param packet Packet to process
 * @return True if packet should be processed
 *
 * Filters packets for processing in World::UpdateSessions context.
 * Processes all packets when player is not in world or when
 * packet handler is not thread-safe.
 */
bool WorldSessionFilter::Process(WorldPacket* packet)
{
    OpcodeHandler const* opHandle = LookupClientOpcode(packet->GetOpcode());
    // check if packet handler is supposed to be safe
    if (!opHandle || opHandle->packetProcessing == PROCESS_INPLACE)
    {
        return true;
    }

    // let's check if our opcode can't be processed in Map::Update()
    return !MapSessionFilterHelper(m_pSession, *opHandle);
}

/// WorldSession constructor
WorldSession::WorldSession(uint32 id, std::shared_ptr<proto::IClientLink> link,
                           AccountTypes sec, uint8 expansion, time_t mute_time, LocaleConstant locale,
                           const uint8 (&sessionKey)[MopAuth::SESSION_KEY_LEN]) :
    m_muteTime(mute_time), _player(NULL), m_Socket(std::move(link)), _security(sec), _accountId(id), m_expansion(expansion), _logoutTime(0),
    m_pendingTransferRootCounter(0), m_suspendTokenCounter(0), m_pendingSuspendToken(0), m_waitingForTransferRootAck(false), m_waitingForSuspendToken(false),
    m_inQueue(false), m_playerLoading(false), m_suppressWorldSends(false),
    m_playerLogout(false), m_playerRecentlyLogout(false), m_playerSave(false),
    m_suppressCharacterSave(false),
    m_sessionDbcLocale(sWorld.GetAvailableDbcLocale(locale)), m_sessionDbLocaleIndex(sObjectMgr.GetIndexForLocale(locale)),
    m_latency(0), m_clientTimeDelay(0), m_tutorialState(TUTORIALDATA_UNCHANGED)
{
    std::memcpy(m_sessionKey, sessionKey, sizeof(m_sessionKey));

    if (m_Socket)
    {
        m_Address = m_Socket->GetRemoteAddress();
    }
}

/// WorldSession destructor
WorldSession::~WorldSession()
{
    ///- unload player if not unloaded
    if (_player)
    {
        LogoutPlayer(true);
    }

    /// - If the connection is still up, close it. Dropping the shared_ptr is all the
    /// bookkeeping there is now: the transport (proto::ClientConnection / the net:: engine)
    /// owns the actual socket, and the link stays safe to call even after the peer is gone.
    if (m_Socket)
    {
        m_Socket->Close();
        m_Socket.reset();
    }

    // CAUSES CRASH ON PLAYER EXITING TO LOGIN SCREEN
    // Warden
//    if (_warden)
//        delete _warden;

    ///- empty incoming packet queue
    WorldPacket* packet = NULL;
    while (_recvQueue.next(packet))
    {
        delete packet;
    }
}

/// Drop the session's reference to its link without closing it.
///
/// Valid only before the session has been published to World: the connection must stay alive
/// to drain an auth-error response through it (WorldGateway::Attach() calls this, then deletes
/// the session, on any post-allocation failure -- the rejection is sent by
/// proto::ClientConnection AFTER Attach() returns INVALID_SESSION_ID). Resetting this session's
/// own shared_ptr is all that is needed: the caller's own copy of the link keeps the connection
/// alive, and ~WorldSession() skips its Close() call because m_Socket is already empty.
void WorldSession::AbandonUnpublishedLink() noexcept
{
    m_Socket.reset();
}

/**
 * @brief Logs an invalid client packet size for the current opcode.
 *
 * @param packet The offending packet.
 * @param size The expected packet size.
 */
void WorldSession::SizeError(WorldPacket const& packet, uint32 size) const
{
    sLog.outError("Client (account %u) send packet %s (%u) with size %zu but expected %u (attempt crash server?), skipped",
                  GetAccountId(), LookupOpcodeName(DIR_CLIENT, packet.GetOpcode()), packet.GetOpcode(), packet.size(), size);
}

/// Get the player name
char const* WorldSession::GetPlayerName() const
{
    return GetPlayer() ? GetPlayer()->GetName() : "<none>";
}

/// Send a packet to the client
// PHASE 6c enter-world envelope whitelist. m_suppressWorldSends drops the entire
// SendInitialPacketsAfterAddToMap flow while its packets are still Cata-format. As each of those
// send-functions is converted to a genuine 18414 wire body (remote Codex Wave 5+), add its opcode
// here so the CONVERTED normal-flow send passes suppression -- letting us bring the in-world
// UI/input envelope online one batch at a time and test incrementally.
// HARD RULE: never whitelist an opcode until its send emits a real 18414 body; a stale Cata body
// reaching the 18414 client can crash it. This whole gate (and m_suppressWorldSends) is removed
// once the after-map flow is fully at parity.
static bool IsEnterWorldConverted(uint16 opcode)
{
    if (MopCompactPackets::IsInstanceResetResult(opcode))
    {
        return true;
    }

    switch (opcode)
    {
        // Wave 5 (9ba498698): these after-map send-functions now emit genuine 18414 bodies
        // (MopInitialPackets), so they pass suppression and populate the client's in-world
        // UI/input state (spells, action bar, factions, account data, tutorials, weather, etc.).
        case SMSG_INITIAL_SPELLS:
        case SMSG_CATEGORY_COOLDOWN:              // 21-bit JamCategoryCooldown records; client reader sub_C7C233
        case SMSG_SEND_UNLEARN_SPELLS:
        case SMSG_ACTION_BUTTONS:
        case SMSG_INITIALIZE_FACTIONS:
        case SMSG_ACCOUNT_DATA_TIMES:
        case SMSG_UPDATE_ACCOUNT_DATA:  // 0x0AAE -- direct reader sub_6F1A32 proves type/GUID/size/blob/time body
        case SMSG_FEATURE_SYSTEM_STATUS:
        case SMSG_TUTORIAL_FLAGS:
        case SMSG_BINDPOINTUPDATE:
        case SMSG_SET_PROFICIENCY:
        case SMSG_WEATHER:
        case SMSG_ALL_ACHIEVEMENT_DATA:  // Wave 5 Task 2 -- converted 6908c5f9e (MopAchievementPackets)
        case SMSG_CRITERIA_UPDATE:       // Isolated timed-expiry tombstone (MopAchievementPackets)
        case SMSG_WHO:                   // 0x161B -- /who results (MopWhoPackets::BuildWhoResponse, converted).
                                         // Body derived from the client's reader sub_720854 and its
                                         // 536-byte JamWhoEntry; the empty form is the single 0x00 byte
                                         // seen at capture-000059 seq 1637608.
            return true;

        // Control/transition packets that must ALWAYS reach the client while suppression is active:
        // world-leave (logout) and in-world teleport. These are tiny, header-stable control bodies
        // (verified against the 18414 client, e.g. SMSG_LOGOUT_RESPONSE now writes the instant flag
        // as a bit) -- not the stale bulk-object envelope suppression exists to drop. Without these
        // the player can neither log out nor teleport once in-world.
        case SMSG_LOGOUT_RESPONSE:       // 0x008F -- ack the logout request (bit-packed body, 18414-correct)
        case SMSG_LOGOUT_CANCEL_ACK:     // 0x0AAF -- ack a cancelled logout (empty body)
        case SMSG_LOGOUT_COMPLETE:       // 0x142F -- final world-leave (leading bit + zero GUID mask: 80 00)
        case SMSG_MOVE_TELEPORT:         // 0x0B39 -- same-map teleport (MopWorldEntryPackets::BuildMoveTeleport, converted)
        case SMSG_NEW_WORLD:             // 0x1C3B -- cross-map teleport target (MopWorldEntryPackets::BuildNewWorld, converted)
        case SMSG_SUSPEND_TOKEN:         // 0x18BA -- gate NEW_WORLD until the client has torn down the old world
        case SMSG_TRANSFER_PENDING:      // 0x061B -- cross-map load-screen preamble (inline bit-packed body; non-transport
                                         //           path byte-identical to the 18414 reference; transport-case field
                                         //           order unverified -- follow-up when far-teleport-with-transport lands)
        case SMSG_TRANSFER_ABORTED:      // 0x0C8F -- absent-argument bit, 5-bit reason, optional byte, map ID
            return true;

        // Converted in-world control, movement, compact UI, and combat-log bodies. Each
        // row is backed by its owning 18414 serializer and focused byte fixtures. Keeping
        // these outside the gate silently discarded otherwise-valid gameplay traffic.
        case SMSG_LOGIN_SETTIMESPEED:              // MopWorldEntryPackets::BuildLoginSetTimeSpeed
        case SMSG_TIME_SYNC_REQ:                   // one uint32 counter; client reader sub_6D9F28
        case SMSG_TRIGGER_CINEMATIC:               // one uint32 sequence; dynamic handler sub_7AD161
        case SMSG_WORLD_SERVER_INFO:               // four presence bits + minimal fixed fields; sub_6F470B
        case SMSG_MOTD:                            // 4-bit count, 7-bit lengths, raw strings; sub_75B75A
        case SMSG_CORPSE_RECLAIM_DELAY:            // no-delay bit plus optional uint32 milliseconds; sub_6D7781
        case SMSG_RESURRECT_REQUEST:               // MopDeathPackets::BuildResurrectRequest
        case SMSG_SPIRIT_HEALER_CONFIRM:           // MopDeathPackets::BuildSpiritHealerConfirm
        case SMSG_SET_FORCED_REACTIONS:            // 2-bit count plus uint32 pairs; sub_72C708
        case SMSG_SET_FACTION_STANDING:            // 1-bit visual flag, 21-bit count, standing/index pairs, two floats
        case SMSG_SET_FACTION_VISIBLE:             // one uint32 reputation-list index
        case SMSG_TITLE_EARNED:                    // uint32 title Mask_ID; opcode selects earned semantics
        case SMSG_TITLE_LOST:                      // uint32 title Mask_ID; opcode selects lost semantics
        case SMSG_PVP_CREDIT:                      // rank, honor, then packed victim GUID
        case SMSG_CROSSED_INEBRIATION_THRESHOLD:   // interleaved packed player GUID, item, state
        case SMSG_TAXINODE_STATUS:                 // MopTaxiPackets::BuildStatusBody
        case SMSG_SHOWTAXINODES:                   // MopTaxiPackets::BuildShowTaxiNodes
        case SMSG_NEW_TAXI_PATH:                   // empty taxi-node discovery notification
        case SMSG_ACTIVATETAXIREPLY:               // MopTaxiPackets::BuildActivateTaxiReply
        case SMSG_INIT_WORLD_STATES:                // map, area, zone, 21-bit count, uint32 pairs; sub_732740
        case SMSG_UPDATE_WORLD_STATE:              // hidden bit, value, field; sub_6E8FA4 -> sub_CC00A4
        case SMSG_ITEM_TIME_UPDATE:                  // packed item GUID then uint32 duration; sub_6F06A8
        case SMSG_ITEM_ENCHANT_TIME_UPDATE:          // interleaved item/player GUIDs, slot, duration; sub_6C8203
        case SMSG_INSPECT_RESULTS:                    // MopInspectPackets::BuildResponse; Player_C.cpp leaf sub_7B9407
        case SMSG_UI_TIME:                           // one uint32 server time; sub_6D9F28 -> sub_40F340
        case SMSG_START_TIMER:                       // max time, remaining time, timer type; sub_6E7584
        case SMSG_CALENDAR_SEND_NUM_PENDING:         // one uint32 pending count; sub_6D9F28 -> sub_40F340
        case SMSG_DB_REPLY:                          // entry, hotfix date, table hash, byte-count, record; sub_708034 -> sub_6E5250
        case SMSG_CLIENT_CONTROL_UPDATE:           // MopControlPackets::BuildClientControlUpdate
        case SMSG_MOVE_SET_ACTIVE_MOVER:           // MopControlPackets::BuildSetActiveMover
        case SMSG_PLAYER_MOVE:                     // MovementInfo relay serializer
        case SMSG_MONSTER_MOVE:                    // PacketBuilder 18414 spline serializer (transport data is embedded)
        // Knockback is admitted atomically: the owner receives the direct body,
        // then its ack is relayed to nearby observers through MovementInfo.
        case SMSG_MOVE_KNOCK_BACK:                 // MopCompactPackets::BuildMoveKnockBack
        case SMSG_MOVE_UPDATE_KNOCK_BACK:          // MovementInfo relay serializer
        case SMSG_MOVE_SET_SWIM_SPEED:             // MopCompactPackets::BuildMoveSetSwimSpeed
        case SMSG_MOVE_SET_RUN_SPEED:              // MopCompactPackets::BuildMoveSetRunSpeed
        case SMSG_MOVE_SET_WALK_SPEED:             // MopCompactPackets::BuildMoveSetWalkSpeed
        case SMSG_SPLINE_MOVE_SET_RUN_SPEED:       // MopCompactPackets::BuildSplineMoveSetRunSpeed -- observers
        case SMSG_MOVE_SET_RUN_BACK_SPEED:         // MopCompactPackets::BuildMoveSetRunBackSpeed
        case SMSG_MOVE_SET_FLIGHT_SPEED:           // MopCompactPackets::BuildMoveSetFlightSpeed
        case SMSG_SPLINE_MOVE_SET_WALK_SPEED:      // MopCompactPackets::BuildSplineMoveSetWalkSpeed
        case SMSG_SPLINE_MOVE_SET_RUN_BACK_SPEED:  // MopCompactPackets::BuildSplineMoveSetRunBackSpeed
        case SMSG_SPLINE_MOVE_SET_SWIM_SPEED:      // MopCompactPackets::BuildSplineMoveSetSwimSpeed
        case SMSG_SPLINE_MOVE_SET_FLIGHT_SPEED:    // MopCompactPackets::BuildSplineMoveSetFlightSpeed
        case SMSG_SPLINE_MOVE_SET_SWIM_BACK_SPEED: // MopCompactPackets::BuildSplineMoveSetSwimBackSpeed
        case SMSG_SPLINE_MOVE_SET_TURN_RATE:       // MopCompactPackets::BuildSplineMoveSetTurnRate
        case SMSG_SPLINE_MOVE_SET_FLIGHT_BACK_SPEED: // MopCompactPackets::BuildSplineMoveSetFlightBackSpeed
        case SMSG_SPLINE_MOVE_SET_PITCH_RATE:      // MopCompactPackets::BuildSplineMoveSetPitchRate
        case SMSG_MOVE_SET_SWIM_BACK_SPEED:        // MopCompactPackets::BuildMoveSetSwimBackSpeed
        case SMSG_MOVE_SET_TURN_RATE:              // MopCompactPackets::BuildMoveSetTurnRate
        case SMSG_MOVE_SET_FLIGHT_BACK_SPEED:      // MopCompactPackets::BuildMoveSetFlightBackSpeed
        case SMSG_MOVE_SET_PITCH_RATE:             // MopCompactPackets::BuildMoveSetPitchRate
        case SMSG_PET_NAME_QUERY_RESPONSE:         // MopCompactPackets::BuildPetNameQueryResponse
        // The can-fly family, admitted as a complete set. The mover pair alone
        // would tell the flying player and leave observers seeing them walk; the
        // observer pair alone is the inversion four other movement states are
        // still in. All four or none.
        case SMSG_MOVE_SET_CAN_FLY:                // MopCompactPackets::BuildMoveSetCanFly
        case SMSG_MOVE_UNSET_CAN_FLY:              // MopCompactPackets::BuildMoveUnsetCanFly
        case SMSG_SPLINE_MOVE_SET_FLYING:          // MopCompactPackets::BuildSplineMoveSetFlying
        case SMSG_SPLINE_MOVE_UNSET_FLYING:        // MopCompactPackets::BuildSplineMoveUnsetFlying
        case SMSG_SEND_MAIL_RESULT:                // MopCompactPackets::BuildSendMailResult
        case SMSG_MAIL_LIST_RESULT:                // MopMailPackets::BuildList
        // Gravity. The MOVER pair 0x159F/0x0A27 is confirmed against a live 18414
        // client: the mover floats and holds altitude, stops sending
        // CMSG_MOVE_JUMP rather than having one rejected, and acknowledges each
        // packet with the matching ack.
        //
        // That test does NOT cover the observer pair 0x0845/0x0865. Those go out
        // through SendMessageToSet(..., false), which excludes the mover, and the
        // capture had no second client nearby -- so zero of them appear in it.
        // Their layouts are reader-derived and byte-exact against captured corpus
        // bodies, which is the same standard the other five families landed on,
        // but "confirmed live" applies to the mover pair alone. A two-client or
        // creature-levitate capture would close it.
        case SMSG_MOVE_GRAVITY_DISABLE:            // MopCompactPackets::BuildMoveGravityDisable
        case SMSG_MOVE_GRAVITY_ENABLE:             // MopCompactPackets::BuildMoveGravityEnable
        case SMSG_SPLINE_MOVE_GRAVITY_DISABLE:     // MopCompactPackets::BuildSplineMoveGravityDisable
        case SMSG_SPLINE_MOVE_GRAVITY_ENABLE:      // MopCompactPackets::BuildSplineMoveGravityEnable
        // Rooting, admitted as a complete set. Reaches far beyond any GM
        // command: death, resurrection and vehicle boarding all root.
        case SMSG_FORCE_MOVE_ROOT:                 // MopCompactPackets::BuildForceMoveRoot
        case SMSG_FORCE_MOVE_UNROOT:               // MopCompactPackets::BuildForceMoveUnroot
        case SMSG_SPLINE_MOVE_ROOT:                // MopCompactPackets::BuildSplineMoveRoot
        case SMSG_SPLINE_MOVE_UNROOT:              // MopCompactPackets::BuildSplineMoveUnroot
        // Hovering, admitted as a complete set. All four inherited bodies were
        // wrong -- they decode none of the 51 real bodies to a plausible GUID --
        // and none was admitted, so the family failed silently at both ends.
        case SMSG_MOVE_SET_HOVER:                  // MopCompactPackets::BuildMoveSetHover
        case SMSG_MOVE_UNSET_HOVER:                // MopCompactPackets::BuildMoveUnsetHover
        case SMSG_SPLINE_MOVE_SET_HOVER:           // MopCompactPackets::BuildSplineMoveSetHover
        case SMSG_SPLINE_MOVE_UNSET_HOVER:         // MopCompactPackets::BuildSplineMoveUnsetHover
        // Water walking, admitted as a complete set for the same reason as
        // can-fly. The observer halves were already here and correct; their
        // mover counterparts were built with the wrong mask order, byte order
        // and counter position, and then dropped, so .waterwalk changed nothing
        // for the player and nothing for anyone watching.
        case SMSG_MOVE_WATER_WALK:                 // MopCompactPackets::BuildMoveWaterWalk
        case SMSG_MOVE_LAND_WALK:                  // MopCompactPackets::BuildMoveLandWalk
        // Falling, admitted as a complete set. The observer halves were already
        // here; both mover halves were wrong in every field and dropped, so
        // feather fall changed nothing and normal fall never undid it.
        case SMSG_MOVE_FEATHER_FALL:               // MopCompactPackets::BuildMoveFeatherFall
        case SMSG_MOVE_NORMAL_FALL:                // MopCompactPackets::BuildMoveNormalFall
        case SMSG_SPLINE_MOVE_SET_NORMAL_FALL:     // MopMovementPackets::BuildSplineState
        case SMSG_SPLINE_MOVE_SET_RUN_MODE:        // MopCompactPackets::BuildSplineMoveSetRunMode -- observers
        case SMSG_SPLINE_MOVE_SET_WALK_MODE:       // MopCompactPackets::BuildSplineMoveSetWalkMode -- observers
        case SMSG_SPLINE_MOVE_SET_WATER_WALK:      // MopMovementPackets::BuildSplineState
        case SMSG_SPLINE_MOVE_SET_FEATHER_FALL:    // MopMovementPackets::BuildSplineState
        case SMSG_SPLINE_MOVE_SET_LAND_WALK:       // MopMovementPackets::BuildSplineState
        case SMSG_ATTACKSWING_ERROR:               // MopCompactPackets::BuildAttackSwingError
        case SMSG_RANDOM_ROLL:                     // MopCompactPackets::BuildRandomRoll
        case SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT:  // MopCompactPackets::BuildInstanceEncounter
        case SMSG_SET_RAID_DIFFICULTY:             // MopCompactPackets::BuildSetRaidDifficulty
        case SMSG_SET_DUNGEON_DIFFICULTY:          // MopCompactPackets::BuildSetDungeonDifficulty
        case SMSG_TRAINER_BUY_FAILED:               // MopTrainerBuyFailed::Build
        case SMSG_GM_TICKET_UPDATE:                 // MopGMTicketPackets::BuildUpdate
        case SMSG_GMTICKET_GETTICKET:               // MopGMTicketPackets::BuildGetTicket
        case SMSG_GMTICKET_SYSTEMSTATUS:            // MopGMTicketPackets::BuildSystemStatus
        case SMSG_GM_TICKET_CASE_STATUS:            // MopGMTicketPackets::BuildCaseStatus
        case SMSG_UPDATE_CURRENCY:                 // MopCurrencyPackets::BuildUpdateCurrency
        case SMSG_SETUP_CURRENCY:                  // MopCurrencyPackets::BuildSetupCurrency
        case SMSG_WEEKLY_RESET_CURRENCIES:         // Empty 18414 weekly-counter reset
        case SMSG_SET_CURRENCY_WEEK_LIMIT:         // uint32 week limit, then uint32 currency ID
        case SMSG_SPELL_EXECUTE_LOG:               // MopCombatLogPackets::BuildSpellExecuteLog
        case SMSG_SPELL_PERIODIC_AURA_LOG:         // MopCombatLogPackets::BuildPeriodicAuraLog
        case SMSG_SPELLDISPELLOG:                  // MopCombatLogPackets::BuildDispelLog
        case SMSG_SPELLINTERRUPTLOG:               // MopCombatLogPackets::BuildSpellInterruptLog
        case SMSG_SPELLINSTAKILLLOG:               // MopCombatLogPackets::BuildSpellInstakillLog
        case SMSG_SPELLENERGIZELOG:                // MopCombatLogPackets::BuildSpellEnergizeLog
        case SMSG_ENVIRONMENTALDAMAGELOG:          // MopCombatLogPackets::BuildEnvironmentalDamageLog
        case SMSG_SPELLNONMELEEDAMAGELOG:          // MopCombatLogPackets::BuildSpellNonMeleeDamageLog
        case SMSG_SPELLHEALLOG:                    // MopCombatLogPackets::BuildSpellHealLog
        case SMSG_SPELLDAMAGESHIELD:               // MopCombatLogPackets::BuildSpellDamageShieldLog
        case SMSG_SPELLLOGMISS:                    // MopCombatLogPackets::BuildSpellMissLog
        case SMSG_CAST_FAILED:                     // MopSpellPackets::BuildCastFailed
        case SMSG_PET_CAST_FAILED:                 // MopSpellPackets::BuildCastFailed (pet bit order)
        case SMSG_SPELL_START:                     // MopSpellPackets::BuildSpellStart
        case SMSG_SPELL_GO:                        // MopSpellPackets::BuildSpellGo
        case SMSG_SPELL_COOLDOWN:                  // MopSpellPackets::BuildSpellCooldown
        case SMSG_CLEAR_COOLDOWNS:                 // MopSpellPackets::BuildClearCooldowns
        case SMSG_COOLDOWN_EVENT:                  // MopSpellPackets::BuildCooldownEvent
        case SMSG_ITEM_COOLDOWN:                   // MopSpellPackets::BuildItemCooldown
        case SMSG_CLEAR_TARGET:                    // MopSpellPackets::BuildClearTarget
        case SMSG_LEARNED_SPELL:                   // MopSpellPackets::BuildLearnedSpell
        case SMSG_REMOVED_SPELL:                   // MopSpellPackets::BuildRemovedSpell
        case SMSG_SUPERCEDED_SPELL:                // MopSpellPackets::BuildSupersededSpell
        case SMSG_PET_LEARNED_SPELL:               // MopSpellPackets::BuildPetLearnedSpell
        case SMSG_PET_REMOVED_SPELL:               // MopSpellPackets::BuildPetRemovedSpell
        case SMSG_MESSAGECHAT:                     // MopChatPackets::BuildMessage
        case SMSG_CHAT_PLAYER_NOT_FOUND:            // MopChatPackets::BuildPlayerNotFound
        case SMSG_CHAT_PLAYER_AMBIGUOUS:            // MopChatPackets::BuildPlayerAmbiguous
        case SMSG_CHAT_RESTRICTED:                  // MopChatPackets::BuildChatRestrictedNotice
        case SMSG_CHANNEL_NOTIFY:                  // MopChannelPackets direct 18414 subtype serializers
        case SMSG_CHANNEL_LIST:                    // MopChannelPackets::BuildList
        case SMSG_TEXT_EMOTE:                      // MopChatPackets::BuildTextEmote
        case SMSG_EMOTE:                           // Unit::HandleEmoteCommand; uint32 plus uint64
        case SMSG_NOTIFICATION:                    // 12-bit byte length followed by raw notification text
        case SMSG_TRADE_STATUS:                    // MopTradePackets::BuildStatus
        case SMSG_TRADE_STATUS_EXTENDED:           // MopTradePackets::BuildUpdate
        case SMSG_STANDSTATE_UPDATE:               // one uint8 stand state; Unit_C.cpp leaf 0x810583
        case SMSG_ATTACKSTART:                     // MopCompactPackets::BuildAttackStart
        case SMSG_ATTACKSTOP:                      // MopCompactPackets::BuildAttackStop
        case SMSG_ATTACKERSTATEUPDATE:             // nested UnitCombat_C record; reader sub_858A94
        case SMSG_PARTYKILLLOG:                    // MopCompactPackets::BuildPartyKillLog
        case SMSG_DUEL_OUTOFBOUNDS:                // MopDuelPackets::BuildOutOfBounds
        case SMSG_DUEL_INBOUNDS:                   // MopDuelPackets::BuildInBounds
        case SMSG_DUEL_COMPLETE:                    // MopDuelPackets::BuildComplete
        case SMSG_DUEL_COUNTDOWN:                   // MopDuelPackets::BuildCountdown
        case SMSG_DUEL_REQUESTED:                   // MopDuelPackets::BuildRequested
        case SMSG_DUEL_WINNER:                      // MopDuelPackets::BuildWinner
        case SMSG_START_MIRROR_TIMER:                // MopMirrorTimerPackets::BuildStart
        case SMSG_STOP_MIRROR_TIMER:                 // MopMirrorTimerPackets::BuildStop
        case SMSG_CHANNEL_START:                     // MopSpellPackets::BuildChannelStart
        case SMSG_CHANNEL_UPDATE:                    // MopSpellPackets::BuildChannelUpdate
        case SMSG_RESYNC_RUNES:                      // MopRunePackets::BuildResync
        case SMSG_ADD_RUNE_POWER:                    // MopRunePackets::BuildAddPower
        case SMSG_CONVERT_RUNE:                      // MopRunePackets::BuildConvert
        case SMSG_THREAT_UPDATE:                     // MopThreatPackets::BuildUpdate
        case SMSG_HIGHEST_THREAT_UPDATE:             // MopThreatPackets::BuildHighest
        case SMSG_THREAT_CLEAR:                      // MopThreatPackets::BuildClear
        case SMSG_THREAT_REMOVE:                     // MopThreatPackets::BuildRemove
        case SMSG_DISMOUNT:                          // MopCompactPackets::BuildDismount
        case SMSG_PRE_RESURRECT:                     // MopCompactPackets::BuildPreResurrect
        case SMSG_UPDATE_COMBO_POINTS:               // MopComboPointPackets::BuildUpdate
        case SMSG_ACHIEVEMENT_EARNED:                // MopAchievementPackets::BuildAchievementEarned
        // Staging UI. SHOW_BANK and the calendar lockout removal use dedicated
        // 18414 packed-GUID builders; the other four already matched their
        // client readers and needed admission only. SHOW_MAILBOX has no corpus
        // body, and the GM ticket opcode name remains a single-fork hypothesis.
        case SMSG_SHOW_BANK:                         // MopCompactPackets::BuildShowBank
        case SMSG_SHOW_MAILBOX:                      // flat uint64 reader; binary-only evidence
        case SMSG_SERVER_MESSAGE:                    // uint32 type plus NUL-terminated text
        case SMSG_RECEIVED_MAIL:                     // one float delivery delay
        case SMSG_GM_TICKET_STATUS_UPDATE:           // one uint32; GM-ticket module route
        case SMSG_CALENDAR_RAID_LOCKOUT_REMOVED:     // MopCalendarPackets::BuildCalendarRaidLockoutRemoved
        case SMSG_CANCEL_COMBAT:                   // Empty reader; terminal clears local-player combat state
        case SMSG_CANCEL_AUTO_REPEAT:              // packed unit GUID; Unit_C leaf 0x819546 clears auto-repeat
        case SMSG_AI_REACTION:                     // packed unit GUID plus reaction; Unit_C.cpp leaf 0x80AD80
        case SMSG_POWER_UPDATE:                    // MopCompactPackets::BuildPowerUpdate; reader sub_72B5D8
        case SMSG_PLAY_SOUND:                      // packed source GUID plus sound ID; ClientPlaySound leaf 0xCC4275
        case SMSG_PLAY_OBJECT_SOUND:               // two packed GUIDs plus sound ID; object-sound leaf 0xCC42F8
        case SMSG_PLAY_MUSIC:                      // one uint32 ID; SI3 zone-sound leaf 0xCC053B
        case SMSG_PET_ACTION_SOUND:                // packed pet GUID plus action; Unit_C leaf 0x80ADB2
        case SMSG_PET_ACTION_FEEDBACK:             // inverse spell-ID presence bit plus feedback; leaf 0x93E7D0
        case SMSG_PET_MODE:                        // MopPetPackets::BuildMode; reader sub_6E0734 fires PET_BAR_UPDATE
        case SMSG_PET_SPELLS:                      // MopPetPackets::BuildSpellSnapshot; reader sub_C7BDED
        case SMSG_QUESTGIVER_STATUS:               // MopQuestStatusPackets::BuildStatus
        case SMSG_GOSSIP_MESSAGE:                  // MopGossipPackets::BuildMessage
        case SMSG_GOSSIP_COMPLETE:                 // Direct 18414 empty reader; closes the gossip frame
        case SMSG_GOSSIP_POI:                      // MopGossipPackets::BuildPointOfInterest
        case SMSG_LIST_INVENTORY:                  // MopItemPackets::BuildVendorList
        case SMSG_SELL_ITEM:                       // MopItemPackets::BuildSellResult
        case SMSG_BUY_ITEM:                        // MopItemPackets::BuildBuyItemResult
        case SMSG_BUY_FAILED:                      // MopItemPackets::BuildBuyFailed
        case SMSG_INVENTORY_CHANGE_FAILURE:        // MopItemPackets::BuildInventoryChangeFailure
        case SMSG_ITEM_PUSH_RESULT:                // MopItemPackets::BuildItemPushResult
        case SMSG_LOOT_RESPONSE:                   // MopLootPackets::BuildLootResponse
        case SMSG_LOOT_RELEASE_RESPONSE:           // MopLootPackets::BuildLootReleaseResponse
        case SMSG_LOOT_REMOVED:                    // MopLootPackets::BuildLootRemoved
        case SMSG_LOOT_MONEY_NOTIFY:               // MopLootPackets::BuildLootMoneyNotify
        case SMSG_LOOT_CLEAR_MONEY:                // MopLootPackets::BuildLootClearMoney
        case SMSG_LOOT_START_ROLL:                 // MopGroupLootPackets::BuildStartRoll
        case SMSG_LOOT_ROLL:                       // MopGroupLootPackets::BuildRollUpdate
        case SMSG_LOOT_ROLL_WON:                   // MopGroupLootPackets::BuildRollWinner
        case SMSG_LOOT_ALL_PASSED:                 // MopGroupLootPackets::BuildAllPassed
        case SMSG_GAMEOBJECT_CUSTOM_ANIM:          // MopGameObjectPackets::BuildCustomAnimation
        case SMSG_GAMEOBJECT_DESPAWN_ANIM:         // MopGameObjectPackets::BuildDespawnAnimation
        case SMSG_GAMEOBJECT_PAGETEXT:             // MopGameObjectPackets::BuildPageText
        case SMSG_ENABLE_BARBER_SHOP:              // empty; direct terminal fires BARBER_SHOP_OPEN
        case SMSG_BARBER_SHOP_RESULT:              // uint32 result; success fires BARBER_SHOP_SUCCESS
        case SMSG_FISH_ESCAPED:                    // empty; direct terminal leaf displays ERR_FISH_ESCAPED
        case SMSG_FISH_NOT_HOOKED:                 // empty; direct terminal leaf displays ERR_FISH_NOT_HOOKED
        case SMSG_AREA_TRIGGER_NO_CORPSE:           // MopAreaTriggerPackets::BuildNoCorpse
        case SMSG_EXPLORATION_EXPERIENCE:           // MopAreaTriggerPackets::BuildExplorationExperience
        case SMSG_LOG_XPGAIN:                       // MopProgressionPackets::BuildExperienceGain
        case SMSG_LEVELUP_INFO:                     // MopProgressionPackets::BuildLevelUpInfo
        case SMSG_QUESTGIVER_STATUS_MULTIPLE:       // MopQuestStatusPackets::BuildMultipleStatus
        case SMSG_QUESTGIVER_QUEST_LIST:            // MopQuestGiverPackets::BuildQuestList
        case SMSG_QUESTGIVER_QUEST_DETAILS:         // MopQuestGiverPackets::BuildQuestDetails
        case SMSG_QUESTGIVER_QUEST_INVALID:         // absent custom string bit, then uint32 reason
        case SMSG_QUESTGIVER_QUEST_FAILED:          // uint32 quest ID, then uint32 InventoryResult
        case SMSG_QUESTLOG_FULL:                    // empty; terminal selects ERR_QUEST_LOG_FULL
        case SMSG_QUESTUPDATE_FAILEDTIMER:          // one uint32 quest ID
        case SMSG_QUESTUPDATE_ADD_KILL:             // MopQuestPackets::BuildQuestProgressCredit
        case SMSG_QUESTGIVER_REQUEST_ITEMS:         // MopQuestGiverPackets::BuildQuestRequestItems
        case SMSG_QUESTGIVER_OFFER_REWARD:          // MopQuestGiverPackets::BuildQuestOfferReward
        case SMSG_QUESTGIVER_QUEST_COMPLETE:        // MopQuestGiverPackets::BuildQuestRewardSummary
        case SMSG_QUESTUPDATE_COMPLETE:             // MopQuestGiverPackets::BuildQuestUpdateComplete
        case SMSG_QUEST_QUERY_RESPONSE:              // MopQuestQueryPackets::BuildResponse / BuildAbsentResponse
        case SMSG_QUEST_POI_QUERY_RESPONSE:          // MopQueryPackets::BuildQuestPoiQueryResponse
        case SMSG_QUEST_NPC_QUERY_RESPONSE:          // MopQueryPackets::BuildQuestNpcQueryResponse
        case SMSG_NPC_TEXT_UPDATE:                  // MopNpcTextPackets::BuildResponse
        case SMSG_CHAR_CUSTOMIZE:                  // MopCharacterCustomizePackets::BuildResponse
            return true;

        // In-world query replies. The core already emits genuine 18414 bodies for these
        // (MopQueryPackets::Build*QueryResponse in QueryHandler.cpp), so they satisfy the
        // HARD RULE and must pass suppression -- otherwise the client's post-login name /
        // creature / gameobject / time queries are answered server-side and then silently
        // dropped here, leaving names and tooltips blank.
        case SMSG_NAME_QUERY_RESPONSE:       // MopQueryPackets::BuildNameQueryResponse
        case SMSG_CREATURE_QUERY_RESPONSE:   // MopQueryPackets::BuildCreatureQueryResponse
        case SMSG_GAMEOBJECT_QUERY_RESPONSE: // MopQueryPackets::BuildGameObjectQueryResponse
        case SMSG_PAGE_TEXT_QUERY_RESPONSE:  // MopQueryPackets::BuildPageTextQueryResponse
        case SMSG_ITEM_TEXT_QUERY_RESPONSE:  // WorldSession::HandleItemTextQuery; uint8 found, raw uint64 guid, cstring
        case SMSG_CORPSE_QUERY_RESPONSE:     // MopQueryPackets::BuildCorpseQueryResponse
        case SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE: // MopQueryPackets::BuildCorpseMapPositionQueryResponse
        case SMSG_DEATH_RELEASE_LOC:         // MopDeathPackets::BuildDeathReleaseLocation
        case SMSG_DURABILITY_DAMAGE_DEATH:   // MopDeathPackets::BuildDurabilityDamageDeath
        case SMSG_REQUEST_CEMETERY_LIST_RESPONSE: // MopDeathPackets::BuildCemeteryListResponse
        case SMSG_BATTLE_PET_JOURNAL:        // MopBattlePetPackets::BuildEmptyJournal
        case SMSG_QUEST_CONFIRM_ACCEPT:      // MopQuestPackets::BuildQuestConfirmAccept
        case SMSG_QUEST_PUSH_RESULT:         // MopQuestPackets::BuildQuestPushResult
        case SMSG_INITIAL_SETUP:             // MopQuestPackets::BuildInitialSetup
        case SMSG_SET_QUEST_COMPLETED_BIT:   // MopQuestPackets::BuildSetQuestCompletedBit
        case SMSG_CLEAR_QUEST_COMPLETED_BIT: // MopQuestPackets::BuildClearQuestCompletedBit
        case SMSG_CLEAR_QUEST_COMPLETED_BITS: // MopQuestPackets::BuildClearQuestCompletedBits
        case SMSG_QUERY_TIME_RESPONSE:       // MopQueryPackets::BuildQueryTimeResponse
        case SMSG_PLAYED_TIME:               // MopQueryPackets::BuildPlayedTimeResponse
        case SMSG_REALM_NAME_QUERY_RESPONSE: // MopQueryPackets::BuildRealmNameQueryResponse (client fires the realm query from the name-cache path during login)
        case SMSG_UPDATE_OBJECT:              // UpdateData/MopUpdateObject binary-proved 18414 outer grammar and eligible block serializers
        case SMSG_DESTROY_OBJECT:             // MopUpdateObject::BuildDestroyObject
        case SMSG_AURA_UPDATE:                // MopAuraPackets::BuildAuraUpdate (full snapshots and incremental updates)
        case SMSG_GUILD_EVENT_MOTD:           // MopGuildPackets::BuildGuildMotd
        case SMSG_GUILD_EVENT_PLAYER_JOINED:  // MopGuildPackets::BuildGuildMemberJoined
        case SMSG_GUILD_EVENT_PRESENCE_CHANGE: // MopGuildPackets::BuildGuildPresenceChange
        case SMSG_GUILD_EVENT_PLAYER_LEFT:    // MopGuildPackets::BuildGuildPlayerLeft
        case SMSG_GUILD_RANKS_UPDATE:         // MopGuildPackets::BuildGuildMemberRankUpdate
        case SMSG_GUILD_EVENT_NEW_LEADER:     // MopGuildPackets::BuildGuildNewLeader
        case SMSG_GUILD_EVENT_DISBANDED:      // MopGuildPackets::BuildGuildDisbanded
        case SMSG_GUILD_COMMAND_RESULT:       // command, result, 8-bit name length, raw name
        case SMSG_GUILD_BANK_MONEY_WITHDRAWN: // one uint64 remaining allowance; sub_660A2A -> sub_40F370
        case SMSG_GUILD_PERMISSIONS:          // MopGuildPackets::BuildGuildPermissions, byte-exact vs capture-000006 seq 1959
        case SMSG_GUILD_QUERY_RANKS_RESULT:   // MopGuildPackets::BuildGuildRanks, byte-exact vs capture-000019 seq 185
        case SMSG_GUILD_ROSTER:               // MopGuildPackets::BuildGuildRoster, byte-exact vs capture-000019 seq 923
        case SMSG_GUILD_QUERY_RESPONSE:       // MopGuildPackets::BuildGuildQueryResponse, byte-exact vs capture-000004 seq 39473
        case SMSG_TABARD_VENDOR_ACTIVATE:     // MopGuildPackets::BuildTabardVendorActivate
        case SMSG_SAVE_GUILD_EMBLEM:          // MopGuildPackets::BuildSaveGuildEmblemResult
        case SMSG_BINDER_CONFIRM:              // MopBindPackets::BuildBinderConfirm
        case SMSG_PLAYERBOUND:                 // MopBindPackets::BuildPlayerBound
        case SMSG_LFG_PROPOSAL_UPDATE:        // MopLfgPackets::BuildProposalUpdate, byte-exact vs capture-000044 seq 1948 and capture-000059 seq 2063424
        case SMSG_LFG_ROLE_CHECK_UPDATE:      // MopLfgPackets::BuildRoleCheckUpdate, byte-exact vs capture-000075 seq 891708 and capture-000059 seq 719547
        case SMSG_LFG_BOOT_PLAYER:            // MopLfgPackets::BuildBootPlayer
        case SMSG_LFG_UPDATE_STATUS:          // MopLfgPackets::BuildUpdateStatus
        case SMSG_LFG_QUEUE_STATUS:           // MopLfgPackets::BuildQueueStatus
        case SMSG_LFG_OFFER_CONTINUE:         // 4-byte packed dungeon entry; 31/31 corpus bodies are this shape
        case SMSG_LFG_JOIN_RESULT:            // MopLfgPackets::BuildJoinResult, byte-exact vs capture-000059 seq 490545 (18B refusal),
                                              // capture-000044 seq 1547 (23B) and capture-000075 seq 891753 (24B)
        case SMSG_LFG_PLAYER_INFO:            // MopLfgPackets::BuildEmptyPlayerInfo
        case SMSG_LFG_PARTY_INFO:             // MopLfgPackets::BuildEmptyPartyInfo
        case SMSG_LFG_PLAYER_REWARD:          // money/queuedSlot/xp/actualSlot + 20-bit count + per-reward is-currency bit + 16B entries
        case SMSG_ROLE_CHOSEN:                // nine mask bits (6th is `accepted`), guid 0/3/6, roles u32, guid 5/1/4/2/7;
                                              // derived from sub_6E921A, byte-exact vs 8 corpus packets over 7 captures
        case SMSG_LFG_TELEPORT_DENIED:        // WriteBits(reason & 0xF, 4) + FlushBits; corpus 0x10 and 0x90 are reasons 1 and 9
        case SMSG_LFG_UPDATE_SEARCH:           // MopLfgPackets::BuildEmptyLfrSearchResponse
        case SMSG_RAID_INSTANCE_INFO:         // MopRaidInstancePackets::BuildRaidInstanceInfo
        case SMSG_RESPEC_WIPE_CONFIRM:        // MopRespecPackets::BuildRespecWipeConfirm
        case SMSG_PARTY_MEMBER_STATS:         // MopPartyStatsPackets::BuildResponse
        case SMSG_GROUP_LIST:                 // MopPartyUpdatePackets::BuildPartyUpdate
        case SMSG_GROUP_INVITE:               // MopGroupInvitePackets::BuildInvite
        case SMSG_GROUP_DESTROYED:            // empty body, Group.cpp Initialize(..., 0)
        case SMSG_PARTY_COMMAND_RESULT:       // flat 18414 body, WorldSession::SendPartyResult
        case SMSG_GROUP_UNINVITE:             // empty body, Group.cpp Initialize(..., 0)
        case SMSG_GROUP_SET_LEADER:           // MopGroupPromotePackets::BuildSetLeader
        case SMSG_GROUP_ROLE_POLL_INFORM:     // MopGroupPromotePackets::BuildRolePollInform
        case SMSG_PET_STABLE_LIST:            // MopStablePackets::BuildStableList
        case SMSG_STABLE_RESULT:              // MopStablePackets::BuildStableResult
        case SMSG_RAID_READY_CHECK:           // MopReadyCheckPackets::BuildStarted
        case SMSG_RAID_READY_CHECK_CONFIRM:   // MopReadyCheckPackets::BuildResponse
        case SMSG_RAID_READY_CHECK_COMPLETED: // MopReadyCheckPackets::BuildCompleted
        case SMSG_MAIL_QUERY_NEXT_TIME_RESULT: // MopMailPackets::BuildNextMailTimeResult
        case SMSG_BATTLEFIELD_RATED_INFO:      // MopRatedBattlegroundPackets::BuildBattlefieldRatedInfo
        case SMSG_BATTLEFIELD_STATUS:          // MopBattleGroundPackets::BuildBattlefieldStatusNone
        case SMSG_BATTLEFIELD_STATUS_QUEUED:   // MopBattleGroundPackets::BuildBattlefieldStatusQueued
        case SMSG_BATTLEFIELD_STATUS_NEEDCONFIRMATION: // MopBattleGroundPackets::BuildBattlefieldStatusConfirmation
        case SMSG_BATTLEFIELD_STATUS_ACTIVE:   // MopBattleGroundPackets::BuildBattlefieldStatusActive
        case SMSG_BATTLEFIELD_STATUS_FAILED:   // MopBattleGroundPackets::BuildBattlefieldStatusFailed
        case SMSG_CONQUEST_FORMULA_CONSTANTS:  // MopBattleGroundPackets::BuildConquestFormulaConstants
        case SMSG_CALENDAR_EVENT_INITIAL_INVITE: // MopCalendarPackets::BuildCalendarInitialInvite
        case SMSG_CALENDAR_EVENT_INVITE_STATUS:  // MopCalendarPackets::BuildCalendarInviteStatus
        case SMSG_CALENDAR_EVENT_MODERATOR_STATUS: // MopCalendarPackets::BuildCalendarModeratorStatus
        case SMSG_CALENDAR_SEND_CALENDAR:       // MopCalendarPackets::BuildCalendarList
        case SMSG_CALENDAR_SEND_EVENT:          // MopCalendarPackets::BuildCalendarEvent
        case SMSG_MINIMAP_PING:               // MopGroupMarkerPackets::BuildMinimapPing
        case SMSG_RAID_TARGET_UPDATE_ALL:     // MopGroupMarkerPackets::BuildRaidTargetAll
        case SMSG_RAID_TARGET_UPDATE_SINGLE:  // MopGroupMarkerPackets::BuildRaidTargetSingle
        case SMSG_AUCTION_HELLO:              // MopAuctionPackets::BuildHello
        case SMSG_AUCTION_COMMAND_RESULT:     // MopAuctionPackets::BuildCommandResult
        case SMSG_AUCTION_OWNER_NOTIFICATION: // MopAuctionPackets::BuildSoldOrExpiredNotification
        case SMSG_AUCTION_WON_NOTIFICATION:   // MopAuctionPackets::BuildWonNotification
        case SMSG_AUCTION_OUTBID_NOTIFICATION: // MopAuctionPackets::BuildOutbidNotification
        case SMSG_AUCTION_BID_UPDATE_NOTIFICATION: // MopAuctionPackets::BuildBidUpdateNotification
            return true;
        default:
            break;
    }
    return false;
}

void WorldSession::SendPacket(WorldPacket const* packet, bool bypassSuppress)
{
#ifdef ENABLE_PLAYERBOTS
    //if (GetPlayer()) {
    //    if (GetPlayer()->GetPlayerbotAI())
    //    {
    //        GetPlayer()->GetPlayerbotAI()->HandleBotOutgoingPacket(*packet);
    //    }
    //    else if (GetPlayer()->GetPlayerbotMgr())
    //    {
    //        GetPlayer()->GetPlayerbotMgr()->HandleMasterOutgoingPacket(*packet);
    //    }
    //}
#endif

    if (!m_Socket)
    {
        return;
    }

    // Incremental aura updates produced while the Player object is still loading are redundant:
    // the full post-add snapshot replaces them once the target exists client-side.
    {
        const uint16 opc = uint16(packet->GetOpcode());
        if (!bypassSuppress && m_playerLoading && opc == SMSG_AURA_UPDATE)
        {
            return;
        }
    }

    // PHASE 6c (MoP enter-world bring-up): once a player has entered the world, drop the
    // remaining Cata-format sends. IsEnterWorldConverted() admits opcodes whose reachable
    // senders now emit genuine 18414 bodies, including UPDATE_OBJECT's guarded create/value
    // subsets and DESTROY_OBJECT. bypassSuppress=true is used by exactly two sends in
    // CharacterHandler.cpp: the SMSG_LOGIN_SETTIMESPEED bootstrap, and the world-entry
    // SMSG_SET_TIME_ZONE_INFORMATION that follows the MOTD. Everything else is dropped.
    // This stays active for the whole
    // in-world session, including logout cleanup, and dies with the session; it is a temporary
    // port scaffold until every live sender reaches 18414 parity.
    if (m_suppressWorldSends && !bypassSuppress && !IsEnterWorldConverted(uint16(packet->GetOpcode())))
    {
        return;
    }

    // Phase 1a: an outbound (SMSG) send cannot be validated against the client table, and the server
    // table registers only the login closure until Phase 1b, so any table-based guard here would drop
    // every non-closure SMSG. Send unconditionally; Phase 1b reinstates a server-direction guard once
    // all SMSG are registered.

    const_cast<WorldPacket*>(packet)->FlushBits();

#ifdef MANGOS_DEBUG

    // Code for network use statistic
    static uint64 sendPacketCount = 0;
    static uint64 sendPacketBytes = 0;

    static time_t firstTime = time(NULL);
    static time_t lastTime = firstTime;                     // next 60 secs start time

    static uint64 sendLastPacketCount = 0;
    static uint64 sendLastPacketBytes = 0;

    time_t cur_time = time(NULL);

    if ((cur_time - lastTime) < 60)
    {
        sendPacketCount += 1;
        sendPacketBytes += packet->size();

        sendLastPacketCount += 1;
        sendLastPacketBytes += packet->size();
    }
    else
    {
        uint64 minTime = uint64(cur_time - lastTime);
        uint64 fullTime = uint64(lastTime - firstTime);
        DETAIL_LOG("Send all time packets count: " UI64FMTD " bytes: " UI64FMTD " avr.count/sec: %f avr.bytes/sec: %f time: %u", sendPacketCount, sendPacketBytes, float(sendPacketCount) / fullTime, float(sendPacketBytes) / fullTime, uint32(fullTime));
        DETAIL_LOG("Send last min packets count: " UI64FMTD " bytes: " UI64FMTD " avr.count/sec: %f avr.bytes/sec: %f", sendLastPacketCount, sendLastPacketBytes, float(sendLastPacketCount) / minTime, float(sendLastPacketBytes) / minTime);

        lastTime = cur_time;
        sendLastPacketCount = 1;
        sendLastPacketBytes = packet->wpos();               // wpos is real written size
    }

#endif                                                  // !MANGOS_DEBUG

    // Packet dumps stay in game code so the transport remains opcode-agnostic.
    // Use the stable account id in place of the old ACE socket handle. Preserve
    // the auth-response redaction that WorldSocket applied before its removal.
    if (sLog.IsPacketLoggingEnabled())
    {
        if (packet->GetOpcode() == SMSG_AUTH_RESPONSE)
        {
            sLog.outWorldPacketDumpRedacted(GetAccountId(), packet->GetOpcode(),
                                            LookupOpcodeName(DIR_SERVER, packet->GetOpcode()), packet->size(), false);
        }
        else
        {
            sLog.outWorldPacketDump(GetAccountId(), packet->GetOpcode(),
                                    LookupOpcodeName(DIR_SERVER, packet->GetOpcode()), packet, false);
        }
    }

    // SendPacket() is void and safe to call on a dead link -- unlike the old
    // WorldSocket::SendPacket, there is no -1 failure to react to here.
    m_Socket->SendPacket(*packet);
}

/// Add an incoming packet to the queue
void WorldSession::QueuePacket(WorldPacket* new_packet)
{
    _recvQueue.add(new_packet);
}

/// Logging helper for unexpected opcodes
void WorldSession::LogUnexpectedOpcode(WorldPacket* packet, const char* reason)
{
    sLog.outError("SESSION: received unexpected opcode %s (0x%.4X) %s",
                  LookupOpcodeName(DIR_CLIENT, packet->GetOpcode()),
                  packet->GetOpcode(),
                  reason);
}

/// Logging helper for unexpected opcodes
void WorldSession::LogUnprocessedTail(WorldPacket* packet)
{
    sLog.outError("SESSION: opcode %s (0x%.4X) have unprocessed tail data (read stop at %zu from %zu)",
                  LookupOpcodeName(DIR_CLIENT, packet->GetOpcode()),
                  packet->GetOpcode(),
                  packet->rpos(), packet->wpos());
}

/// Update the WorldSession (triggered by World update)
bool WorldSession::Update(PacketFilter& updater)
{
    ///- Retrieve packets from the receive queue and call the appropriate handlers
    /// not process packets if socket already closed
    WorldPacket* packet = NULL;
    while (m_Socket && !m_Socket->IsClosed() && _recvQueue.next(packet, updater))
    {
        /*#if 1
        sLog.outError( "MOEP: %s (0x%.4X)",
                        LookupOpcodeName(DIR_CLIENT, packet->GetOpcode()),
                        packet->GetOpcode());
        #endif*/

        OpcodeHandler const* opHandlePtr = LookupClientOpcode(packet->GetOpcode());
        if (!opHandlePtr)
        {
            DEBUG_LOG("SESSION: received not handled opcode %s (0x%.4X)",
                      LookupOpcodeName(DIR_CLIENT, packet->GetOpcode()),
                      packet->GetOpcode());
            delete packet;
            continue;
        }
        OpcodeHandler const& opHandle = *opHandlePtr;
        try
        {
            switch (opHandle.status)
            {
                case STATUS_LOGGEDIN:
                    if (!_player)
                    {
                        // skip STATUS_LOGGEDIN opcode unexpected errors if player logout sometime ago - this can be network lag delayed packets
                        if (!m_playerRecentlyLogout)
                        {
                            LogUnexpectedOpcode(packet, "the player has not logged in yet");
                        }
                    }
                    else if (_player->IsInWorld())
                    {
                        ExecuteOpcode(opHandle, packet);
                    }

                    // lag can cause STATUS_LOGGEDIN opcodes to arrive after the player started a transfer

#ifdef ENABLE_PLAYERBOTS
              /*      if (_player && _player->GetPlayerbotMgr())
                    {
                        _player->GetPlayerbotMgr()->HandleMasterIncomingPacket(*packet);
                    }*/
#endif
                    break;
                case STATUS_LOGGEDIN_OR_RECENTLY_LOGGEDOUT:
                    if (!_player && !m_playerRecentlyLogout)
                    {
                        LogUnexpectedOpcode(packet, "the player has not logged in yet and not recently logout");
                    }
                    else
                        // not expected _player or must checked in packet hanlder
                    {
                        ExecuteOpcode(opHandle, packet);
                    }
                    break;
                case STATUS_TRANSFER:
                    if (!_player)
                    {
                        LogUnexpectedOpcode(packet, "the player has not logged in yet");
                    }
                    else if (_player->IsInWorld())
                    {
                        LogUnexpectedOpcode(packet, "the player is still in world");
                    }
                    else
                    {
                        ExecuteOpcode(opHandle, packet);
                    }
                    break;
                case STATUS_LOGGEDIN_OR_TRANSFER:
                    if (!_player)
                    {
                        LogUnexpectedOpcode(packet, "the player has not logged in yet");
                    }
                    else
                    {
                        ExecuteOpcode(opHandle, packet);
                    }
                    break;
                case STATUS_AUTHED:
                    // prevent cheating with skip queue wait
                    if (m_inQueue)
                    {
                        LogUnexpectedOpcode(packet, "the player not pass queue yet");
                        break;
                    }

                    // single from authed time opcodes send in to after logout time
                    // and before other STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT opcodes.
                    if (packet->GetOpcode() != CMSG_SET_ACTIVE_VOICE_CHANNEL)
                    {
                        m_playerRecentlyLogout = false;
                    }

                    ExecuteOpcode(opHandle, packet);
                    break;
                case STATUS_NEVER:
                    sLog.outError("SESSION: received not allowed opcode %s (0x%.4X)",
                                  LookupOpcodeName(DIR_CLIENT, packet->GetOpcode()),
                                  packet->GetOpcode());
                    break;
                case STATUS_UNHANDLED:
                    DEBUG_LOG("SESSION: received not handled opcode %s (0x%.4X)",
                              LookupOpcodeName(DIR_CLIENT, packet->GetOpcode()),
                              packet->GetOpcode());
                    break;
                default:
                    sLog.outError("SESSION: received wrong-status-req opcode %s (0x%.4X)",
                                  LookupOpcodeName(DIR_CLIENT, packet->GetOpcode()),
                                  packet->GetOpcode());
                    break;
            }
        }
        catch (ByteBufferException&)
        {
            sLog.outError("WorldSession::Update ByteBufferException occured while parsing a packet (opcode: %u) from client %s, accountid=%i.",
                          packet->GetOpcode(), GetRemoteAddress().c_str(), GetAccountId());
            if (sLog.HasLogLevelOrHigher(LOG_LVL_DEBUG))
            {
                DEBUG_LOG("Dumping error causing packet:");
                packet->hexlike();
            }

            if (sWorld.getConfig(CONFIG_BOOL_KICK_PLAYER_ON_BAD_PACKET))
            {
                DETAIL_LOG("Disconnecting session [account id %u / address %s] for badly formatted packet.",
                           GetAccountId(), GetRemoteAddress().c_str());

                KickPlayer();
            }
        }

        delete packet;
    }

#ifdef ENABLE_PLAYERBOTS
    //if (GetPlayer() && GetPlayer()->GetPlayerbotMgr())
    //{
    //    GetPlayer()->GetPlayerbotMgr()->UpdateSessions(0);
    //}
#endif

    ///- Cleanup socket pointer if need
    if (m_Socket && m_Socket->IsClosed())
    {
        m_Socket.reset();
    }

 // WARDEN ISSUE - commented out to stop crash
 //   if (m_Socket && !m_Socket->IsClosed() && _warden)
 //       _warden->Update();

    // check if we are safe to proceed with logout
    // logout procedure should happen only in World::UpdateSessions() method!!!
    if (updater.ProcessLogout())
    {
        ///- If necessary, log the player out
        time_t currTime = time(NULL);
        if (!m_Socket || (ShouldLogOut(currTime) && !m_playerLoading))
        {
            LogoutPlayer(true);
        }
// WARDEN ISSUE - commented out to stop crash
//        if (m_Socket && GetPlayer() && _warden)
//           _warden->Update();

        if (!m_Socket)
        {
            return false;                                    // Will remove this session from the world session map
        }
    }

    return true;
}

#ifdef ENABLE_PLAYERBOTS
void WorldSession::HandleBotPackets()
{
    WorldPacket* packet;
    while (_recvQueue.next(packet))
    {
        OpcodeHandler const* opHandle = LookupClientOpcode(packet->GetOpcode());
        if (opHandle)
        {
            (this->*opHandle->handler)(*packet);
        }
        delete packet;
    }
}
#endif

/// %Log the player out
void WorldSession::LogoutPlayer(bool Save)
{
    // PHASE 6c: keep enter-world suppression active THROUGH logout cleanup, then lift it at the
    // END of this function (see below). Suppressing cleanup drops the stale Cata-format teardown
    // sends (loot release, group/social updates, Map::Remove visibility, transport removal); the
    // world-leave control packets (SMSG_LOGOUT_RESPONSE/CANCEL_ACK/COMPLETE) are whitelisted in
    // IsEnterWorldConverted(), so logout still completes. It MUST be cleared before returning:
    // logout keeps the same session alive at character-select, whose SMSG_CHAR_ENUM is not
    // whitelisted, so leaving suppression on hangs the client at "retrieving character list".

    // finish pending transfers before starting the logout
    while (_player && _player->IsBeingTeleportedFar())
    {
        HandleMoveWorldportAckOpcode();
    }

    m_playerLogout = true;
    // Deliberately records the save the caller ASKED for, not whether one will
    // actually be written. m_playerSave feeds PlayerLogoutWithSave(), which
    // other code reads as "logout is going to persist me, so I need not" --
    // SpawnCorpseBones() saves the player itself when it returns false. Clearing
    // it to express suppression would therefore invert that safeguard and
    // provoke the very nested SaveToDB the suppression exists to prevent. The
    // suppression is applied at the save call below instead.
    m_playerSave = Save;

    if (_player)
    {
#ifdef ENABLE_PLAYERBOTS
  /*      if (GetPlayer()->GetPlayerbotMgr())
        {
            GetPlayer()->GetPlayerbotMgr()->LogoutAllBots();
        }*/
#endif

        // Stop cinematic flyover if present; DK may hold an early
        // visibility lease before the flyover becomes active.
        if (CinematicFlyover* flyover = _player->GetCinematicFlyover())
        {
            flyover->Stop();
        }

        sLog.outChar("Account: %d (IP: %s) Logout Character:[%s] (guid: %u)", GetAccountId(), GetRemoteAddress().c_str(), _player->GetName() , _player->GetGUIDLow());

        if (ObjectGuid lootGuid = GetPlayer()->GetLootGuid())
        {
            DoLootRelease(lootGuid);
        }

#ifdef ENABLE_PLAYERBOTS
        //if (_player->GetPlayerbotMgr())
        //{
        //    _player->GetPlayerbotMgr()->LogoutAllBots();
        //}
        //sRandomPlayerbotMgr.OnPlayerLogout(_player);
#endif

        ///- If the player just died before logging out, make him appear as a ghost
        // FIXME: logout must be delayed in case lost connection with client in time of combat
        if (_player->GetDeathTimer())
        {
            _player->GetHostileRefManager().deleteReferences();
            _player->BuildPlayerRepop();
            _player->RepopAtGraveyard();
        }
        else if (!_player->getAttackers().empty())
        {
            // Build the set of player (or player-pet) attackers first; the
            // kill-on-logout cascade below only runs when real players are
            // involved (PvP-style honor death). PvE-only attackers — training
            // dummies, neutral mobs that briefly grabbed aggro, anything whose
            // owner isn't a player — get the cheap CombatStop and then continue
            // into the normal save-and-logout path. Without this guard, just
            // attacking a PACIFIED training dummy and typing /logout would
            // KillPlayer, save the character to DB with 0 HP, and force a
            // graveyard revive on the next login.
            std::set<Player*> aset;
            for (Unit::AttackerSet::const_iterator itr = _player->getAttackers().begin(); itr != _player->getAttackers().end(); ++itr)
            {
                Unit* owner = (*itr)->GetOwner();           // including player controlled case
                if (owner)
                {
                    if (owner->GetTypeId() == TYPEID_PLAYER)
                    {
                        aset.insert((Player*)owner);
                    }
                }
                else if ((*itr)->GetTypeId() == TYPEID_PLAYER)
                {
                    aset.insert((Player*)(*itr));
                }
            }

            _player->CombatStop();
            _player->GetHostileRefManager().setOnlineOfflineState(false);

            if (!aset.empty())
            {
                _player->RemoveAllAurasOnDeath();
                _player->SetPvPDeath(true);
                _player->KillPlayer();
                _player->BuildPlayerRepop();
                _player->RepopAtGraveyard();

                // give honor to all attackers from set like group case
                for (std::set<Player*>::const_iterator itr = aset.begin(); itr != aset.end(); ++itr)
                {
                    (*itr)->RewardHonor(_player, aset.size());
                }

                // give bg rewards and update counters like kill by first from attackers
                // this can't be called for all attackers.
                if (BattleGround* bg = _player->GetBattleGround())
                {
                    bg->HandleKillPlayer(_player, *aset.begin());
                }
            }
        }
        else if (_player->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
        {
            // this will kill character by SPELL_AURA_SPIRIT_OF_REDEMPTION
            _player->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
            //_player->SetDeathPvP(*); set at SPELL_AURA_SPIRIT_OF_REDEMPTION apply time
            _player->KillPlayer();
            _player->BuildPlayerRepop();
            _player->RepopAtGraveyard();
        }
        // drop a flag if player is carrying it
        if (BattleGround* bg = _player->GetBattleGround())
        {
            bg->EventPlayerLoggedOut(_player);
        }

        ///- Teleport to home if the player is in an invalid instance
        if (!_player->m_InstanceValid && !_player->isGameMaster())
        {
            _player->TeleportToHomebind();
            // this is a bad place to call for far teleport because we need player to be in world for successful logout
            // maybe we should implement delayed far teleport logout?
        }

        // FG: finish pending transfers after starting the logout
        // this should fix players beeing able to logout and login back with full hp at death position
        while (_player->IsBeingTeleportedFar())
        {
            HandleMoveWorldportAckOpcode();
        }

        for (int i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            if (BattleGroundQueueTypeId bgQueueTypeId = _player->GetBattleGroundQueueTypeId(i))
            {
                _player->RemoveBattleGroundQueueId(bgQueueTypeId);
                sBattleGroundMgr.m_BattleGroundQueues[ bgQueueTypeId ].RemovePlayer(_player->GetObjectGuid(), true);
            }
        }

        ///- Reset the online field in the account table
        // no point resetting online in character table here as Player::SaveToDB() will set it to 1 since player has not been removed from world at this stage
        // No SQL injection as AccountID is uint32
#ifdef ENABLE_PLAYERBOTS
        //if (!GetPlayer()->GetPlayerbotAI())
        //{
        //    static SqlStatementID id;
        //    // playerbot mod
        //    if (!_player->GetPlayerbotAI())
        //    {
        //        SqlStatement stmt = LoginDatabase.CreateStatement(id, "UPDATE account SET active_realm_id = ? WHERE id = ?");
        //        stmt.PExecute(uint32(0), GetAccountId());
        //    }
        //}
#else
        static SqlStatementID id;

        SqlStatement stmt = LoginDatabase.CreateStatement(id, "UPDATE `account` SET `active_realm_id` = ? WHERE `id` = ?");
        stmt.PExecute(uint32(0), GetAccountId());
#endif
        ///- If the player is in a guild, update the guild roster and broadcast a logout message to other guild members
        if (Guild* guild = sGuildMgr.GetGuildById(_player->GetGuildId()))
        {
            if (MemberSlot* slot = guild->GetMemberSlot(_player->GetObjectGuid()))
            {
                slot->SetMemberStats(_player);
                slot->UpdateLogoutTime();
            }

            guild->BroadcastMemberPresence(_player->GetObjectGuid(), _player->GetName(), false);
        }

        ///- Remove pet
        _player->RemovePet(PET_SAVE_AS_CURRENT);

        ///- empty buyback items and save the player in the database
        // some save parts only correctly work in case player present in map/player_lists (pets, etc)
        // Not gated on SuppressCharacterSave() here: Player::SaveToDB refuses
        // on its own once that is set, which also covers the routes this call
        // site cannot see, such as the deferred DELAYED_SAVE_PLAYER replayed
        // by ProcessDelayedOperations during logout's far-teleport completion.
        if (Save)
        {
            _player->SaveToDB();
        }

        ///- Leave all channels before player delete...
        _player->CleanupChannels();

        // Pending invites always own raw Player pointers, including in
        // PLAYERBOTS builds, so clear them before the player is destroyed.
        // An initiator that logs out also cannot keep a ready check active.
        _player->ReadyCheckComplete();
        _player->UninviteFromGroup();
#ifndef ENABLE_PLAYERBOTS
        // remove player from the group if he is:
        // a) in group; b) not in raid group; c) not in a dungeon finder group;
        // d) logging out normally (not being kicked or disconnected)
        //
        // Dungeon finder groups are exempt for the same reason raids are: the group is the
        // run. Dropping a member on logout loses the group, and with it the group's instance
        // bind -- so the player comes back to a BRAND NEW instance of the same dungeon, with
        // no group, standing inside an instance nobody else is in.
        //
        // Observed live 2026-08-06: five characters idled to the character screen between
        // 21:58:56 and 22:01:21, each sending a normal CMSG_LOGOUT_REQUEST. Each logout
        // stripped that member, leadership walking down the line, until `group_member` held
        // only guid 8 -- the last to leave. Returning put them in instance 3 of Wailing
        // Caverns where they had left instance 2.
        //
        // The m_Socket term is why this went unnoticed: a hard disconnect (alt-F4) leaves no
        // socket and skips the branch entirely, so relogging that way kept the group and
        // looked correct. Only a GRACEFUL logout destroyed it.
        //
        // This also makes restart restoration meaningful. There is no point rebuilding a
        // run's LFG status at group load if one member quitting to the character screen
        // dissolves the group it was rebuilt for.
        if (_player->GetGroup() && !_player->GetGroup()->isRaidGroup() &&
            !_player->GetGroup()->isLFGGroup() && m_Socket)
        {
            _player->RemoveFromGroup();
        }
#endif
        ///- Send update to group
        if (_player->GetGroup())
        {
            _player->GetGroup()->SendUpdate();
        }

        ///- Broadcast a logout message to the player's friends
        sSocialMgr.SendFriendStatus(_player, FRIEND_OFFLINE, _player->GetObjectGuid(), true);
        sSocialMgr.RemovePlayerSocial(_player->GetGUIDLow());

#ifdef ENABLE_PLAYERBOTS
        uint32 guid = GetPlayer()->GetGUIDLow();
#endif

        ///- Used by Eluna
#ifdef ENABLE_ELUNA
        if (Eluna* e = sWorld.GetEluna())
        {
            e->OnLogout(_player);
        }
#endif /* ENABLE_ELUNA */

        ///- Remove the player from the world
        // the player may not be in the world when logging out
        // e.g if he got disconnected during a transfer to another map
        // calls to GetMap in this case may cause crashes
        if (_player->IsInWorld())
        {
            Map* _map = _player->GetMap();
            _map->Remove(_player, true);
        }
        else
        {
            _player->CleanupsBeforeDelete();
            Map::DeleteFromWorld(_player);
        }

        SetPlayer(NULL);                                    // deleted in Remove/DeleteFromWorld call

        ///- Send the 'logout complete' packet to the client
        WorldPacket data;
        MopLogoutPackets::BuildComplete(data);
        SendPacket(&data);

        ///- Since each account can only have one online character at any given time, ensure all characters for active account are marked as offline
        // No SQL injection as AccountId is uint32

        static SqlStatementID updChars;
#ifdef ENABLE_PLAYERBOTS
        SqlStatement stmt = CharacterDatabase.CreateStatement(updChars, "UPDATE characters SET online = 0 WHERE account = ?");
#else
        stmt = CharacterDatabase.CreateStatement(updChars, "UPDATE `characters` SET `online` = 0 WHERE `account` = ?");
#endif
        stmt.PExecute(GetAccountId());

        DEBUG_LOG("SESSION: Sent SMSG_LOGOUT_COMPLETE Message");
    }

    m_playerLogout = false;
    m_playerSave = false;
    m_playerRecentlyLogout = true;
    LogoutRequest(0);

    // PHASE 6c: the player has now left the world (SMSG_LOGOUT_COMPLETE sent above), so lift
    // enter-world suppression. Logout keeps the SAME session alive at character-select, where the
    // client immediately requests its character list (COP_GET_CHARACTERS -> CMSG_CHAR_ENUM).
    // SMSG_CHAR_ENUM is not in the enter-world whitelist, so leaving suppression on drops it and
    // hangs the client at "retrieving character list". Re-armed on the next enter-world
    // (CharacterHandler). Cleanup above stayed suppressed; only the post-world state is freed here.
    m_suppressWorldSends = false;
}

/// Kick a player out of the World
void WorldSession::KickPlayer()
{
    if (m_Socket)
    {
        m_Socket->Close();
    }
}

/**
 * @brief Sends a formatted notification message to the client.
 *
 * @param format The printf-style message format.
 */
void WorldSession::SendNotification(const char* format, ...)
{
    if (format)
    {
        va_list ap;
        char szStr [1024];
        szStr[0] = '\0';
        va_start(ap, format);
        // Guarded even though this overload takes a caller-supplied format: several
        // callers resolve a `mangos_string` row first and pass that as the format -
        // entering a level-locked area (PlayerAreaTrigger) and speaking while muted
        // (ChatHandler) both do - so a malformed row reaches here just as it would
        // the entry-id overload below.
        bool const formatted = SafeFormatDbString(szStr, sizeof(szStr), format, ap);
        va_end(ap);

        if (!formatted)
        {
            sLog.outError("A notification could not be formatted; message dropped. If it came from `mangos_string`, check that row's conversions against its caller.");
            return;
        }

        WorldPacket data;
        if (!MopNotificationPackets::Build(data, std::string(szStr)))
        {
            sLog.outError("A formatted notification violated the 1023-byte NUL-free packet contract; message dropped.");
            return;
        }
        SendPacket(&data);
    }
}

/**
 * @brief Sends a localized formatted notification message to the client.
 *
 * @param string_id The localization string identifier.
 */
void WorldSession::SendNotification(int32 string_id, ...)
{
    char const* format = GetMangosString(string_id);
    if (format)
    {
        va_list ap;
        char szStr [1024];
        szStr[0] = '\0';
        va_start(ap, string_id);
        bool const formatted = SafeFormatDbString(szStr, sizeof(szStr), format, ap);
        va_end(ap);

        if (!formatted)
        {
            sLog.outError("String entry %i could not be formatted; notification dropped. Check its conversions against the caller in `mangos_string`.", string_id);
            return;
        }

        WorldPacket data;
        if (!MopNotificationPackets::Build(data, std::string(szStr)))
        {
            sLog.outError("String entry %i produced a notification outside the 1023-byte NUL-free packet contract; message dropped.", string_id);
            return;
        }
        SendPacket(&data);
    }
}

void WorldSession::SendSetPhaseShift(uint32 phaseMask, uint16 mapId)
{
    ObjectGuid guid = _player->GetObjectGuid();

    uint32 phaseFlags = 0;

    for (uint32 i = 0; i < sPhaseStore.GetNumRows(); i++)
    {
        if (PhaseEntry const* phase = sPhaseStore.LookupEntry(i))
        {
            if (phase->ID == phaseMask)
            {
                phaseFlags = phase->Flags;
                break;
            }
        }
    }

    WorldPacket data(SMSG_SET_PHASE_SHIFT, 30);
    data.WriteGuidMask<2, 3, 1, 6, 4, 5, 0, 7>(guid);
    data.WriteGuidBytes<7, 4>(guid);

    data << uint32(0);                  // number of WorldMapArea.dbc entries to control world map shift * 2

    data.WriteGuidBytes<1>(guid);
    data << uint32(phaseMask ? phaseFlags : 8);
    data.WriteGuidBytes<2, 6>(guid);

    data << uint32(0);                  // number of inactive terrain swaps * 2

    data << uint32(phaseMask ? 2 : 0);  // WRONG: number of Phase.dbc ids * 2
    if (phaseMask)
    {
        data << uint16(phaseMask);
    }

    data.WriteGuidBytes<3, 0>(guid);

    data << uint32(mapId ? 2 : 0);      // number of terrains swaps * 2
    if (mapId)
    {
        data << uint16(mapId);
    }

    data.WriteGuidBytes<5>(guid);
    SendPacket(&data);
}

/*
void WorldSession::SendSetPhaseShift(std::set<uint32> const& phaseIds, std::set<uint32> const& terrainswaps)
{
    if (PlayerLoading())
    {
        return;
    }

    ObjectGuid guid = _player->GetObjectGuid();

    WorldPacket data(SMSG_SET_PHASE_SHIFT, 1 + 8 + 4 + 4 + 4 + 4 + 2 * phaseIds.size() + 4 + terrainswaps.size() * 2);
    data.WriteGuidMask<2, 3, 1, 6, 4, 5, 0, 7>(guid);
    data.WriteGuidBytes<7, 4>(guid);

    data << uint32(0);
    //for (uint8 i = 0; i < worldMapAreaCount; ++i)
    //    data << uint16(0);                    // WorldMapArea.dbc id (controls map display)

    data.WriteGuidBytes<1>(guid);
    data << uint32(phaseIds.size() ? 0 : 8);  // flags (not phasemask)
    data.WriteGuidBytes<2, 6>(guid);

    data << uint32(0);                          // Inactive terrain swaps
    //for (uint8 i = 0; i < inactiveSwapsCount; ++i)
    //    data << uint16(0);

    data << uint32(phaseIds.size() * 2);        // Phase.dbc ids
    for (std::set<uint32>::const_iterator itr = phaseIds.begin(); itr != phaseIds.end(); ++itr)
    {
        data << uint16(*itr);
    }

    data.WriteGuidBytes<3, 0>(guid);

    data << uint32(terrainswaps.size() * 2);    // Active terrain swaps
    for (std::set<uint32>::const_iterator itr = terrainswaps.begin(); itr != terrainswaps.end(); ++itr)
    {
        data << uint16(*itr);
    }

    data.WriteGuidBytes<5>(guid);
    SendPacket(&data);
}
*/

/**
 * @brief Resolves a localized MaNGOS string for this session locale.
 *
 * @param entry The localization entry id.
 * @return const char* The localized string text.
 */
const char* WorldSession::GetMangosString(int32 entry) const
{
    return sObjectMgr.GetMangosString(entry, GetSessionDbLocaleIndex());
}

/**
 * @brief Logs receipt of an unimplemented opcode handler.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::Handle_NULL(WorldPacket& recvPacket)
{
    DEBUG_LOG("SESSION: received unimplemented opcode %s (0x%.4X)",
              LookupOpcodeName(DIR_CLIENT, recvPacket.GetOpcode()),
              recvPacket.GetOpcode());
}

/**
 * @brief Logs receipt of an opcode that should be handled earlier in socket processing.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::Handle_EarlyProccess(WorldPacket& recvPacket)
{
    sLog.outError("SESSION: received opcode %s (0x%.4X) that must be processed by proto::ClientConnection",
                  LookupOpcodeName(DIR_CLIENT, recvPacket.GetOpcode()),
                  recvPacket.GetOpcode());
}

/**
 * @brief Logs receipt of an opcode reserved for server-side use.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::Handle_ServerSide(WorldPacket& recvPacket)
{
    sLog.outError("SESSION: received server-side opcode %s (0x%.4X)",
                  LookupOpcodeName(DIR_CLIENT, recvPacket.GetOpcode()),
                  recvPacket.GetOpcode());
}

/**
 * @brief Logs receipt of a deprecated client opcode.
 *
 * @param recvPacket The received opcode packet.
 */
void WorldSession::Handle_Deprecated(WorldPacket& recvPacket)
{
    sLog.outError("SESSION: received deprecated opcode %s (0x%.4X)",
                  LookupOpcodeName(DIR_CLIENT, recvPacket.GetOpcode()),
                  recvPacket.GetOpcode());
}

/**
 * @brief Sends the authentication response or queue position to the client.
 *
 * @param position The queue position, or zero when login may proceed immediately.
 */
void WorldSession::SendAuthWaitQue(uint32 position)
{
    // position 0 is the RELEASE, and it is BYTE-IDENTICAL to ACCEPTED -- not a bare AUTH_OK.
    // The old code sent AUTH_OK with hasAccountData=0, which the client rewrites to AUTH_FAILED
    // (13); with queued=0 there is no queue branch to mask it, so the release was the one packet
    // where the defect actually bit.
    //
    // Expansion() is the account's entitlement ALREADY CLAMPED to the realm (see
    // WorldGateway::LookupAccount's expansion clamp); the config value is the realm's own cap.
    // They differ only for a restricted account.
    // BuildAuthResponseQueued routes position 0 to Accepted itself, so the release rule lives in
    // the serializer rather than in every caller that has to remember it.
    WorldPacket packet;
    MopAuth::BuildAuthResponseQueued(packet, position, Expansion(),
                                     uint8(sWorld.getConfig(CONFIG_UINT32_EXPANSION)));
    SendPacket(&packet);
}

void WorldSession::LoadGlobalAccountData()
{
    LoadAccountData(
        CharacterDatabase.PQuery("SELECT `type`, `time`, `data` FROM `account_data` WHERE `account` = '%u'", GetAccountId()),
        GLOBAL_CACHE_MASK
    );
}

void WorldSession::LoadAccountData(QueryResult* result, uint32 mask)
{
    for (uint32 i = 0; i < NUM_ACCOUNT_DATA_TYPES; ++i)
        if (mask & (1 << i))
        {
            m_accountData[i] = AccountData();
        }

    if (!result)
    {
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 type = fields[0].GetUInt32();
        if (type >= NUM_ACCOUNT_DATA_TYPES)
        {
            sLog.outError("Table `%s` have invalid account data type (%u), ignore.",
                          mask == GLOBAL_CACHE_MASK ? "account_data" : "character_account_data", type);
            continue;
        }

        if ((mask & (1 << type)) == 0)
        {
            sLog.outError("Table `%s` have non appropriate for table  account data type (%u), ignore.",
                          mask == GLOBAL_CACHE_MASK ? "account_data" : "character_account_data", type);
            continue;
        }

        m_accountData[type].Time = time_t(fields[1].GetUInt64());
        m_accountData[type].Data = fields[2].GetCppString();
    }
    while (result->NextRow());

    delete result;
}

void WorldSession::SetAccountData(AccountDataType type, time_t time_, const std::string& data)
{
    if ((1 << type) & GLOBAL_CACHE_MASK)
    {
        uint32 acc = GetAccountId();

        static SqlStatementID delId;
        static SqlStatementID insId;

        CharacterDatabase.BeginTransaction();

        SqlStatement stmt = CharacterDatabase.CreateStatement(delId, "DELETE FROM `account_data` WHERE `account` = ? AND `type` = ?");
        stmt.PExecute(acc, uint32(type));

        stmt = CharacterDatabase.CreateStatement(insId, "INSERT INTO `account_data` VALUES (?,?,?,?)");
        stmt.PExecute(acc, uint32(type), uint64(time_), data.c_str());

        CharacterDatabase.CommitTransaction();
    }
    else
    {
        // _player can be NULL and packet received after logout but m_GUID still store correct guid
        if (!m_GUIDLow)
        {
            return;
        }

        static SqlStatementID delId;
        static SqlStatementID insId;

        CharacterDatabase.BeginTransaction();

        SqlStatement stmt = CharacterDatabase.CreateStatement(delId, "DELETE FROM `character_account_data` WHERE `guid` = ? AND `type` = ?");
        stmt.PExecute(m_GUIDLow, uint32(type));

        stmt = CharacterDatabase.CreateStatement(insId, "INSERT INTO `character_account_data` VALUES (?,?,?,?)");
        stmt.PExecute(m_GUIDLow, uint32(type), uint64(time_), data.c_str());

        CharacterDatabase.CommitTransaction();
    }

    m_accountData[type].Time = time_;
    m_accountData[type].Data = data;
}

void WorldSession::SendAccountDataTimes(uint32 mask)
{
    static_assert(NUM_ACCOUNT_DATA_TYPES == MopInitialPackets::ACCOUNT_DATA_COUNT,
        "account data wire count changed");
    std::array<uint32, MopInitialPackets::ACCOUNT_DATA_COUNT> times{};
    for (uint32 i = 0; i < NUM_ACCOUNT_DATA_TYPES; ++i)
        times[i] = uint32(GetAccountData(AccountDataType(i))->Time);

    WorldPacket data(SMSG_ACCOUNT_DATA_TIMES, 41);
    MopInitialPackets::BuildAccountDataTimes(data, times, mask, uint32(time(NULL)), true);
    SendPacket(&data);
}

/**
 * @brief Loads tutorial flag state for the current account.
 */
void WorldSession::LoadTutorialsData()
{
    for (int aX = 0 ; aX < 8 ; ++aX)
    {
        m_Tutorials[ aX ] = 0;
    }

    QueryResult* result = CharacterDatabase.PQuery("SELECT `tut0`,`tut1`,`tut2`,`tut3`,`tut4`,`tut5`,`tut6`,`tut7` FROM `character_tutorial` WHERE `account` = '%u'", GetAccountId());

    if (!result)
    {
        m_tutorialState = TUTORIALDATA_NEW;
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        for (int iI = 0; iI < 8; ++iI)
        {
            m_Tutorials[iI] = fields[iI].GetUInt32();
        }
    }
    while (result->NextRow());

    delete result;

    m_tutorialState = TUTORIALDATA_UNCHANGED;
}

/**
 * @brief Sends the current tutorial flags to the client.
 */
void WorldSession::SendTutorialsData()
{
    std::array<uint32, MopInitialPackets::TUTORIAL_WORD_COUNT> words{};
    for (uint32 i = 0; i < MopInitialPackets::TUTORIAL_WORD_COUNT; ++i)
        words[i] = m_Tutorials[i];

    WorldPacket data(SMSG_TUTORIAL_FLAGS, 4 * 8);
    MopInitialPackets::BuildTutorialFlags(data, words);
    SendPacket(&data);
}

/**
 * @brief Persists tutorial flag state changes for the current account.
 */
void WorldSession::SaveTutorialsData()
{
    static SqlStatementID updTutorial ;
    static SqlStatementID insTutorial ;

    switch (m_tutorialState)
    {
        case TUTORIALDATA_CHANGED:
        {
            SqlStatement stmt = CharacterDatabase.CreateStatement(updTutorial, "UPDATE `character_tutorial` SET `tut0`=?, `tut1`=?, `tut2`=?, `tut3`=?, `tut4`=?, `tut5`=?, `tut6`=?, `tut7`=? WHERE `account` = ?");
            for (int i = 0; i < 8; ++i)
            {
                stmt.addUInt32(m_Tutorials[i]);
            }

            stmt.addUInt32(GetAccountId());
            stmt.Execute();
        }
        break;

        case TUTORIALDATA_NEW:
        {
            SqlStatement stmt = CharacterDatabase.CreateStatement(insTutorial, "INSERT INTO `character_tutorial` (`account`,`tut0`,`tut1`,`tut2`,`tut3`,`tut4`,`tut5`,`tut6`,`tut7`) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");

            stmt.addUInt32(GetAccountId());
            for (int i = 0; i < 8; ++i)
            {
                stmt.addUInt32(m_Tutorials[i]);
            }

            stmt.Execute();
        }
        break;
        case TUTORIALDATA_UNCHANGED:
            break;
    }

    m_tutorialState = TUTORIALDATA_UNCHANGED;
}

// Send chat information about aborted transfer (mostly used by Player::SendTransferAbortedByLockstatus())
void WorldSession::SendTransferAborted(uint32 mapid, uint8 reason, uint8 arg)
{
    WorldPacket data(SMSG_TRANSFER_ABORTED, 6);
    MopTransferPackets::BuildTransferAborted(data, mapid, reason, arg);
    SendPacket(&data);
}

void WorldSession::ReadAddonsInfo(ByteBuffer &data)
{
    if (data.rpos() + 4 > data.size())
    {
        return;
    }
    uint32 size;
    data >> size;

    if (!size)
    {
        return;
    }

    if (size > 0xFFFFF)
    {
        sLog.outError("WorldSession::ReadAddonsInfo addon info too big, size %u", size);
        return;
    }

    uLongf uSize = size;

    uint32 pos = data.rpos();

    ByteBuffer addonInfo;
    addonInfo.resize(size);

    if (uncompress(const_cast<uint8*>(addonInfo.contents()), &uSize, const_cast<uint8*>(data.contents() + pos), data.size() - pos) == Z_OK)
    {
        uint32 addonsCount;
        addonInfo >> addonsCount;                         // addons count
        DEBUG_LOG("Addon count: %u", addonsCount);

        for (uint32 i = 0; i < addonsCount; ++i)
        {
            std::string addonName;
            uint8 enabled;
            uint32 crc, urlHash;       // per client RE (FACTS_mop548_addon_info): name, usePubKey, crc, urlHash

            // check next addon data format correctness
            if (addonInfo.rpos() + 1 > addonInfo.size())
            {
                return;
            }

            addonInfo >> addonName;

            addonInfo >> enabled >> crc >> urlHash;

            DEBUG_LOG("ADDON: Name: %s, Enabled: 0x%x, CRC: 0x%x, UrlHash: 0x%x", addonName.c_str(), enabled, crc, urlHash);

            m_addonsList.push_back(AddonInfo(addonName, enabled, crc));
        }

        uint32 currentTime;        // trailing field after the addon list (client RE)
        addonInfo >> currentTime;

        if (addonInfo.rpos() != addonInfo.size())
        {
            DEBUG_LOG("packet under read!");
        }
    }
    else
    {
        sLog.outError("Addon packet uncompress error!");
    }
}

void WorldSession::SendAddonsInfo()
{
    unsigned char tdata[256] =
    {
        0xC3, 0x5B, 0x50, 0x84, 0xB9, 0x3E, 0x32, 0x42, 0x8C, 0xD0, 0xC7, 0x48, 0xFA, 0x0E, 0x5D, 0x54,
        0x5A, 0xA3, 0x0E, 0x14, 0xBA, 0x9E, 0x0D, 0xB9, 0x5D, 0x8B, 0xEE, 0xB6, 0x84, 0x93, 0x45, 0x75,
        0xFF, 0x31, 0xFE, 0x2F, 0x64, 0x3F, 0x3D, 0x6D, 0x07, 0xD9, 0x44, 0x9B, 0x40, 0x85, 0x59, 0x34,
        0x4E, 0x10, 0xE1, 0xE7, 0x43, 0x69, 0xEF, 0x7C, 0x16, 0xFC, 0xB4, 0xED, 0x1B, 0x95, 0x28, 0xA8,
        0x23, 0x76, 0x51, 0x31, 0x57, 0x30, 0x2B, 0x79, 0x08, 0x50, 0x10, 0x1C, 0x4A, 0x1A, 0x2C, 0xC8,
        0x8B, 0x8F, 0x05, 0x2D, 0x22, 0x3D, 0xDB, 0x5A, 0x24, 0x7A, 0x0F, 0x13, 0x50, 0x37, 0x8F, 0x5A,
        0xCC, 0x9E, 0x04, 0x44, 0x0E, 0x87, 0x01, 0xD4, 0xA3, 0x15, 0x94, 0x16, 0x34, 0xC6, 0xC2, 0xC3,
        0xFB, 0x49, 0xFE, 0xE1, 0xF9, 0xDA, 0x8C, 0x50, 0x3C, 0xBE, 0x2C, 0xBB, 0x57, 0xED, 0x46, 0xB9,
        0xAD, 0x8B, 0xC6, 0xDF, 0x0E, 0xD6, 0x0F, 0xBE, 0x80, 0xB3, 0x8B, 0x1E, 0x77, 0xCF, 0xAD, 0x22,
        0xCF, 0xB7, 0x4B, 0xCF, 0xFB, 0xF0, 0x6B, 0x11, 0x45, 0x2D, 0x7A, 0x81, 0x18, 0xF2, 0x92, 0x7E,
        0x98, 0x56, 0x5D, 0x5E, 0x69, 0x72, 0x0A, 0x0D, 0x03, 0x0A, 0x85, 0xA2, 0x85, 0x9C, 0xCB, 0xFB,
        0x56, 0x6E, 0x8F, 0x44, 0xBB, 0x8F, 0x02, 0x22, 0x68, 0x63, 0x97, 0xBC, 0x85, 0xBA, 0xA8, 0xF7,
        0xB5, 0x40, 0x68, 0x3C, 0x77, 0x86, 0x6F, 0x4B, 0xD7, 0x88, 0xCA, 0x8A, 0xD7, 0xCE, 0x36, 0xF0,
        0x45, 0x6E, 0xD5, 0x64, 0x79, 0x0F, 0x17, 0xFC, 0x64, 0xDD, 0x10, 0x6F, 0xF3, 0xF5, 0xE0, 0xA6,
        0xC3, 0xFB, 0x1B, 0x8C, 0x29, 0xEF, 0x8E, 0xE5, 0x34, 0xCB, 0xD1, 0x2A, 0xCE, 0x79, 0xC3, 0x9A,
        0x0D, 0x36, 0xEA, 0x01, 0xE0, 0xAA, 0x91, 0x20, 0x54, 0xF0, 0x72, 0xD8, 0x1E, 0xC7, 0x89, 0xD2
    };

    // MoP 5.4.8 SMSG_ADDON_INFO is BIT-PACKED. The pre-MoP FLAT layout that used to live here is why
    // the 18414 client rejected every addon (all shown disabled / "download an updated version"):
    // it cannot parse a single field of the flat form. Layout: header = banned-addon count (18 bits)
    // + addon count (23 bits); then ONE 3-bit flag group per addon (hasUrl, enabled, serverSendsKey),
    // byte-aligned via FlushBits; then per-addon byte data; then the banned block.
    //
    // The framing below is confirmed against retail: all 128 SMSG_ADDON_INFO bodies in the 18414
    // capture corpus are 286 bytes and decode under exactly this grammar with zero residual
    // (bannedCount 0, addonCount 44, every flag group 0b010, every per-addon record
    // 01 00 00 00 00 02). Retail clears serverSendsKey and ships no key at all, because a retail
    // client already holds one in its .pub cache; we send ours so that a fresh install validates
    // without depending on what the archives happen to carry.
    //
    // Enable/ban/allow policy will move into a dedicated module (AddonRegistry).
    WorldPacket data(SMSG_ADDON_INFO);

    data.WriteBits(0, 18);                                  // banned-addon count (none yet)
    data.WriteBits(uint32(m_addonsList.size()), 23);        // addon count
    for (AddonsList::const_iterator itr = m_addonsList.begin(); itr != m_addonsList.end(); ++itr)
    {
        data.WriteBit(0);                                   // has URL file
        data.WriteBit(1);                                   // enabled
        data.WriteBit(1);                                   // server includes its public key (REQUIRED)
    }
    data.FlushBits();

    for (AddonsList::const_iterator itr = m_addonsList.begin(); itr != m_addonsList.end(); ++itr)
    {
        // serverSendsKey bit set -> the 256-byte RSA public key, emitted in the client's scatter
        // order rather than modulus order. See MopAddonPackets::kAddonKeyWireOrder: the parser
        // stores wire byte i at key[kAddonKeyWireOrder[i]], so a raw append left the client with a
        // key wrong at 254 of 256 positions, failing signature verification and loading every
        // "## Secure:" Blizzard addon untrusted.
        MopAddonPackets::AppendAddonPublicKey(data, tdata);
        // 'enabled' bit was set -> { u8 enabled, u32 reserved }; then the state byte (2 = valid/loaded).
        data << uint8(1);
        data << uint32(0);
        data << uint8(2);
    }

    m_addonsList.clear();

    // banned-addon block is empty (count 0 in the header)
    SendPacket(&data);
}

void WorldSession::SendRedirectClient(std::string& ip, uint16 port)
{
    uint32 ip2 = inet_addr(ip.c_str());
    WorldPacket pkt(SMSG_CONNECT_TO, 4 + 2 + 4 + 20);

    pkt << uint32(ip2);                                     // inet_addr(ipstr)
    pkt << uint16(port);                                    // port

    pkt << uint32(0);                                       // unknown

    // GetSessionKey() returns m_s -- the SRP6 SALT, not K -- and AsByteArray() returns
    // GetNumBytes() bytes while this asked for 40: a heap OVER-READ whenever the value is shorter.
    // Both are fixed by using the canonical raw-40 K. (SendRedirectClient has ZERO call sites, so
    // neither bug is live today; it is migrated because Phase 3 puts it on a reachable path and
    // because leaving one BigNumber K consumer behind is how the short-K bug returns.)
    HMACSHA1 sha1(uint32(MopAuth::SESSION_KEY_LEN), GetSessionKeyRaw());
    sha1.UpdateData((uint8*)&ip2, 4);
    sha1.UpdateData((uint8*)&port, 2);
    sha1.Finalize();
    if (!sha1.IsValid())
    {
        sLog.outError("WorldSession::SendRedirectClient: HMAC-SHA1 failed; redirect not sent.");
        return;
    }
    pkt.append(sha1.GetDigest(), 20);                       // hmacsha1(ip+port) w/ sessionkey as seed

    SendPacket(&pkt);
}

/**
 * @brief Executes a validated opcode handler with delayed-teleport protection.
 *
 * @param opHandle The opcode handler metadata.
 * @param packet The packet to process.
 */
void WorldSession::ExecuteOpcode(OpcodeHandler const& opHandle, WorldPacket* packet)
{
#ifdef ENABLE_ELUNA
    if (Eluna* e = sWorld.GetEluna())
    {
        if (!e->OnPacketReceive(this, *packet))
        {
            return;
        }
    }
#endif /* ENABLE_ELUNA */

    // need prevent do internal far teleports in handlers because some handlers do lot steps
    // or call code that can do far teleports in some conditions unexpectedly for generic way work code
    if (_player)
    {
        _player->SetCanDelayTeleport(true);
    }

    (this->*opHandle.handler)(*packet);

    if (_player)
    {
        // can be not set in fact for login opcode, but this not create porblems.
        _player->SetCanDelayTeleport(false);

        // we should execute delayed teleports only for alive(!) players
        // because we don't want player's ghost teleported from graveyard
        if (_player->IsHasDelayedTeleport())
        {
            _player->TeleportTo(_player->m_teleport_dest, _player->m_teleport_options);
        }
    }

    if (packet->rpos() < packet->wpos() && sLog.HasLogLevelOrHigher(LOG_LVL_DEBUG))
    {
        LogUnprocessedTail(packet);
    }
}

/**
 * @brief Initializes Warden for the authenticated client platform.
 *
 * @param build The client build number.
 * @param k The session key material.
 * @param os The reported client operating system.
 */
void WorldSession::InitWarden(uint16 build, BigNumber* k, std::string const& os)
{
    _build = build;

    if (os == "Win" && sWorld.getConfig(CONFIG_BOOL_WARDEN_WIN_ENABLED))
    {
        _warden = new WardenWin();
        _warden->Init(this, k);
    }
    else if (os == "OSX" && sWorld.getConfig(CONFIG_BOOL_WARDEN_OSX_ENABLED))
    {
        _warden = new WardenMac();
        _warden->Init(this, k);
    }
}
