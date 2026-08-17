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

#ifndef OPCODES_REFERENCE_H
#define OPCODES_REFERENCE_H

// Client-reference rows are generated from clean-room binary evidence -- do not hand-edit
// their names, values, order, confidence, or notes. The ACTIVE/DORMANT/DOC overlay and
// server-binding annotations are source-derived by contrib/sync_mop_opcode_reference_status.ps1.
// Inert reference map of the MoP 5.4.8.18414 client's network opcode vocabulary.
// This header is NEVER #included. Activating an opcode = add it to Opcodes.h, register it
// with DefC()/DefS() in Opcodes.cpp, and implement the WorldSession handler.
//
// PROVENANCE (clean-room; derived from the client binary Wow.exe 5.4.8.18414):
//   CMSG  = opcode literals read out of the client's send serializer. Strongest tier: the
//           client materializes each value, so these are read, not inferred.
//   SMSG  = IMPLEMENTED WORLD-PATH DISPATCH LEAVES. This is deliberately NOT phrased as
//           "the client accepts X" -- see SCOPE below. Derived by emulating the client's
//           world receive path:
//             framing  sub_A6305D           PacketPipe_CliSvr.cpp; unpacks the wire header
//                                           (size << 13) | (opcode & 0x1FFF)
//               -> receive root sub_799310  NetClient message root. Owns the
//                                           "WORLD OF WARCRAFT CONNECTION - SERVER TO
//                                           CLIENT" handshake, so it is the first consumer
//                                           of the world stream. Handles 4 opcodes itself
//                                           (0x0D48, 0x1148, 0x1168, 0x1568) and dispatches
//                                           the SPECIAL_CONTROL family INLINE (see below).
//               -> gate sub_798BBC          + dynamic-family probe sub_79795D for
//                                           (op & 0x148) == 0x100
//               -> queue sub_79A97F(...,23) NETEVENTQUEUENODE, event type 23
//               -> drain -> sub_79864E      requires netState == 5, wraps payload in a reader
//               -> router sub_797CEE        -> 5 computed dispatchers
//                                           (sub_659694 / sub_68EC4C / sub_C80E74 /
//                                            sub_68F873 / sub_C6763F)
//           A LEAF EXISTS when the computed selector reaches an implemented case that
//           builds a message-parser and returns success. 881 = 660+65+29+80+47 leaves.
//
// SCOPE OF THE 881 -- what it does and does not cover:
//   The 881 counts leaves reached through the QUEUED world path only. It deliberately
//   excludes the SPECIAL_CONTROL / services family, which never reaches the world router
//   because the receive root dispatches it inline and returns. That family is documented
//   separately below and IS present in this table -- 12 implemented of 32, being the 4
//   root literals plus 8 leaves in the inline dispatcher sub_C80CDA. Those 12 are the
//   login/services opcodes (auth challenge/response, char enum, addon info, cache version,
//   queue status). The sibling build 5.4.7.18019 decomposes identically: 4 + 8 = 12.
//   OPEN -- whether the type-23 queue can discard between enqueue and drain is not proven.
//           If it can, 881 is an upper bound on what is actually dispatched.
//   Cross-model reviewed (APPROVE-WITH-FOLLOWUP): no leaf false-negatives/positives in the
//   dispatcher emulation. The wording above was tightened afterwards: the emulated router
//   sits downstream of a gate and a message queue, so "implemented leaves" is the claim the
//   evidence supports, and "the client accepts X" is not.
//
// STATUS (mirrors DBCStructure_reference.h):
//   ACTIVE  = this direction + wire value is present in Opcodes.h AND registered via
//             DefC()/DefS(); server-binding= records a differing active server label.
//   DORMANT = this direction + wire value is present in Opcodes.h but never registered
//             (enum constant only, no handler).
//   DOC     = the client materializes this value when sending (CMSG), or the world router
//             has a dispatch leaf for it (SMSG), but Opcodes.h does not list it.

/*
 * SUBSYSTEM SUMMARY -- SMSG by client module (handler address -> owning module).
 * Module names are the client's own, retained in the binary via __FILE__ literals.
 * Attribution was RECALIBRATED: translation-unit bracket inference agrees with the
 * independent guard-init signal 98.4%% (n=191); nearest-reference locality agrees only
 * 71.4%% -- hence bracket => medium, locality => low. Trust the marker, not the module.
 *
 *   unresolved                        154
 *   Player_C.cpp                      140
 *   Unit_C.cpp                        112
 *   GameUI.cpp                         49
 *   GuildInfo.cpp                      33
 *   Spell_C.cpp                        32
 *   BattlefieldInfo.cpp                31
 *   Calendar.cpp                       23
 *   ChatFrame.cpp                      21
 *   LFGInfo.cpp                        16
 *   PetJournalInfo.cpp                 15
 *   AchievementInfo.cpp                13
 *   login/auth message layer           12
 *   DBCacheInstances.cpp               12
 *   BattlePetFrame.cpp                 11
 *   LootFrame.cpp                      11
 *   EquipmentManager.cpp               10
 *   GossipInfo.cpp                     10
 *   Client.cpp                         10
 *   DressUpModelFrame.cpp               9
 *   ResearchFrame.cpp                   9
 *   AuctionHouse.cpp                    9
 *   CGlueMgr.cpp                        8
 *   LossOfControlUI.cpp                 8
 *   GMTicketInfo.cpp                    7
 *   DuelInfo.cpp                        7
 *   PartyInfo.cpp                       7
 *   LootRoll.cpp                        7
 *   CharacterModelBase.cpp              7
 *   ReputationInfo.cpp                  7
 *   UnitCombat_C.cpp                    7
 *   WardenClient.cpp                    7
 *   AuthChallenge.cpp                   6
 *   UnitCombatLog_C.cpp                 6
 *   blp.cpp                             5
 *   GameObject_C.cpp                    4
 *   UIBindings.cpp                      4
 *   MailInfo.cpp                        4
 *   VoidStorage_C.cpp                   4
 *   AreaTrigger_C.cpp                   4
 *   RaidMarkers.cpp                     3
 *   TaxiMapFrame.cpp                    3
 *   PetitionInfo.cpp                    3
 *   GuildBankFrame.cpp                  3
 *   SI3.cpp                             3
 *   CUFProfiles.cpp                     3
 *   TradeFrame.cpp                      3
 *   LootHistory.cpp                     3
 *   AnimReplacementSet.cpp              2
 *   Log.cpp                             2
 *   BattlenetLogin.cpp                  2
 *   SpecializationInfo.cpp              2
 *   CharacterCreation.cpp               2
 *   VignetteInfo.cpp                    2
 *   AccountData.cpp                     2
 *   ObjectMgrClient.cpp                 2
 *   WowClientDB2.cpp                    2
 *   SpellVisuals.cpp                    2
 *   BattlenetUI.cpp                     2
 *   IncomingResurrection.cpp            2
 *   KnowledgeBase.cpp                   2
 *   ItemSocketInfo.cpp                  1
 *   SI3ZoneSounds.cpp                   1
 *   CheckExecutableSignature.cpp        1
 *   TradeSkillFrame.cpp                 1
 *   QuestCache.cpp                      1
 *   UnitAnim_C.cpp                      1
 *   Effect_C.cpp                        1
 *   QuestTextParserWOW.cpp              1
 *   SpellBookFrame.cpp                  1
 *   NetClient.cpp                       1
 *   SComp.cpp                           1
 *   Missile_C.cpp                       1
 *   ConsoleVar.cpp                      1
 *   AreaTriggers.cpp                    1
 *   ContainerFrame.cpp                  1
 *   ComSatClient.cpp                    1
 *   FriendList.cpp                      1
 *   CalendarEvent.cpp                   1
 *   EncounterJournal.cpp                1
 *   Movie.cpp                           1
 *   SceneObject_C.cpp                   1
 *   QuestLog.cpp                        1
 *   LoadingScreen.cpp                   1
 *   Reforge.cpp                         1
 *   TOTAL SMSG rows                   925
 *
 * SUBSYSTEM CONFIDENCE: high=365, low=221, medium=182, none=157
 * STATUS TOTALS: ACTIVE=662, DOC=420, DORMANT=435
 *   SMSG: ACTIVE=364, DOC=270, DORMANT=290
 *   CMSG: ACTIVE=298, DOC=150, DORMANT=145
 */

// CAVEATS -- read before trusting any single row:
//   * SPECIAL_CONTROL is a FAMILY ((v & 0x3DE) == 0x148, 32 values); only the 12 listed
//     here are implemented -- the other 20 reach return 0.
//   * The DYNAMIC family ((v & 0x148) == 0x100, 1024 values) is RESOLVED: the slot<->wire
//     transform was recovered and every store into the callback arrays swept. Only 32 are
//     actually INSTALLED and appear here; 992 are provably NEVER installed and are omitted;
//     1 (0x1DA3) is an orphan teardown with no install in either build -- UNPROVABLE.
//   * 107 opcodes REACH A LEAF and are PARSED, but their handler is NEVER INSTALLED in this build
//     (the guard global is never written non-zero anywhere in the image). They are marked
//     'handler never installed'. The client still consumes the message; nothing acts on it.
//   * Subsystem attribution is evidence-graded (high/medium/low/none). 'low' is locality
//     only (~71%% accurate) -- not safe to build on. No opcode NAMES are asserted from it.
//   * SMSG names for DOC rows are reference-derived. Unresolved ones are emitted as
//     SMSG_UNKNOWN_0xNNNN -- never promote those into Opcodes.h.

#ifndef OPCODE_REF_INT_TYPES
#define OPCODE_REF_INT_TYPES
#include <cstdint>
typedef uint16_t uint16;
#endif

/*
 * OPCODE MAP -- 1520 client opcodes (build 5.4.8.18414), grouped by direction then module.
 *   <name>  <value>  <STATUS>  [conf]  <note>
 */
 *
 * ==== SMSG (925) ====
 *
 *  -- AccountData.cpp (2) --
 *   SMSG_UPDATE_ACCOUNT_DATA                       0x0AAE  ACTIVE
 *   SMSG_ACCOUNT_DATA_TIMES                        0x162B  ACTIVE
 *
 *  -- AchievementInfo.cpp (13) --
 *   SMSG_RESPOND_INSPECT_ACHIEVEMENTS              0x009E  DORMANT  [low-conf]
 *   SMSG_ALL_ACCOUNT_CRITERIA                      0x0A9E  DORMANT  [low-conf]
 *   SMSG_CRITERIA_UPDATE                           0x0E9B  ACTIVE   [low-conf]
 *   SMSG_GUILD_ACHIEVEMENT_DATA                    0x0EF8  DORMANT  [medium-conf]
 *   SMSG_ALL_ACHIEVEMENT_DATA                      0x180A  ACTIVE   [low-conf]
 *   SMSG_ACCOUNT_CRITERIA_UPDATE                   0x189E  DORMANT  [low-conf]
 *   SMSG_ACHIEVEMENT_DELETED                       0x1A2F  DORMANT  [medium-conf]
 *   SMSG_GUILD_CRITERIA_DELETED                    0x1B60  DORMANT  [low-conf]
 *   SMSG_GUILD_ACHIEVEMENT_MEMBERS                 0x1B70  DORMANT  [low-conf]
 *   SMSG_GUILD_CRITERIA_DATA                       0x1BF0  DORMANT  [low-conf]
 *   SMSG_GUILD_ACHIEVEMENT_EARNED                  0x1BF1  DORMANT  [medium-conf]
 *   SMSG_CRITERIA_DELETED                          0x1C33  DORMANT  [low-conf]
 *   SMSG_GUILD_ACHIEVEMENT_DELETED                 0x1E61  DORMANT  [medium-conf]
 *
 *  -- AnimReplacementSet.cpp (2) --
 *   SMSG_MAP_OBJ_EVENTS                            0x00BB  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x1942                            0x1942  DOC      [low-conf]
 *
 *  -- AreaTrigger_C.cpp (4) --
 *   SMSG_UNKNOWN_0x109B                            0x109B  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x18E1                            0x18E1  DOC
 *   SMSG_UNKNOWN_0x1AAA                            0x1AAA  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x1E0A                            0x1E0A  DOC      [low-conf]
 *
 *  -- AreaTriggers.cpp (1) --
 *   SMSG_UNKNOWN_0x148F                            0x148F  DOC      [low-conf]
 *
 *  -- AuctionHouse.cpp (9) --
 *   SMSG_AUCTION_COMMAND_RESULT                    0x1002  ACTIVE
 *   SMSG_AUCTION_HELLO                             0x10A7  ACTIVE
 *   SMSG_AUCTION_BIDDER_NOTIFICATION               0x11C1  ACTIVE   server-binding=SMSG_AUCTION_WON_NOTIFICATION
 *   SMSG_RAID_INSTANCE_INFO                        0x16BF  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x18AE                            0x18AE  ACTIVE   server-binding=SMSG_AUCTION_BID_UPDATE_NOTIFICATION
 *   SMSG_AUCTION_OWNER_NOTIFICATION                0x1A8E  ACTIVE
 *   SMSG_UNKNOWN_0x1A9F                            0x1A9F  ACTIVE   server-binding=SMSG_AUCTION_OUTBID_NOTIFICATION
 *   SMSG_UNKNOWN_0x1AAB                            0x1AAB  DOC
 *   SMSG_INSTANCE_SAVE_CREATED                     0x1EAE  DORMANT  [low-conf]
 *
 *  -- AuthChallenge.cpp (6) --
 *   SMSG_BATTLE_PAY_DELIVERY_ENDED                 0x020B  DORMANT  [low-conf]
 *   SMSG_BATTLE_PAY_GET_PURCHASE_LIST_RESPONSE     0x023A  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x08BF                            0x08BF  DOC
 *   SMSG_UNKNOWN_0x143E                            0x143E  DOC
 *   SMSG_BATTLE_PAY_PURCHASE_UPDATE                0x14E2  DOC      [low-conf]
 *   SMSG_BATTLE_PAY_GET_PRODUCT_LIST_RESPONSE      0x1ABF  DORMANT  [low-conf]
 *
 *  -- BattlePetFrame.cpp (11) --
 *   SMSG_UNKNOWN_0x001E                            0x001E  DOC      [low-conf]
 *   SMSG_PET_BATTLE_QUEUE_STATUS                   0x00A6  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x022B                            0x022B  DOC      [low-conf]
 *   SMSG_PET_BATTLE_REQUEST_FAILED                 0x022F  DORMANT  [low-conf]
 *   SMSG_PET_BATTLE_FINISHED                       0x04BB  DORMANT  [medium-conf]
 *   SMSG_PET_BATTLE_FIRST_ROUND                    0x0613  DORMANT  [low-conf]
 *   SMSG_PET_BATTLE_INITIAL_UPDATE                 0x0E1E  DORMANT  [low-conf]
 *   SMSG_PET_BATTLE_QUEUE_PROPOSE_MATCH            0x1202  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x1A1A                            0x1A1A  DOC      [low-conf]
 *   SMSG_PET_BATTLE_FINAL_ROUND                    0x1C2F  DORMANT  [medium-conf]
 *   SMSG_PET_BATTLE_PVP_CHALLENGE                  0x1E0B  DORMANT  [name: binary-derived]
 *
 *  -- BattlefieldInfo.cpp (31) --
 *   SMSG_ARENA_UNIT_DESTROYED                      0x000F  DORMANT
 *   SMSG_BATTLEFIELD_MANAGER_EJECT_PENDING         0x003E  DORMANT
 *   SMSG_ARENA_PREP_OPPONENT_SPECIALIZATIONS       0x00BE  DORMANT
 *   SMSG_BATTLEGROUND_PLAYER_LEFT                  0x0206  DORMANT
 *   SMSG_UNKNOWN_0x023F                            0x023F  DOC
 *   SMSG_BATTLEFIELD_STATUS                        0x0433  ACTIVE
 *   SMSG_BATTLEGROUND_PLAYER_POSITIONS             0x060A  DORMANT
 *   SMSG_PVP_SEASON                                0x069B  DORMANT
 *   SMSG_PVP_OPTIONS_ENABLED                       0x080A  DORMANT
 *   SMSG_BATTLEFIELD_MGR_ENTERED                   0x081B  DORMANT
 *   SMSG_BATTLEFIELD_MGR_STATE_CHANGE              0x083A  DORMANT
 *   SMSG_PVP_REWARDS                               0x08AA  DORMANT
 *   SMSG_BATTLEFIELD_MANAGER_QUEUE_REQUEST_RESPONSE 0x08BE  DORMANT
 *   SMSG_UNKNOWN_0x0C33                            0x0C33  DOC
 *   SMSG_WARGAME_REQUEST_SENT                      0x0CAE  DOC
 *   SMSG_CONQUEST_FORMULA_CONSTANTS                0x0EAB  ACTIVE
 *   SMSG_BATTLEFIELD_RATED_INFO                    0x0EBA  ACTIVE
 *   SMSG_UNKNOWN_0x1027                            0x1027  DOC
 *   SMSG_BATTLEFIELD_STATUS_WAITFORGROUPS          0x10A6  DORMANT
 *   SMSG_BATTLEFIELD_STATUS_FAILED                 0x1140  ACTIVE
 *   SMSG_BATTLEFIELD_MANAGER_ENTRY_INVITE          0x1226  DORMANT
 *   SMSG_BATTLEFIELD_STATUS_QUEUED                 0x122E  ACTIVE
 *   SMSG_BATTLEFIELD_MANAGER_QUEUE_INVITE          0x142E  DORMANT
 *   SMSG_BATTLEFIELD_PORT_DENIED                   0x149A  DORMANT
 *   SMSG_BATTLEFIELD_LIST                          0x160E  DORMANT
 *   SMSG_UNKNOWN_0x18AF                            0x18AF  DOC
 *   SMSG_BATTLEFIELD_MGR_EJECTED                   0x18C2  DORMANT
 *   SMSG_BATTLEFIELD_STATUS_ACTIVE                 0x1AAF  ACTIVE
 *   SMSG_BATTLEGROUND_PLAYER_JOINED                0x1E2F  DORMANT
 *   SMSG_PVP_LOG_DATA                              0x1E8F  DORMANT
 *   SMSG_BATTLEFIELD_STATUS_NEEDCONFIRMATION       0x1EAF  ACTIVE
 *
 *  -- BattlenetLogin.cpp (2) --
 *   SMSG_BATTLE_PAY_GET_DISTRIBUTION_LIST_RESPONSE 0x043F  DORMANT
 *   SMSG_BATTLE_PAY_DISTRIBUTION_UPDATE            0x1E1B  DORMANT
 *
 *  -- BattlenetUI.cpp (2) --
 *   SMSG_UNKNOWN_0x140E                            0x140E  DOC
 *   SMSG_UNKNOWN_0x1C8B                            0x1C8B  DOC      [low-conf]
 *
 *  -- CGlueMgr.cpp (8) --
 *   SMSG_DISPLAY_PROMOTION                         0x00A3  DORMANT
 *   SMSG_KICK_REASON                               0x0A1E  DORMANT  [medium-conf]
 *   SMSG_CHAR_RENAME                               0x0CBF  DORMANT
 *   SMSG_UNKNOWN_0x10AF                            0x10AF  DOC
 *   SMSG_CHAR_CUSTOMIZE                            0x1432  ACTIVE
 *   SMSG_RANDOMIZE_CHAR_NAME                       0x169F  ACTIVE
 *   SMSG_SET_PLAYER_DECLINED_NAMES_RESULT          0x180E  DORMANT
 *   SMSG_REALM_SPLIT                               0x1A2E  ACTIVE
 *
 *  -- CUFProfiles.cpp (3) --
 *   SMSG_LOAD_CUF_PROFILES                         0x0E32  DORMANT
 *   SMSG_UNKNOWN_0x11C2                            0x11C2  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x168A                            0x168A  DOC      [low-conf]
 *
 *  -- Calendar.cpp (23) --
 *   SMSG_CALENDAR_EVENT_INVITE_REMOVED             0x00A2  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x0354                            0x0354  ACTIVE   [low-conf]  server-binding=SMSG_SET_QUEST_COMPLETED_BIT
 *   SMSG_UNKNOWN_0x0364                            0x0364  ACTIVE   [low-conf]  server-binding=SMSG_CLEAR_QUEST_COMPLETED_BITS
 *   SMSG_UNKNOWN_0x03EC                            0x03EC  ACTIVE   [low-conf]  server-binding=SMSG_CLEAR_QUEST_COMPLETED_BIT
 *   SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT        0x0412  DORMANT  [low-conf]
 *   SMSG_CALENDAR_EVENT_MODERATOR_STATUS           0x048F  ACTIVE   [low-conf]
 *   SMSG_CALENDAR_EVENT_REMOVED_ALERT              0x049B  ACTIVE   [low-conf]
 *   SMSG_CALENDAR_EVENT_UPDATED_ALERT              0x0A0E  ACTIVE   [medium-conf]
 *   SMSG_CALENDAR_EVENT_INVITE_ALERT               0x0A9F  ACTIVE   [medium-conf]
 *   SMSG_CALENDAR_RAID_LOCKOUT_ADDED               0x0CAB  ACTIVE   [medium-conf]
 *   SMSG_CALENDAR_RAID_LOCKOUT_UPDATED             0x0E1F  DORMANT  [medium-conf]
 *   SMSG_MOVE_CHARACTER_CHEAT                      0x100F  ACTIVE   [low-conf]  server-binding=SMSG_QUERY_TIME_RESPONSE
 *   SMSG_CALENDAR_EVENT_INVITE_NOTES               0x11C0  DORMANT  [medium-conf]
 *   SMSG_CALENDAR_RAID_LOCKOUT_REMOVED             0x11E0  ACTIVE   [low-conf]
 *   SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT       0x122B  ACTIVE   [low-conf]
 *   SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT         0x1286  DORMANT  [medium-conf]
 *   SMSG_COMPLAIN_RESULT                           0x128F  ACTIVE   [low-conf]
 *   SMSG_CALENDAR_SEND_EVENT                       0x12AE  ACTIVE
 *   SMSG_DAILY_QUESTS_RESET                        0x1366  DOC      [high-conf]
 *   SMSG_CALENDAR_COMMAND_RESULT                   0x142A  ACTIVE   [low-conf]
 *   SMSG_CALENDAR_EVENT_INVITE                     0x15C3  ACTIVE   [low-conf]
 *   SMSG_CALENDAR_SEND_CALENDAR                    0x1A0A  ACTIVE   [medium-conf]
 *   SMSG_CALENDAR_EVENT_INVITE_STATUS              0x1C9B  ACTIVE   [low-conf]
 *
 *  -- CalendarEvent.cpp (1) --
 *   SMSG_CALENDAR_EVENT_INITIAL_INVITE             0x16AE  ACTIVE   [medium-conf]
 *
 *  -- CharacterCreation.cpp (2) --
 *   SMSG_CHARACTER_UPGRADE_COMPLETE                0x083B  DORMANT  [low-conf]
 *   SMSG_CHARACTER_UPGRADE_STARTED                 0x188A  DORMANT  [low-conf]
 *
 *  -- CharacterModelBase.cpp (7) --
 *   SMSG_BATTLE_PAY_START_PURCHASE_RESPONSE        0x0612  DORMANT  [low-conf]
 *   SMSG_BATTLE_PAY_START_DISTRIBUTION_ASSIGN_TO_TARGET_RESPONSE 0x08AF  DOC      [low-conf]
 *   SMSG_BATTLE_PAY_ACK_FAILED                     0x103E  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x1162                            0x1162  DOC      [low-conf]
 *   SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN         0x121E  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x149E                            0x149E  DOC      [low-conf]
 *   SMSG_BATTLE_PAY_CONFIRM_PURCHASE               0x14E3  DOC      [low-conf]
 *
 *  -- ChatFrame.cpp (21) --
 *   SMSG_INSTANCE_RESET_FAILED                     0x0026  ACTIVE   [medium-conf]
 *   SMSG_TEXT_EMOTE                                0x002E  ACTIVE   [medium-conf]
 *   SMSG_SERVER_FIRST_ACHIEVEMENT                  0x028B  DORMANT
 *   SMSG_USERLIST_UPDATE                           0x063A  DORMANT  [medium-conf]
 *   SMSG_TITLE_EARNED                              0x068E  ACTIVE   [medium-conf]
 *   SMSG_DEFENSE_MESSAGE                           0x0A1F  DORMANT  [medium-conf]
 *   SMSG_CHAT_SERVER_RECONNECTED                   0x0A2E  DORMANT  [medium-conf]
 *   SMSG_USERLIST_REMOVE                           0x0AAB  DORMANT  [medium-conf]
 *   SMSG_UNKNOWN_0x0B68                            0x0B68  ACTIVE   [medium-conf]  server-binding=SMSG_GUILD_EVENT_MOTD
 *   SMSG_RAID_INSTANCE_MESSAGE                     0x0CAF  DORMANT  [medium-conf]
 *   SMSG_RESET_FAILED_NOTIFY                       0x10AE  ACTIVE   [medium-conf]
 *   SMSG_ZONE_UNDER_ATTACK                         0x10C2  DORMANT  [medium-conf]
 *   SMSG_VOICE_CHAT_STATUS                         0x10E2  DORMANT  [medium-conf]
 *   SMSG_TITLE_LOST                                0x12BF  ACTIVE   [medium-conf]
 *   SMSG_USERLIST_ADD                              0x1462  DORMANT  [medium-conf]
 *   SMSG_INSTANCE_RESET                            0x160F  ACTIVE   [medium-conf]
 *   SMSG_UPDATE_LAST_INSTANCE                      0x189B  DORMANT  [medium-conf]
 *   SMSG_EXPECTED_SPAM_RECORDS                     0x18C0  DORMANT  [medium-conf]
 *   SMSG_MESSAGECHAT                               0x1A9A  ACTIVE   [medium-conf]
 *   SMSG_DURABILITY_DAMAGE_DEATH                   0x1E3E  ACTIVE   [Wow.exe binary: empty route to retained DURABILITYDAMAGE_DEATH semantic]
 *   SMSG_LOG_XPGAIN                                0x1E9A  ACTIVE   [Wow.exe binary: sub_6F7E25 packed reader; sub_CE07DA combat-log semantic]
 *
 *  -- CheckExecutableSignature.cpp (1) --
 *   SMSG_UI_TIME                                   0x0027  ACTIVE   [low-conf]
 *
 *  -- Client.cpp (10) --
 *   SMSG_MESSAGE_BOX                               0x02AE  DORMANT
 *   SMSG_TRANSFER_PENDING                          0x061B  ACTIVE
 *   SMSG_NOTIFICATION                              0x0C2A  ACTIVE
 *   SMSG_TRANSFER_ABORTED                          0x0C8F  ACTIVE
 *   SMSG_PLAYED_TIME                               0x11E2  ACTIVE
 *   SMSG_WHOIS                                     0x12BA  DORMANT
 *   SMSG_UNKNOWN_0x188F                            0x188F  DOC
 *   SMSG_UNKNOWN_0x18BA                            0x18BA  ACTIVE   server-binding=SMSG_SUSPEND_TOKEN
 *   SMSG_LOGIN_VERIFY_WORLD                        0x1C0F  ACTIVE
 *   SMSG_NEW_WORLD                                 0x1C3B  ACTIVE
 *
 *  -- ComSatClient.cpp (1) --
 *   SMSG_VOICE_SESSION_LEAVE                       0x15C0  DORMANT  [low-conf]
 *
 *  -- ConsoleVar.cpp (1) --
 *   SMSG_SET_PROFICIENCY                           0x1440  ACTIVE   [low-conf]
 *
 *  -- ContainerFrame.cpp (1) --
 *   SMSG_OPEN_CONTAINER                            0x14BB  DORMANT  [medium-conf]
 *
 *  -- DBCacheInstances.cpp (12) --
 *   SMSG_QUEST_QUERY_RESPONSE                      0x0276  ACTIVE   [high-conf]
 *   SMSG_CREATURE_QUERY_RESPONSE                   0x048B  ACTIVE   [low-conf]
 *   SMSG_REALM_NAME_QUERY_RESPONSE                 0x063E  ACTIVE   [low-conf]
 *   SMSG_GAMEOBJECT_QUERY_RESPONSE                 0x06BF  ACTIVE   [low-conf]
 *   SMSG_PAGE_TEXT_QUERY_RESPONSE                  0x081E  ACTIVE   [high-conf]
 *   SMSG_PET_NAME_QUERY_RESPONSE                   0x0ABE  ACTIVE
 *   SMSG_INVALIDATE_PLAYER                         0x102E  DORMANT  [medium-conf]
 *   SMSG_PETITION_QUERY_RESPONSE                   0x1083  ACTIVE   [medium-conf]
 *   SMSG_NPC_TEXT_UPDATE                           0x140A  ACTIVE   [high-conf]
 *   SMSG_BATTLE_PET_QUERY_NAME_RESPONSE            0x1540  DORMANT
 *   SMSG_NAME_QUERY_RESPONSE                       0x169B  ACTIVE
 *   SMSG_GUILD_QUERY_RESPONSE                      0x1B79  ACTIVE
 *
 *  -- DressUpModelFrame.cpp (9) --
 *   SMSG_BLACK_MARKET_OPEN_RESULT                  0x00AE  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x083E                            0x083E  DOC      [low-conf]
 *   SMSG_BLACK_MARKET_OUTBID                       0x1040  DORMANT  [low-conf]
 *   SMSG_BLACK_MARKET_WON                          0x1060  DORMANT  [low-conf]
 *   SMSG_PETITION_SHOWLIST                         0x10A3  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x10C3                            0x10C3  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x128B                            0x128B  DOC      [low-conf]
 *   SMSG_BLACK_MARKET_BID_RESULT                           0x148A  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x14E0                            0x14E0  DOC      [low-conf]
 *
 *  -- DuelInfo.cpp (7) --
 *   SMSG_DUEL_OUTOFBOUNDS                          0x001A  ACTIVE   [high-conf]
 *   SMSG_DUEL_REQUESTED                            0x0022  ACTIVE   [high-conf]
 *   SMSG_UNKNOWN_0x083F                            0x083F  DOC
 *   SMSG_DUEL_WINNER                               0x10E1  ACTIVE   [high-conf]
 *   SMSG_DUEL_COUNTDOWN                            0x129F  ACTIVE   [high-conf]
 *   SMSG_DUEL_INBOUNDS                             0x163A  ACTIVE   [high-conf]
 *   SMSG_DUEL_COMPLETE                             0x1C0A  ACTIVE   [high-conf]
 *
 *  -- Effect_C.cpp (1) --
 *   SMSG_ACHIEVEMENT_EARNED                        0x080B  ACTIVE   [medium-conf]
 *
 *  -- EncounterJournal.cpp (1) --
 *   SMSG_UNKNOWN_0x182E                            0x182E  DOC      [unattributed]
 *
 *  -- EquipmentManager.cpp (10) --
 *   SMSG_EQUIPMENT_SET_ID                          0x0006  DORMANT
 *   SMSG_USE_EQUIPMENT_SET_RESULT                  0x0A2B  DORMANT
 *   SMSG_LF_GUILD_MEMBERSHIP_LIST_UPDATED          0x0AE0  DORMANT  [low-conf]
 *   SMSG_LF_GUILD_APPLICANT_LIST_UPDATED           0x0B71  DORMANT  [low-conf]
 *   SMSG_LF_GUILD_RECRUIT_LIST_UPDATED             0x0E68  DORMANT  [low-conf]
 *   SMSG_LF_GUILD_BROWSE_UPDATED                   0x0F69  DORMANT  [low-conf]
 *   SMSG_LOAD_EQUIPMENT_SET                        0x18E2  DORMANT
 *   SMSG_LF_GUILD_APPLICATIONS_LIST_CHANGED        0x1A70  DORMANT  [low-conf]
 *   SMSG_LF_GUILD_COMMAND_RESULT                   0x1A79  DORMANT  [low-conf]
 *   SMSG_LF_GUILD_POST_UPDATED                     0x1B71  DORMANT  [low-conf]
 *
 *  -- FriendList.cpp (1) --
 *   SMSG_WHO                                       0x161B  ACTIVE
 *
 *  -- GMTicketInfo.cpp (7) --
 *   SMSG_GM_TICKET_STATUS_UPDATE                   0x000B  ACTIVE   [medium-conf]
 *   SMSG_UNKNOWN_0x009B                            0x009B  DOC      [low-conf]
 *   SMSG_GM_TICKET_RESPONSE                        0x0207  DORMANT  [medium-conf]
 *   SMSG_GM_TICKET_UPDATE                          0x02A6  ACTIVE   [medium-conf]
 *   SMSG_GMTICKET_GETTICKET                        0x129B  ACTIVE   [medium-conf]
 *   SMSG_GM_TICKET_CASE_STATUS                     0x148E  ACTIVE   [low-conf]
 *   SMSG_GMTICKET_RESOLVE_RESPONSE                 0x1ABE  DORMANT  [medium-conf]
 *
 *  -- GameObject_C.cpp (4) --
 *   SMSG_GAMEOBJECT_CUSTOM_ANIM                    0x001F  ACTIVE   [high-conf]
 *   SMSG_GAME_OBJECT_ACTIVATE_ANIM_KIT             0x0C8A  DORMANT  [low-conf]
 *   SMSG_GAMEOBJECT_DESPAWN_ANIM                   0x108B  ACTIVE   [high-conf]
 *   SMSG_GAMEOBJECT_PAGETEXT                       0x14AF  ACTIVE   [high-conf]
 *
 *  -- GameUI.cpp (49) --
 *   SMSG_UNKNOWN_0x001B                            0x001B  DOC
 *   SMSG_WORLD_SERVER_INFO                         0x0082  ACTIVE   [medium-conf]
 *   SMSG_BREAK_TARGET                              0x021A  DORMANT
 *   SMSG_REFER_A_FRIEND_FAILURE                    0x021E  DORMANT  [medium-conf]
 *   SMSG_CORPSE_RECLAIM_DELAY                      0x022A  ACTIVE   [medium-conf]
 *   SMSG_CLEAR_BOSS_EMOTES                         0x062B  DORMANT  [low-conf]
 *   SMSG_OVERRIDE_LIGHT                            0x068A  DORMANT  [medium-conf]
 *   SMSG_WEATHER                                   0x06AB  ACTIVE   [medium-conf]
 *   SMSG_UNKNOWN_0x06BA                            0x06BA  DOC      [low-conf]
 *   SMSG_UPDATE_COMBO_POINTS                       0x082F  ACTIVE   [high-conf]
 *   SMSG_TALENTS_INVOLUNTARILY_RESET               0x088A  DORMANT  [low-conf]
 *   SMSG_AREA_TRIGGER_NO_CORPSE                    0x089E  ACTIVE   [high-conf]
 *   SMSG_GUILD_BANK_LIST                           0x0B79  ACTIVE   [high-conf]  byte-exact vs capture-000601 seq 1289646
 *   SMSG_MONEY_NOTIFY                              0x0C0F  DORMANT  [medium-conf]
 *   SMSG_ITEM_PUSH_RESULT                          0x0E0A  ACTIVE
 *   SMSG_CORPSE_TRANSPORT_QUERY                    0x0E0B  ACTIVE   server-binding=SMSG_CORPSE_QUERY_RESPONSE
 *   SMSG_START_MIRROR_TIMER                        0x0E12  ACTIVE  [high-conf]
 *   SMSG_UNKNOWN_0x0E2B                            0x0E2B  DOC
 *   SMSG_UNKNOWN_0x0E2E                            0x0E2E  DOC      [medium-conf]
 *   SMSG_GUILD_INVITE_CANCEL                       0x0FE1  DORMANT  [low-conf]
 *   SMSG_PVP_CREDIT                                0x100A  ACTIVE
 *   SMSG_UNKNOWN_0x101F                            0x101F  DOC      [medium-conf]
 *   SMSG_STOP_MIRROR_TIMER                         0x1026  ACTIVE  [high-conf]
 *   SMSG_PLAY_SOUND                                0x102A  ACTIVE
 *   SMSG_GM_PLAYER_INFO                            0x102B  DORMANT  [medium-conf]
 *   SMSG_CLEAR_TARGET                              0x1061  ACTIVE   [Wow.exe binary: sub_6D4AFB packed GUID reader; sub_85876E target-clear terminal]
 *   SMSG_PROPOSE_LEVEL_GRANT                       0x109A  DORMANT
 *   SMSG_UPDATE_INSTANCE_OWNERSHIP                 0x10E0  DORMANT  [medium-conf]
 *   SMSG_REFER_A_FRIEND_EXPIRED                    0x1143  DORMANT  [medium-conf]
 *   SMSG_DIFFERENT_INSTANCE_FROM_PARTY             0x120B  DORMANT
 *   SMSG_ENCOUNTER_END                             0x120F  DORMANT  [name: binary-derived]
 *   SMSG_UPDATE_WORLD_STATE                        0x121B  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x1223                            0x1223  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x122A                            0x122A  DOC      [low-conf]
 *   SMSG_SET_DUNGEON_DIFFICULTY                    0x1283  ACTIVE   [medium-conf]
 *   SMSG_PLAYER_DIFFICULTY_CHANGE                  0x128E  DORMANT  [medium-conf]
 *   SMSG_PLAY_OBJECT_SOUND                         0x1443  ACTIVE
 *   SMSG_PLAYER_SKINNED                            0x1463  DORMANT  [low-conf]
 *   SMSG_INIT_WORLD_STATES                         0x1560  ACTIVE   [medium-conf]
 *   SMSG_PAUSE_MIRROR_TIMER                        0x162E  DORMANT  [medium-conf]
 *   SMSG_DISPLAY_GAME_ERROR                        0x181F  DORMANT  [medium-conf]
 *   SMSG_UNKNOWN_0x1841                            0x1841  DOC
 *   SMSG_AREA_SPIRIT_HEALER_TIME                   0x188E  DORMANT
 *   SMSG_UNKNOWN_0x18E0                            0x18E0  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x19C3                            0x19C3  DOC      [medium-conf]
 *   SMSG_INVALID_PROMOTION_CODE                    0x1A0E  DORMANT  [medium-conf]
 *   SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE        0x1A3A  ACTIVE   [low-conf]
 *   SMSG_TOTEM_CREATED                             0x1C8F  DORMANT
 *   SMSG_ENCOUNTER_START                           0x1E8A  DORMANT  [medium-conf]  [name: binary-derived]
 *
 *  -- GossipInfo.cpp (10) --
 *   SMSG_PETITION_SHOW_SIGNATURES                  0x00AA  ACTIVE   [low-conf]
 *   SMSG_GOSSIP_MESSAGE                            0x0244  ACTIVE
 *   SMSG_GOSSIP_COMPLETE                           0x034E  ACTIVE   [high-conf]
 *   SMSG_PETITION_SIGN_RESULTS                     0x06AE  ACTIVE   [low-conf]
 *   SMSG_GUILD_MOVE_STARTING                       0x0AE1  DORMANT  [low-conf]  [name: binary-derived]
 *   SMSG_GUILD_MOVE_COMPLETE                       0x0BE8  DORMANT  [low-conf]  [name: binary-derived]
 *   SMSG_UNKNOWN_0x0EF9                            0x0EF9  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x1A61                            0x1A61  DOC      [low-conf]
 *   SMSG_GUILD_MEMBER_DAILY_RESET                  0x1BE8  DORMANT  [low-conf]
 *   SMSG_GUILD_EVENT_BANK_TAB_ADDED                0x1BE9  DORMANT  [low-conf]
 *
 *  -- GuildBankFrame.cpp (3) --
 *   SMSG_CALENDAR_SEND_NUM_PENDING                 0x0A3F  ACTIVE   [low-conf]
 *   SMSG_GUILD_BANK_LOG_QUERY_RESULT               0x0FF0  DORMANT  [low-conf]
 *   SMSG_CALENDAR_CLEAR_PENDING_ACTION             0x1E3A  ACTIVE   [low-conf]
 *
 *  -- GuildInfo.cpp (33) --
 *   SMSG_GUILD_EVENT_BANK_TAB_TEXT_CHANGED         0x0A70  DORMANT  [low-conf]
 *   SMSG_GUILD_PARTY_STATE_RESPONSE                0x0A78  ACTIVE   [low-conf]
 *   SMSG_GUILD_QUERY_RANKS_RESULT                  0x0A79  ACTIVE   [low-conf]
 *   SMSG_GUILD_NEWS_UPDATE                         0x0AE8  DORMANT  [medium-conf]
 *   SMSG_GUILD_CHALLENGE_UPDATED                   0x0AE9  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x0AF1                            0x0AF1  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x0B69                            0x0B69  ACTIVE   [low-conf]  server-binding=SMSG_GUILD_EVENT_PLAYER_JOINED
 *   SMSG_UNKNOWN_0x0B70                            0x0B70  ACTIVE   [low-conf]  server-binding=SMSG_GUILD_EVENT_PRESENCE_CHANGE
 *   SMSG_GUILD_BANK_MONEY_WITHDRAWN                0x0B78  ACTIVE   [low-conf]
 *   SMSG_GUILD_ROSTER                              0x0BE0  ACTIVE
 *   SMSG_GUILD_MEMBER_UPDATE_NOTE                  0x0BE1  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x0BE9                            0x0BE9  DOC      [low-conf]
 *   SMSG_GUILD_MEMBERS_FOR_RECIPE                  0x0BF0  DORMANT  [low-conf]
 *   SMSG_GUILD_EVENT_BANK_TAB_MODIFIED             0x0BF1  DORMANT  [low-conf]
 *   SMSG_GUILD_EVENT_PLAYER_LEFT                   0x0BF8  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x0E69                            0x0E69  ACTIVE   [low-conf]  server-binding=SMSG_GUILD_EVENT_NEW_LEADER
 *   SMSG_GUILD_RENAMED                             0x0E70  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x0E71                            0x0E71  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x0EF0                            0x0EF0  DOC      [low-conf]
 *   SMSG_GUILD_EVENT_BANK_MONEY_CHANGED            0x0F68  DORMANT  [low-conf]
 *   SMSG_GUILD_NEWS_DELETED                        0x0F70  DORMANT  [medium-conf]
 *   SMSG_GUILD_INVITE                              0x0F71  ACTIVE   [high-conf]  reader sub_69E959; byte-exact vs capture-000499 seq 777
 *   SMSG_GUILD_XP_GAIN                             0x0FE0  DORMANT  [low-conf]
 *   SMSG_GUILD_FLAGGED_FOR_RENAME                  0x0FE9  DORMANT  [low-conf]
 *   SMSG_GUILD_PERMISSIONS                         0x0FF9  ACTIVE   [low-conf]
 *   SMSG_GUILD_REWARDS_LIST                        0x1A69  DORMANT  [medium-conf]
 *   SMSG_GUILD_REPUTATION_WEEKLY_CAP               0x1A71  DORMANT  [low-conf]
 *   SMSG_GUILD_BANK_QUERY_TEXT_RESULT              0x1AE0  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x1AF0                            0x1AF0  DOC      [low-conf]
 *   SMSG_GUILD_EVENT_LOG                           0x1AF1  ACTIVE   [low-conf]
 *   SMSG_GUILD_CHALLENGE_COMPLETED                 0x1AF8  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x1B69                            0x1B69  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x1E68                            0x1E68  ACTIVE   [low-conf]  server-binding=SMSG_GUILD_EVENT_DISBANDED
 *
 *  -- IncomingResurrection.cpp (2) --
 *   SMSG_RESYNC_RUNES                              0x15E3  ACTIVE   [high-conf]
 *   SMSG_ADD_RUNE_POWER                            0x1860  ACTIVE   [high-conf]
 *
 *  -- ItemSocketInfo.cpp (1) --
 *   SMSG_SHOW_BANK                                 0x0007  ACTIVE   [low-conf]
 *
 *  -- KnowledgeBase.cpp (2) --
 *   SMSG_MOTD                                      0x183B  ACTIVE   [medium-conf]
 *   SMSG_UNKNOWN_0x1941                            0x1941  DOC      [low-conf]
 *
 *  -- LFGInfo.cpp (16) --
 *   SMSG_UNKNOWN_0x003B                            0x003B  DOC      [low-conf]
 *   SMSG_LFG_DISABLED                              0x008E  DORMANT  [low-conf]
 *   SMSG_LFG_TELEPORT_DENIED                       0x063B  ACTIVE   [low-conf]
 *   SMSG_LFG_SLOT_INVALID                          0x0C12  DORMANT  [low-conf]
 *   SMSG_OPEN_LFG_DUNGEON_FINDER                   0x0E8A  DORMANT  [low-conf]
 *   SMSG_LFG_QUEUE_STATUS                          0x1006  ACTIVE   [high-conf]
 *   SMSG_UNKNOWN_0x1041                            0x1041  DOC      [low-conf]
 *   SMSG_LFG_UPDATE_SEARCH                         0x1161  ACTIVE
 *   SMSG_LFG_PLAYER_REWARD                         0x121A  ACTIVE   [low-conf]
 *   SMSG_LFG_PARTY_INFO                            0x168E  ACTIVE   [low-conf]
 *   SMSG_LFG_BOOT_PROPOSAL_UPDATE                  0x183A  ACTIVE   [low-conf]  server-binding=SMSG_LFG_BOOT_PLAYER
 *   SMSG_LFG_PLAYER_INFO                           0x1861  ACTIVE   [low-conf]
 *   SMSG_LOOT_UPDATE                               0x1863  DORMANT  [low-conf]
 *   SMSG_LFG_JOIN_RESULT                           0x18E3  ACTIVE   [low-conf]
 *   SMSG_ROLE_CHOSEN                               0x1A1F  ACTIVE   [low-conf]
 *   SMSG_LFG_OFFER_CONTINUE                        0x1EAB  ACTIVE   [low-conf]
 *
 *  -- LoadingScreen.cpp (1) --
 *   SMSG_CUSTOM_LOAD_SCREEN                        0x1CAF  DORMANT  [low-conf]
 *
 *  -- Log.cpp (2) --
 *   SMSG_UNKNOWN_0x020A                            0x020A  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x02BA                            0x02BA  DOC      [low-conf]
 *
 *  -- LootFrame.cpp (11) --
 *   SMSG_INSPECT_RATED_BG_STATS                    0x041F  DORMANT  [low-conf]
 *   SMSG_AE_LOOT_TARGETS                           0x0C32  DORMANT
 *   SMSG_LOOT_REMOVED                              0x0C3E  ACTIVE   [medium-conf]
 *   SMSG_PET_UPDATE_COMBO_POINTS                   0x1206  DORMANT  [low-conf]
 *   SMSG_LOOT_CONTENTS                             0x121F  DOC       [name: reference-derived; semantic: binary+UI]
 *   SMSG_LOOT_RELEASE_RESPONSE                     0x123F  ACTIVE   [medium-conf]
 *   SMSG_LOOT_RESPONSE                             0x128A  ACTIVE
 *   SMSG_LOOT_MONEY_NOTIFY                         0x14C0  ACTIVE   [medium-conf]
 *   SMSG_LOOT_CLEAR_MONEY                          0x1632  ACTIVE   [medium-conf]
 *   SMSG_PET_MODE                                  0x163F  ACTIVE   [low-conf]
 *   SMSG_INSPECT_HONOR_STATS                       0x1A1E  DORMANT  [low-conf]
 *
 *  -- LootHistory.cpp (3) --
 *   SMSG_UNKNOWN_0x1A3F                            0x1A3F  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x1CBA                            0x1CBA  DOC      [low-conf]
 *   SMSG_COMMENTATOR_MAP_INFO                      0x1CBE  DORMANT  [low-conf]
 *
 *  -- LootRoll.cpp (7) --
 *   SMSG_LOOT_MASTER_LIST                          0x02BF  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_080F                              0x080F  DORMANT  [direct reader: loot-roll batch; exact name unresolved]
 *   SMSG_LOOT_ROLL_WON                             0x0A3A  ACTIVE
 *   SMSG_LOOT_START_ROLL                           0x0EAA  ACTIVE
 *   SMSG_LOOT_ALL_PASSED                           0x0EBB  ACTIVE
 *   SMSG_LOOT_ROLLS_COMPLETE                       0x101B  DORMANT  [low-conf]
 *   SMSG_LOOT_ROLL                                 0x1840  ACTIVE   [medium-conf]
 *
 *  -- LossOfControlUI.cpp (8) --
 *   SMSG_UNKNOWN_0x021F                            0x021F  DOC      [low-conf]
 *   SMSG_WEEKLY_RESET_CURRENCY                     0x023E  ACTIVE   [low-conf]  server-binding=SMSG_WEEKLY_RESET_CURRENCIES
 *   SMSG_UNKNOWN_0x049A                            0x049A  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x0C13                            0x0C13  DOC      [medium-conf]
 *   SMSG_SET_CURRENCY_WEEK_LIMIT                   0x0E2A  ACTIVE   [low-conf]
 *   SMSG_UPDATE_CURRENCY                           0x129E  ACTIVE   [low-conf]
 *   SMSG_SETUP_CURRENCY                            0x1A8B  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x1E13                            0x1E13  DOC      [low-conf]
 *
 *  -- MailInfo.cpp (4) --
 *   SMSG_MAIL_QUERY_NEXT_TIME_RESULT               0x089B  ACTIVE
 *   SMSG_RECEIVED_MAIL                             0x182B  ACTIVE
 *   SMSG_SEND_MAIL_RESULT                          0x1A9B  ACTIVE
 *   SMSG_MAIL_LIST_RESULT                          0x1C0B  ACTIVE
 *
 *  -- Missile_C.cpp (1) --
 *   SMSG_MISSILE_CANCEL                            0x1203  DOC
 *
 *  -- Movie.cpp (1) --
 *   SMSG_STREAMING_MOVIE                           0x1843  DORMANT
 *
 *  -- NetClient.cpp (1) --
 *   SMSG_UNKNOWN_0x0EAF                            0x0EAF  DOC      [medium-conf]
 *
 *  -- ObjectMgrClient.cpp (2) --
 *   SMSG_UNKNOWN_0x0C9A                            0x0C9A  DOC
 *   SMSG_DESTROY_OBJECT                            0x14C2  ACTIVE
 *
 *  -- PartyInfo.cpp (7) --
 *   SMSG_RAID_READY_CHECK_CONFIRM                  0x02AF  ACTIVE   [low-conf]
 *   SMSG_GROUP_INVITE                              0x0A8F  ACTIVE   [medium-conf]
 *   SMSG_GROUP_LIST                                0x0CBB  ACTIVE
 *   SMSG_GROUP_ROLE_POLL_INFORM                    0x1007  ACTIVE   [medium-conf]
 *   SMSG_RAID_READY_CHECK_COMPLETED                0x15C2  ACTIVE   [medium-conf]
 *   SMSG_RAID_READY_CHECK                          0x1C8E  ACTIVE   [medium-conf]
 *   SMSG_GROUP_SET_ROLE                            0x1E1F  DORMANT  [medium-conf]
 *
 *  -- PetJournalInfo.cpp (15) --
 *   SMSG_UNKNOWN_0x002F                            0x002F  DOC      [medium-conf]
 *   SMSG_BATTLE_PET_JOURNAL_LOCK_DENINED           0x0203  DORMANT  [low-conf]
 *   SMSG_BATTLE_PET_PET_UPDATES                    0x041A  DORMANT
 *   SMSG_UNKNOWN_0x068B                            0x068B  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x06BE                            0x06BE  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x0A1A                            0x0A1A  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x0E9E                            0x0E9E  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x1003                            0x1003  DOC      [medium-conf]
 *   SMSG_ENABLE_BARBER_SHOP                        0x1222  ACTIVE   [high-conf]
 *   SMSG_BATTLE_PET_JOURNAL                        0x1542  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x1612                            0x1612  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x162F                            0x162F  DOC      [low-conf]
 *   SMSG_BATTLE_PET_SLOT_UPDATE                    0x16AF  DORMANT  [medium-conf]
 *   SMSG_BATTLE_PET_DELETED                        0x18AB  DORMANT  [medium-conf]
 *   SMSG_BATTLE_PET_JOURNAL_LOCK_ACQUIRED          0x1A0F  DORMANT  [low-conf]
 *
 *  -- PetitionInfo.cpp (3) --
 *   SMSG_TABARD_VENDOR_ACTIVATE                    0x0A3E  ACTIVE   [low-conf]
 *   SMSG_TURN_IN_PETITION_RESULTS                  0x0E13  ACTIVE   [low-conf]
 *   SMSG_OFFER_PETITION_ERROR                      0x161E  DORMANT  [low-conf]
 *
 *  -- Player_C.cpp (140) --
 *   SMSG_SUMMON_CANCEL                             0x000A  DORMANT  [medium-conf]
 *   SMSG_VOICE_SESSION_ROSTER_UPDATE               0x000E  DORMANT  [medium-conf]
 *   SMSG_UNKNOWN_0x0050                            0x0050  DOC
 *   SMSG_SOR_START_EXPERIENCE_INCOMPLETE           0x0083  DORMANT  [medium-conf]
 *   SMSG_RAID_MARKERS_CHANGED                      0x008A  DORMANT  [medium-conf]
 *   SMSG_VOID_STORAGE_CONTENTS                     0x008B  DORMANT  [low-conf]
 *   SMSG_INSTANCE_LOCK_WARNING_QUERY               0x00A7  DOC      [medium-conf]
 *   SMSG_SPELL_EXECUTE_LOG                         0x00D8  ACTIVE   [medium-conf]
 *   SMSG_UNKNOWN_0x0170                            0x0170  DOC
 *   SMSG_CATEGORY_COOLDOWN                         0x01DB  ACTIVE   [high-conf]
 *   SMSG_FISH_ESCAPED                              0x0227  ACTIVE
 *   SMSG_QUESTUPDATE_ADD_PVP_KILL                  0x0256  DORMANT
 *   SMSG_QUESTGIVER_REQUEST_ITEMS                  0x0277  ACTIVE
 *   SMSG_QUESTGIVER_QUEST_INVALID                  0x027D  ACTIVE   [high-conf]
 *   SMSG_PET_LEARNED_SPELL                         0x0282  ACTIVE   [medium-conf]
 *   SMSG_PETITION_ALREADY_SIGNED                   0x0286  DORMANT  [medium-conf]
 *   SMSG_GROUPACTION_THROTTLED                     0x0287  DORMANT  [medium-conf]
 *   SMSG_PET_NAME_INVALID                          0x028E  DORMANT
 *   SMSG_AVAILABLE_VOICE_CHANNEL                   0x029A  DORMANT  [medium-conf]
 *   SMSG_RESPEC_WIPE_CONFIRM                       0x02AB  ACTIVE
 *   SMSG_QUESTGIVER_QUEST_LIST                     0x02D4  ACTIVE   [high-conf]
 *   SMSG_UNKNOWN_0x02EF                            0x02EF  DOC
 *   SMSG_QUESTGIVER_QUEST_COMPLETE                 0x0346  ACTIVE
 *   SMSG_QUEST_NPC_QUERY_RESPONSE                  0x036D  ACTIVE   [medium-conf]
 *   SMSG_UNKNOWN_0x040F                            0x040F  DOC
 *   SMSG_UNKNOWN_0x041E                            0x041E  DOC      [medium-conf]
 *   SMSG_REQUEST_CEMETERY_LIST_RESPONSE            0x042A  ACTIVE   [medium-conf]
 *   SMSG_TRAINER_BUY_FAILED                        0x042E  ACTIVE   [medium-conf]
 *   SMSG_INITIAL_SPELLS                            0x045A  ACTIVE
 *   SMSG_SELL_ITEM                                 0x048E  ACTIVE
 *   SMSG_ITEM_PURCHASE_REFUND_RESULT               0x049E  DORMANT
 *   SMSG_UNKNOWN_0x04AA                            0x04AA  DOC      [medium-conf]
 *   SMSG_ARENA_ERROR                               0x04BA  DORMANT  [medium-conf]
 *   SMSG_VOICE_PARENTAL_CONTROLS                   0x04BF  DORMANT  [medium-conf]
 *   SMSG_SPELLDAMAGESHIELD                         0x05F3  ACTIVE
 *   SMSG_CHAT_PLAYER_AMBIGUOUS                     0x061A  ACTIVE   [medium-conf]
 *   SMSG_PLAY_TIME_WARNING                         0x062A  DORMANT  [medium-conf]
 *   SMSG_DISMOUNTRESULT                            0x062F  DORMANT  [medium-conf]
 *   SMSG_QUEST_POI_QUERY_RESPONSE                  0x067F  ACTIVE   [medium-conf]
 *   SMSG_QUESTGIVER_STATUS_MULTIPLE                0x06CE  ACTIVE   [high-conf]
 *   SMSG_QUESTUPDATE_FAILEDTIMER                   0x06FF  ACTIVE   [high-conf]
 *   SMSG_QUEST_PUSH_RESULT                         0x074D  ACTIVE   [medium-conf]
 *   SMSG_QUESTGIVER_OFFER_REWARD                   0x074F  ACTIVE
 *   SMSG_QUESTUPDATE_COMPLETE                      0x0776  ACTIVE
 *   SMSG_UNKNOWN_0x07C5                            0x07C5  DOC      [medium-conf]
 *   SMSG_QUESTUPDATE_FAILED                        0x07DD  DORMANT
 *   SMSG_UNKNOWN_0x07F5                            0x07F5  DOC
 *   SMSG_QUESTLOG_FULL                             0x07FD  ACTIVE   [high-conf]
 *   SMSG_ACTION_BUTTONS                            0x081A  ACTIVE   [medium-conf]
 *   SMSG_SUMMON_REQUEST                            0x081F  DORMANT
 *   SMSG_PETITION_RENAME_RESULT                    0x082A  DORMANT
 *   SMSG_PLAYERBOUND                               0x088E  ACTIVE
 *   SMSG_UNKNOWN_0x089F                            0x089F  ACTIVE   [medium-conf]  server-binding=SMSG_SAVE_GUILD_EMBLEM
 *   SMSG_UNKNOWN_0x08FB                            0x08FB  DOC
 *   SMSG_SPELLINSTAKILLLOG                         0x09F8  ACTIVE
 *   SMSG_SPELLHEALLOG                              0x09FB  ACTIVE
 *   SMSG_UNKNOWN_0x0A2F                            0x0A2F  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x0A3B                            0x0A3B  DOC
 *   SMSG_GUILD_RANKS_UPDATE                        0x0A60  ACTIVE   [medium-conf]
 *   SMSG_CHAT_NOT_IN_PARTY                         0x0A8A  DORMANT  [medium-conf]
 *   SMSG_UNKNOWN_0x0A8B                            0x0A8B  ACTIVE   [medium-conf]  server-binding=SMSG_INITIAL_SETUP
 *   SMSG_PARTY_MEMBER_STATS                        0x0A9A  ACTIVE
 *   SMSG_GUILD_XP                                  0x0AF0  DORMANT  [medium-conf]
 *   SMSG_NEW_WORLD_ABORT                           0x0C1B  DOC
 *   SMSG_INVENTORY_CHANGE_FAILURE                  0x0C1E  ACTIVE   [medium-conf]
 *   SMSG_CAMERA_SHAKE                              0x0C3A  DORMANT
 *   SMSG_UNKNOWN_0x0C3B                            0x0C3B  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x0C8E                            0x0C8E  DOC
 *   SMSG_UNKNOWN_0x0D51                            0x0D51  DOC      [medium-conf]
 *   SMSG_SPELLENERGIZELOG                          0x0D79  ACTIVE   [medium-conf]
 *   SMSG_SPELLDISPELLOG                            0x0DF9  ACTIVE
 *   SMSG_MOUNTRESULT                               0x0E0F  DORMANT  [medium-conf]
 *   SMSG_ITEM_EXPIRE_PURCHASE_REFUND               0x0E33  DORMANT
 *   SMSG_BINDPOINTUPDATE                           0x0E3B  ACTIVE   [medium-conf]
 *   SMSG_CANCEL_COMBAT                             0x0E8B  ACTIVE   [high-conf]  direct empty reader and local-player combat-cancel terminal
 *   SMSG_FORCED_DEATH_UPDATE                       0x0E8F  DORMANT  [medium-conf]
 *   SMSG_GUILD_MEMBER_RECIPES                      0x0EE1  DORMANT
 *   SMSG_GUILD_COMMAND_RESULT                      0x0EF1  ACTIVE   [high-conf]  direct command/result/name reader and guild-error consumer
 *   SMSG_GUILD_RECIPES                             0x0FF1  DORMANT
 *   SMSG_BUY_ITEM                                  0x101A  ACTIVE
 *   SMSG_UNKNOWN_0x1023                            0x1023  DOC
 *   SMSG_RESURRECT_REQUEST                         0x1062  ACTIVE
 *   SMSG_DEATH_RELEASE_LOC                         0x1063  ACTIVE   [medium-conf]
 *   SMSG_CHAT_PLAYER_NOT_FOUND                     0x1082  ACTIVE   [medium-conf]
 *   SMSG_UNKNOWN_0x108A                            0x108A  DOC      [medium-conf]
 *   SMSG_ITEM_ENCHANT_TIME_UPDATE                  0x10A2  ACTIVE
 *   SMSG_UNKNOWN_0x10BB                            0x10BB  DOC      [medium-conf]
 *   SMSG_FISH_NOT_HOOKED                           0x10BE  ACTIVE
 *   SMSG_UNKNOWN_0x10C1                            0x10C1  DOC
 *   SMSG_SEND_UNLEARN_SPELLS                       0x10F1  ACTIVE
 *   SMSG_CANCEL_SCENE                              0x120E  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x122F                            0x122F  DOC      [medium-conf]
 *   SMSG_QUESTGIVER_STATUS                         0x1275  ACTIVE
 *   SMSG_UNKNOWN_0x1282                            0x1282  DOC      [medium-conf]
 *   SMSG_BINDER_CONFIRM                            0x1287  ACTIVE   [medium-conf]
 *   SMSG_LEARNED_SPELL                             0x129A  ACTIVE   [high-conf]
 *   SMSG_SOCKET_GEMS                               0x12A6  DORMANT
 *   SMSG_QUESTGIVER_QUEST_FAILED                   0x12DE  ACTIVE   [high-conf]
 *   SMSG_QUESTGIVER_QUEST_DETAILS                  0x134C  ACTIVE   [high-conf]
 *   SMSG_QUEST_CONFIRM_ACCEPT                      0x13C7  ACTIVE
 *   SMSG_RANDOM_ROLL                               0x141A  ACTIVE
 *   SMSG_UNKNOWN_0x1441                            0x1441  DOC
 *   SMSG_SPELLNONMELEEDAMAGELOG                    0x1450  DORMANT  [medium-conf]
 *   SMSG_VOID_STORAGE_TRANSFER_CHANGES             0x14BA  DORMANT  [medium-conf]
 *   SMSG_REMOVED_SPELL                             0x14C3  ACTIVE   [high-conf]
 *   SMSG_BUY_FAILED                                0x1563  ACTIVE   [medium-conf]
 *   SMSG_SPELLLOGMISS                              0x1570  ACTIVE
 *   SMSG_SHOW_NEURTRAL_PLAYER_FACTION_SELECT_UI    0x15E0  DORMANT  [medium-conf]
 *   SMSG_UNKNOWN_0x161A                            0x161A  DOC      [medium-conf]
 *   SMSG_GMTICKET_SYSTEMSTATUS                     0x163B  ACTIVE   [medium-conf]
 *   SMSG_QUESTUPDATE_ADD_KILL                      0x1645  ACTIVE
 *   SMSG_MINIMAP_PING                              0x168F  ACTIVE
 *   SMSG_FEATURE_SYSTEM_STATUS                     0x16BB  ACTIVE
 *   SMSG_INSPECT_RESULTS                           0x1842  ACTIVE
 *   SMSG_SPELLINTERRUPTLOG                         0x1851  ACTIVE
 *   SMSG_GODMODE                                   0x1862  DORMANT  [medium-conf]
 *   SMSG_EXPLORATION_EXPERIENCE                    0x189A  ACTIVE   [Wow.exe binary: sub_6BB9C1 reads area ID then experience; sub_7B1384 retained semantic]
 *   SMSG_TRAINER_LIST                              0x189F  DORMANT  [medium-conf]
 *   SMSG_REPORT_PVP_AFK_RESULT                     0x18BE  DORMANT  [medium-conf]
 *   SMSG_GROUP_SET_LEADER                          0x18BF  ACTIVE   [medium-conf]
 *   SMSG_ITEM_TIME_UPDATE                          0x18C1  ACTIVE
 *   SMSG_PETGODMODE                                0x1940  DORMANT  [medium-conf]
 *   SMSG_SUPERCEDED_SPELL                          0x1943  ACTIVE   [high-conf]
 *   SMSG_LEVELUP_INFO                              0x1961  ACTIVE   [Wow.exe binary: sub_6BAC39 reads 13 uint32 fields; sub_7B12E9 maps them to PLAYER_LEVEL_UP]
 *   SMSG_UNKNOWN_0x19C2                            0x19C2  DOC      [medium-conf]
 *   SMSG_CONVERT_RUNE                              0x1A1B  ACTIVE   [high-conf]
 *   SMSG_UNKNOWN_0x1A2B                            0x1A2B  DOC      [medium-conf]
 *   SMSG_CHAT_RESTRICTED                           0x1A3B  ACTIVE   [medium-conf]
 *   SMSG_TIME_SYNC_REQ                             0x1A8F  ACTIVE
 *   SMSG_LIST_INVENTORY                            0x1AAE  ACTIVE   [medium-conf]
 *   SMSG_GUILD_DECLINE                             0x1AF9  ACTIVE   [medium-conf]
 *   SMSG_TRIGGER_MOVIE                             0x1C2E  DORMANT  [medium-conf]
 *   SMSG_PLAY_SCENE                                0x1C3A  DORMANT  [low-conf]
 *   SMSG_SET_ITEM_PURCHASE_DATA                    0x1C9A  DORMANT
 *   SMSG_VOID_TRANSFER_RESULT                      0x1C9E  DORMANT  [medium-conf]
 *   SMSG_PET_REMOVED_SPELL                         0x1CAE  ACTIVE
 *   SMSG_SHOWTAXINODES                             0x1E1A  ACTIVE   [medium-conf]
 *   SMSG_BATTLEGROUND_INFO_THROTTLED               0x1E1E  DORMANT  [medium-conf]
 *   SMSG_CROSSED_INEBRIATION_THRESHOLD             0x1E9E  ACTIVE
 *   SMSG_SPIRIT_HEALER_CONFIRM                     0x1EAA  ACTIVE   [medium-conf]
 *
 *  -- QuestCache.cpp (1) --
 *   SMSG_SET_PHASE_SHIFT                           0x02A2  DORMANT  [low-conf]
 *
 *  -- QuestLog.cpp (1) --
 *   SMSG_UNKNOWN_0x1C9F                            0x1C9F  DOC      [low-conf]
 *
 *  -- QuestTextParserWOW.cpp (1) --
 *   SMSG_UNKNOWN_0x08BA                            0x08BA  DOC      [low-conf]
 *
 *  -- RaidMarkers.cpp (3) --
 *   SMSG_RAID_TARGET_UPDATE_ALL                    0x0283  ACTIVE
 *   SMSG_BARBER_SHOP_RESULT                        0x0C3F  ACTIVE   [high-conf]
 *   SMSG_RAID_TARGET_UPDATE_SINGLE                 0x160B  ACTIVE
 *
 *  -- Reforge.cpp (1) --
 *   SMSG_UNKNOWN_0x1E33                            0x1E33  DOC      [low-conf]
 *
 *  -- ReputationInfo.cpp (7) --
 *   SMSG_SET_FORCED_REACTIONS                      0x068F  ACTIVE
 *   SMSG_UNKNOWN_0x0A0B                            0x0A0B  DOC
 *   SMSG_INITIALIZE_FACTIONS                       0x0AAA  ACTIVE
 *   SMSG_SET_FACTION_ATWAR                         0x0C9B  DORMANT
 *   SMSG_SET_FACTION_STANDING                      0x10AA  ACTIVE
 *   SMSG_UNKNOWN_0x1C2B                            0x1C2B  DOC
 *   SMSG_SET_FACTION_VISIBLE                       0x1E8E  ACTIVE
 *
 *  -- ResearchFrame.cpp (9) --
 *   SMSG_UNKNOWN_0x069A                            0x069A  DOC      [low-conf]
 *   SMSG_PET_ACTION_FEEDBACK                       0x080E  ACTIVE   [low-conf]
 *   SMSG_RESEARCH_SETUP_HISTORY                    0x08AB  DORMANT
 *   SMSG_RESEARCH_COMPLETE                         0x0C0E  DORMANT
 *   SMSG_UNKNOWN_0x0C59                            0x0C59  DOC      [low-conf]
 *   SMSG_ARCHAEOLOGY_SURVERY_CAST                  0x1160  DORMANT
 *   SMSG_PET_GUIDS                                 0x1227  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x123A                            0x123A  DOC      [low-conf]
 *   SMSG_PET_STABLE_LIST                           0x1613  ACTIVE   [low-conf]
 *
 *  -- SComp.cpp (1) --
 *   SMSG_DB_REPLY                                  0x103B  ACTIVE   [low-conf]
 *
 *  -- SI3.cpp (3) --
 *   SMSG_LFG_UPDATE_STATUS                         0x0C2E  ACTIVE   [medium-conf]  [name: binary-derived]
 *   SMSG_LFG_ROLE_CHECK_UPDATE                     0x12BB  ACTIVE   [medium-conf]
 *   SMSG_LFG_PROPOSAL_UPDATE                       0x1E3B  ACTIVE   [medium-conf]
 *
 *  -- SI3ZoneSounds.cpp (1) --
 *   SMSG_PLAY_MUSIC                                0x0023  ACTIVE   [medium-conf]
 *
 *  -- SceneObject_C.cpp (1) --
 *   SMSG_UNKNOWN_0x1962                            0x1962  DOC      [medium-conf]
 *
 *  -- SpecializationInfo.cpp (2) --
 *   SMSG_UNKNOWN_0x060F                            0x060F  DOC      [low-conf]
 *   SMSG_TALENT_UPDATE                             0x0A9B  DORMANT  [low-conf]
 *
 *  -- SpellBookFrame.cpp (1) --
 *   SMSG_PET_SPELLS                                0x095A  ACTIVE   [medium-conf]
 *
 *  -- SpellVisuals.cpp (2) --
 *   SMSG_UNKNOWN_0x1086                            0x1086  DOC      [medium-conf]
 *   SMSG_UNKNOWN_0x1412                            0x1412  DOC      [low-conf]
 *
 *  -- Spell_C.cpp (32) --
 *   SMSG_UNKNOWN_0x0002                            0x0002  DOC
 *   SMSG_UNKNOWN_0x00F2                            0x00F2  DOC
 *   SMSG_UNKNOWN_0x00F3                            0x00F3  DOC
 *   SMSG_UNKNOWN_0x00F9                            0x00F9  DOC
 *   SMSG_RESUME_CAST_BAR                           0x01D2  DORMANT
 *   SMSG_FEIGN_DEATH_RESISTED                      0x029E  DORMANT
 *   SMSG_SPELL_FAILED_OTHER                        0x040B  DORMANT
 *   SMSG_PET_TAME_FAILURE                          0x040E  DORMANT
 *   SMSG_COOLDOWN_CHEAT                            0x0432  DORMANT
 *   SMSG_SPELL_FAILURE                             0x04AF  DORMANT
 *   SMSG_SPELL_DELAYED                             0x087A  DORMANT
 *   SMSG_SET_PCT_SPELL_MODIFIER                    0x09D3  DORMANT
 *   SMSG_SPELL_GO                                  0x09D8  ACTIVE
 *   SMSG_UNKNOWN_0x0C5B                            0x0C5B  DOC
 *   SMSG_SPELL_UPDATE_CHAIN_TARGETS                0x0D52  DORMANT
 *   SMSG_GAMEOBJECT_RESET_STATE                    0x100E  DORMANT
 *   SMSG_SPELL_START                               0x107A  ACTIVE
 *   SMSG_SET_FLAT_SPELL_MODIFIER                   0x10F2  DORMANT
 *   SMSG_CHANNEL_START                             0x10F9  ACTIVE  [high-conf]
 *   SMSG_COOLDOWN_EVENT                            0x1163  ACTIVE
 *   SMSG_UNKNOWN_0x117A                            0x117A  DOC
 *   SMSG_CHANNEL_UPDATE                            0x11D9  ACTIVE  [high-conf]
 *   SMSG_PLAY_SPELL_VISUAL_KIT                     0x11E3  DORMANT
 *   SMSG_NOTIFY_MISSILE_TRAJECTORY_COLLISION       0x120A  DORMANT
 *   SMSG_CAST_FAILED                               0x143A  ACTIVE
 *   SMSG_CLEAR_COOLDOWNS                           0x1458  ACTIVE
 *   SMSG_PET_CAST_FAILED                           0x149B  ACTIVE
 *   SMSG_CLEAR_COOLDOWN                            0x162A  DOC
 *   SMSG_ON_CANCEL_EXPECTED_RIDE_VEHICLE_AURA      0x1A2A  DORMANT
 *   SMSG_NOTIFY_DEST_LOC_SPELL_CAST                0x1E0E  DORMANT
 *   SMSG_MODIFY_COOLDOWN                           0x1E2E  DORMANT
 *   SMSG_UNKNOWN_0x1EBB                            0x1EBB  DOC
 *
 *  -- TaxiMapFrame.cpp (3) --
 *   SMSG_ACTIVATETAXIREPLY                         0x02A7  ACTIVE   [low-conf]
 *   SMSG_NEW_TAXI_PATH                             0x141B  ACTIVE   [low-conf]
 *   SMSG_TAXINODE_STATUS                           0x169E  ACTIVE
 *
 *  -- TradeFrame.cpp (3) --
 *   SMSG_STABLE_RESULT                             0x14BE  ACTIVE   [low-conf]
 *   SMSG_TRADE_STATUS_EXTENDED                     0x181E  ACTIVE   [medium-conf]
 *   SMSG_TRADE_STATUS                              0x1963  ACTIVE
 *
 *  -- TradeSkillFrame.cpp (1) --
 *   SMSG_UNKNOWN_0x00BA                            0x00BA  DOC      [medium-conf]
 *
 *  -- UIBindings.cpp (4) --
 *   SMSG_UNKNOWN_0x00AB                            0x00AB  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x0E1A                            0x0E1A  DOC      [low-conf]
 *   SMSG_START_TIMER                               0x0E3F  ACTIVE   [low-conf]
 *   SMSG_UNKNOWN_0x101E                            0x101E  DOC      [low-conf]
 *
 *  -- UnitAnim_C.cpp (1) --
 *   SMSG_SET_PLAY_HOVER_ANIM                       0x069F  DORMANT
 *
 *  -- UnitCombatLog_C.cpp (6) --
 *   SMSG_PARTYKILLLOG                              0x048A  ACTIVE   [Wow.exe binary: sub_6F2FE4 decodes two GUIDs, slot B the killer and slot A the victim; the unnamed handler at .text:00841B83 emits COMBAT_LOG_EVENT PARTY_KILL]
 *   SMSG_DISPEL_FAILED                             0x085B  DORMANT
 *   SMSG_SPELL_PERIODIC_AURA_LOG                   0x0CF2  ACTIVE
 *   SMSG_ENCHANTMENTLOG                            0x12A3  DORMANT
 *   SMSG_PROCRESIST                                0x12BE  DORMANT
 *   SMSG_DESTRUCTIBLE_BUILDING_DAMAGE              0x14BF  DORMANT  [low-conf]
 *
 *  -- UnitCombat_C.cpp (7) --
 *   SMSG_ATTACKERSTATEUPDATE                       0x06AA  ACTIVE
 *   SMSG_UNKNOWN_0x0C9E                            0x0C9E  DOC
 *   SMSG_ENVIRONMENTALDAMAGELOG                    0x0DF1  DORMANT
 *   SMSG_ATTACKSWING_ERROR                         0x11E1  ACTIVE   [low-conf]
 *   SMSG_ATTACKSTOP                                0x12AF  ACTIVE
 *   SMSG_COMBAT_EVENT_FAILED                       0x18C3  DORMANT
 *   SMSG_ATTACKSTART                               0x1A9E  ACTIVE
 *
 *  -- Unit_C.cpp (112) --
 *   SMSG_MOUNTSPECIAL_ANIM                         0x003A  DORMANT
 *   SMSG_SPLINE_MOVE_SET_SWIM_BACK_SPEED           0x0046  ACTIVE
 *   SMSG_MOVE_UPDATE_WALK_SPEED                    0x0047  DORMANT
 *   SMSG_COMPOUND_MOVE                             0x0061  DORMANT
 *   SMSG_FLIGHT_SPLINE_SYNC                        0x0063  DORMANT
 *   SMSG_MOVE_SET_TURN_RATE                        0x0069  ACTIVE
 *   SMSG_UNKNOWN_0x006B                            0x006B  DOC
 *   SMSG_MOVE_SET_FLIGHT_SPEED                     0x006E  ACTIVE
 *   SMSG_AURA_UPDATE                               0x0072  ACTIVE
 *   SMSG_MOVE_UPDATE_FLIGHT_SPEED                  0x00E1  DORMANT
 *   SMSG_MOVE_UNSET_CAN_FLY                        0x0162  ACTIVE
 *   SMSG_SPLINE_MOVE_UNROOT                        0x01E1  DORMANT
 *   SMSG_MOVE_UPDATE_SWIM_SPEED                    0x01E2  DORMANT
 *   SMSG_UNKNOWN_0x023B                            0x023B  DOC
 *   SMSG_MOVE_SET_COLLISION_HGT                    0x0250  DORMANT
 *   SMSG_MOVE_UPDATE_KNOCK_BACK                    0x0251  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_HOVER                     0x0258  DORMANT
 *   SMSG_MOVE_UPDATE_SWIM_BACK_SPEED               0x025A  DORMANT
 *   SMSG_UNKNOWN_0x02BB                            0x02BB  DOC
 *   SMSG_MOVE_UNSET_HOVER                          0x02D3  DORMANT
 *   SMSG_SPLINE_MOVE_SET_RUN_SPEED                 0x02F1  ACTIVE
 *   SMSG_MOVE_SET_FLIGHT_BACK_SPEED                0x0319  ACTIVE
 *   SMSG_MOVE_REMOVE_MOVEMENT_FORCE                0x0341  DORMANT
 *   SMSG_MOVE_UPDATE_FLIGHT_BACK_SPEED             0x036A  DOC
 *   SMSG_PLAY_ONE_SHOT_ANIM_KIT                    0x043E  DORMANT
 *   SMSG_SPELL_COOLDOWN                            0x0452  ACTIVE
 *   SMSG_MOVE_SET_WALK_SPEED                       0x0469  ACTIVE
 *   SMSG_MIRROR_IMAGE_CREATURE_DATA                0x04D0  DORMANT
 *   SMSG_MIRROR_IMAGE_COMPONENTED_DATA             0x04D9  DORMANT
 *   SMSG_MOVE_KNOCK_BACK                           0x0562  ACTIVE
 *   SMSG_PLAY_SPELL_VISUAL                         0x061E  DORMANT
 *   SMSG_THREAT_UPDATE                             0x0632  ACTIVE   [high-conf]
 *   SMSG_AI_REACTION                               0x06AF  ACTIVE
 *   SMSG_SPLINE_MOVE_ROOT                          0x0728  DORMANT
 *   SMSG_MOVE_SET_SWIM_SPEED                       0x0817  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_TURN_RATE                 0x0832  ACTIVE
 *   SMSG_SPLINE_MOVE_GRAVITY_DISABLE               0x0845  ACTIVE
 *   SMSG_MOVE_SET_VEHICLE_REC_ID                   0x0861  DORMANT
 *   SMSG_SPLINE_MOVE_GRAVITY_ENABLE                0x0865  ACTIVE
 *   SMSG_MOVE_UNSET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY 0x0868  DORMANT
 *   SMSG_MOVE_LAND_WALK                            0x086A  ACTIVE
 *   SMSG_MOVE_UPDATE_RUN_BACK_SPEED                0x08A3  DORMANT
 *   SMSG_SPLINE_MOVE_SET_WALK_SPEED                0x08B2  ACTIVE
 *   SMSG_MOVE_NORMAL_FALL                          0x08E0  DORMANT
 *   SMSG_MOVE_SET_SWIM_BACK_SPEED                  0x0962  ACTIVE
 *   SMSG_MOVE_UPDATE_PITCH_RATE                    0x09E2  DOC
 *   SMSG_UNKNOWN_0x0A02                            0x0A02  DOC
 *   SMSG_UNKNOWN_0x0A03                            0x0A03  DOC
 *   SMSG_MOVE_GRAVITY_ENABLE                       0x0A27  ACTIVE
 *   SMSG_MOVE_SET_RUN_BACK_SPEED                   0x0A83  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_PITCH_RATE                0x0AB3  ACTIVE
 *   SMSG_MOVE_UPDATE_APPLY_MOVEMENT_FORCE          0x0AB6  DORMANT
 *   SMSG_SPLINE_MOVE_SET_NORMAL_FALL               0x0B08  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_RUN_MODE                  0x0B18  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_FLIGHT_BACK_SPEED         0x0B28  ACTIVE
 *   SMSG_MOVE_TELEPORT                             0x0B39  ACTIVE
 *   SMSG_MOVE_FEATHER_FALL                         0x0C60  DORMANT
 *   SMSG_MOVE_SET_ACTIVE_MOVER                     0x0C6D  ACTIVE
 *   SMSG_SET_MOVEMENT_ANIM_KIT                     0x0CAA  DORMANT
 *   SMSG_SPLINE_MOVE_UNSET_HOVER                   0x0CE1  DORMANT
 *   SMSG_MOVE_UNSET_CAN_TURN_WHILE_FALLING         0x0D61  DOC
 *   SMSG_MOVE_UPDATE_TURN_RATE                     0x0D62  DORMANT
 *   SMSG_SPLINE_MOVE_UNSET_FLYING                  0x0DE2  ACTIVE
 *   SMSG_DISMOUNT                                  0x0E3A  ACTIVE   [high-conf]
 *   SMSG_SPLINE_MOVE_START_SWIM                    0x0F29  DORMANT
 *   SMSG_UNKNOWN_0x100B                            0x100B  DOC
 *   SMSG_CLIENT_CONTROL_UPDATE                     0x1043  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_FLYING                    0x1046  ACTIVE
 *   SMSG_MOVE_SET_CAN_TURN_WHILE_FALLING           0x1065  DORMANT
 *   SMSG_POWER_UPDATE                              0x109F  ACTIVE
 *   SMSG_MOVE_UPDATE_REMOVE_MOVEMENT_FORCE         0x1464  DORMANT
 *   SMSG_HEALTH_UPDATE                             0x148B  DORMANT
 *   SMSG_SET_VEHICLE_REC_ID                        0x149F  DORMANT
 *   SMSG_HIGHEST_THREAT_UPDATE                     0x14AE  ACTIVE   [high-conf]
 *   SMSG_UNKNOWN_0x1553                            0x1553  DOC
 *   SMSG_MOVE_UPDATE_RUN_SPEED                     0x158E  DORMANT
 *   SMSG_MOVE_GRAVITY_DISABLE                      0x159F  ACTIVE
 *   SMSG_MOVE_UPDATE_TELEPORT                      0x15A9  DOC
 *   SMSG_FORCE_MOVE_ROOT                           0x15AE  ACTIVE
 *   SMSG_MOVE_COLLISION_DISABLE                    0x15B8  DORMANT
 *   SMSG_PET_ACTION_SOUND                          0x15E2  ACTIVE
 *   SMSG_MOVE_SET_CAN_FLY                          0x178D  ACTIVE
 *   SMSG_SPLINE_MOVE_STOP_SWIM                     0x1798  DORMANT
 *   SMSG_MOVE_SET_PITCH_RATE                       0x17AB  ACTIVE
 *   SMSG_MOVE_SET_HOVER                            0x1802  DORMANT
 *   SMSG_THREAT_CLEAR                              0x180B  ACTIVE   [high-conf]
 *   SMSG_MOVE_UPDATE_COLLISION_HEIGHT              0x1812  DORMANT
 *   SMSG_UNKNOWN_0x181B                            0x181B  DOC
 *   SMSG_SPLINE_MOVE_SET_WATER_WALK                0x1823  ACTIVE
 *   SMSG_MOVE_COLLISION_ENABLE                     0x1826  DORMANT
 *   SMSG_MOVE_SET_RUN_SPEED                        0x184C  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_WALK_MODE                 0x1865  ACTIVE
 *   SMSG_UNKNOWN_0x186F                            0x186F  DOC
 *   SMSG_SPLINE_MOVE_SET_FEATHER_FALL              0x1893  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_LAND_WALK                 0x18B6  ACTIVE
 *   SMSG_THREAT_REMOVE                             0x1960  ACTIVE   [high-conf]
 *   SMSG_PRE_RESURRECT                             0x19C0  ACTIVE   [high-conf]
 *   SMSG_MONSTER_MOVE                              0x1A07  ACTIVE
 *   SMSG_PLAYER_MOVE                               0x1A32  ACTIVE
 *   SMSG_PET_DISMISS_SOUND                         0x1ABB  DORMANT
 *   SMSG_UNKNOWN_0x1C0E                            0x1C0E  DOC
 *   SMSG_STANDSTATE_UPDATE                         0x1C12  ACTIVE
 *   SMSG_LOOT_LIST                                 0x1C3F  DORMANT
 *   SMSG_SPLINE_MOVE_SET_SWIM_SPEED                0x1D8E  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_FLIGHT_SPEED              0x1DAB  ACTIVE
 *   SMSG_MOVE_APPLY_MOVEMENT_FORCE                 0x1DBE  DORMANT
 *   SMSG_CANCEL_AUTO_REPEAT                        0x1E0F  ACTIVE   [high-conf]  direct packed-GUID reader and Unit_C auto-repeat-clear terminal; name reference-consensus
 *   SMSG_UNKNOWN_0x1E12                            0x1E12  DOC
 *   SMSG_UNKNOWN_0x1E9F                            0x1E9F  DOC
 *   SMSG_MOVE_WATER_WALK                           0x1F9A  ACTIVE
 *   SMSG_SPLINE_MOVE_SET_RUN_BACK_SPEED            0x1F9F  ACTIVE
 *   SMSG_FORCE_MOVE_UNROOT                         0x1FAE  ACTIVE
 *
 *  -- VignetteInfo.cpp (2) --
 *   SMSG_UNKNOWN_0x088F                            0x088F  DOC      [low-conf]
 *   SMSG_VIGNETTE_UPDATE                           0x0CBE  DORMANT  [medium-conf]
 *
 *  -- VoidStorage_C.cpp (4) --
 *   SMSG_PET_BATTLE_ROUND_RESULT                   0x0C1A  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x0E9A                            0x0E9A  DOC      [low-conf]
 *   SMSG_UNKNOWN_0x161F                            0x161F  DOC      [low-conf]
 *   SMSG_VOID_ITEM_SWAP_RESPONSE                   0x1EBF  DORMANT
 *
 *  -- WardenClient.cpp (7) --
 *   SMSG_LOGIN_SETTIMESPEED                        0x082B  ACTIVE   [low-conf]
 *   SMSG_GAMETIME_SET                              0x0A0F  DORMANT  [low-conf]
 *   SMSG_WARDEN_DATA                               0x0C0A  DORMANT
 *   SMSG_GAMETIME_UPDATE                           0x0E1B  DORMANT  [low-conf]
 *   SMSG_UNKNOWN_0x1042                            0x1042  DOC      [low-conf]
 *   SMSG_SET_TIME_ZONE_INFORMATION                 0x19C1  ACTIVE   [low-conf]
 *   SMSG_SERVERTIME                                0x1C3E  DORMANT  [low-conf]
 *
 *  -- WowClientDB2.cpp (2) --
 *   SMSG_UNKNOWN_0x0EBF                            0x0EBF  DOC      [medium-conf]
 *   SMSG_HOTFIX_NOTIFY_BLOB                        0x1EBA  DORMANT  [medium-conf]
 *
 *  -- blp.cpp (5) --
 *   SMSG_UNKNOWN_0x020F                            0x020F  DOC      [unattributed]
 *   SMSG_UNKNOWN_0x05D8                            0x05D8  DOC      [unattributed]
 *   SMSG_DEBUG_RUNE_REGEN                          0x12A7  DORMANT  [unattributed]
 *   SMSG_REFORGE_RESULT                            0x141E  DORMANT  [unattributed]
 *   SMSG_BATTLE_PAY_DELIVERY_STARTED               0x1E32  DORMANT  [unattributed]
 *
 *  -- login/auth message layer (12) --
 *   SMSG_CLIENTCACHE_VERSION                       0x002A  ACTIVE
 *   SMSG_LOGOUT_RESPONSE                           0x008F  ACTIVE   [medium-conf]
 *   SMSG_WAIT_QUEUE_FINISH                         0x060E  DORMANT  [name: binary-proven]
 *   SMSG_LOGOUT_CANCEL_ACK                         0x0AAF  ACTIVE   [medium-conf]
 *   SMSG_AUTH_RESPONSE                             0x0ABA  ACTIVE
 *   SMSG_WAIT_QUEUE_UPDATE                         0x0C2F  DORMANT  [name: binary-proven]
 *   SMSG_CHAR_DELETE                               0x0C9F  ACTIVE   [medium-conf]
 *   SMSG_CHAR_ENUM                                 0x11C3  ACTIVE
 *   SMSG_LOGOUT_COMPLETE                           0x142F  ACTIVE   [medium-conf]
 *   SMSG_ADDON_INFO                                0x160A  ACTIVE
 *   SMSG_CHARACTER_LOGIN_FAILED                    0x1A0B  ACTIVE   [medium-conf]
 *   SMSG_CHAR_CREATE                               0x1CAA  ACTIVE   [medium-conf]
 *
 *  -- unresolved (154) --
 *   SMSG_UNKNOWN_0x0003                            0x0003  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x002B                            0x002B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x003F                            0x003F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0086                            0x0086  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x00AF                            0x00AF  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x00BF                            0x00BF  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0115                            0x0115  DOC      [unattributed]  dynamic slot 13 installed by 0x7C170E
 *   SMSG_UNKNOWN_0x0202                            0x0202  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x020E                            0x020E  DOC      [unattributed]
 *   SMSG_UNKNOWN_0x021B                            0x021B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0222                            0x0222  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0226                            0x0226  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x022E                            0x022E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x028A                            0x028A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x029B                            0x029B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x029F                            0x029F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x02A3                            0x02A3  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x02BE                            0x02BE  DOC      [unattributed]  handler never installed in this build
 *   SMSG_SERVER_MESSAGE                            0x0302  ACTIVE   [unattributed]  dynamic slot 66 installed by 0xCE2FDA
 *   SMSG_READ_ITEM_OK                              0x0305  DORMANT  [unattributed]  dynamic slot 69 installed by 0x7C170E
 *   SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT            0x0332  ACTIVE   [unattributed]  dynamic slot 90 installed by 0x94E1E0
 *   SMSG_UNKNOWN_0x040A                            0x040A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0413                            0x0413  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x041B                            0x041B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x042F                            0x042F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x043A                            0x043A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x043B                            0x043B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0470                            0x0470  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x049F                            0x049F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x04AE                            0x04AE  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x04F0                            0x04F0  DOC      [unattributed]  handler never installed in this build
 *   SMSG_FRIEND_STATUS                             0x0532  ACTIVE   [unattributed]  dynamic slot 154 installed by 0xA6C177
 *   SMSG_UNKNOWN_0x0534                            0x0534  DOC      [unattributed]  alternate guild-command-result route; canonical purpose unresolved
 *   SMSG_UNKNOWN_0x0569                            0x0569  DOC      [unattributed]  special-control (ingress)
 *   SMSG_SET_RAID_DIFFICULTY                       0x0591  ACTIVE   [unattributed]  dynamic slot 169 installed by 0xCD3E18
 *   SMSG_UNKNOWN_0x061F                            0x061F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x062E                            0x062E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0633                            0x0633  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x069E                            0x069E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_GOSSIP_POI                                0x0785  ACTIVE   [unattributed]  dynamic slot 229 installed by 0x964A6D
 *   SMSG_PET_BATTLE_FINALIZE_LOCATION              0x082E  DORMANT  [unattributed]
 *   SMSG_UNKNOWN_0x08AE                            0x08AE  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x08BB                            0x08BB  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x08F3                            0x08F3  DOC      [unattributed]  handler never installed in this build
 *   SMSG_AUTH_CHALLENGE                            0x0949  ACTIVE   [unattributed]  special-control (ingress)
 *   SMSG_RESUME_COMMS                              0x0969  DORMANT  [unattributed]  special-control (ingress)
 *   SMSG_AUCTION_LIST_RESULT                       0x0982  DORMANT  [unattributed]  dynamic slot 290 installed by 0x9CCA19
 *   SMSG_EMOTE                                     0x0987  ACTIVE   [unattributed]  dynamic slot 295 installed by 0x7C170E
 *   SMSG_UNKNOWN_0x0A0A                            0x0A0A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0A1B                            0x0A1B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_DEBUG_AISTATE                             0x0A2A  DORMANT  [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0A69                            0x0A69  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0A8E                            0x0A8E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0ABB                            0x0ABB  DOC      [unattributed]  handler never installed in this build
 *   SMSG_TRIGGER_CINEMATIC                         0x0B01  ACTIVE   [unattributed]  dynamic slot 321 installed by 0x7C170E
 *   SMSG_CHANNEL_LIST                              0x0B22  ACTIVE   [unattributed]  dynamic slot 338 installed by 0xCE2FDA
 *   SMSG_AUCTION_BIDDER_LIST_RESULT                0x0B24  DORMANT  [unattributed]  dynamic slot 340 installed by 0x9CCA19
 *   SMSG_AUCTION_LIST_PENDING_SALES                0x0B81  DORMANT  [unattributed]  dynamic slot 353 installed by 0x9CCA19
 *   SMSG_UNKNOWN_0x0C1F                            0x0C1F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0C2B                            0x0C2B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0C8B                            0x0C8B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0CBA                            0x0CBA  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0D48                            0x0D48  DOC      [unattributed]  special-control (ingress)
 *   SMSG_RAID_GROUP_ONLY                           0x0D82  DORMANT  [unattributed]  dynamic slot 418 installed by 0x7C170E
 *   SMSG_UNKNOWN_0x0DA7                            0x0DA7  DOC      [unattributed]  dynamic slot 439 installed by 0x7C170E
 *   SMSG_UNKNOWN_0x0E0E                            0x0E0E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0E2F                            0x0E2F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0E3E                            0x0E3E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0E8E                            0x0E8E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0EAE                            0x0EAE  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x0EBE                            0x0EBE  DOC      [unattributed]  handler never installed in this build
 *   SMSG_CHANNEL_NOTIFY                            0x0F06  ACTIVE   [unattributed]  dynamic slot 454 installed by 0xCE2FDA
 *   SMSG_UNKNOWN_0x0F27                            0x0F27  DOC      [unattributed]  dynamic slot 471 installed by 0x7C170E
 *   SMSG_PARTY_COMMAND_RESULT                      0x0F86  ACTIVE   [unattributed]  dynamic slot 486 installed by 0x7C170E
 *   SMSG_UNKNOWN_0x102F                            0x102F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_AUTH_SRP6_RESPONSE                        0x103A  DORMANT  [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x108E                            0x108E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x108F                            0x108F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x109E                            0x109E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x10AB                            0x10AB  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x10BA                            0x10BA  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x10BF                            0x10BF  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x10C0                            0x10C0  DOC      [unattributed]  handler never installed in this build
 *   MSG_MOVE_SET_TURN_RATE_CHEAT                   0x10D8  DORMANT  [unattributed]  handler never installed in this build
 *   SMSG_FORCE_RUN_SPEED_CHANGE                    0x10E3  DORMANT  [unattributed]  handler never installed in this build
 *   SMSG_ITEM_TEXT_QUERY_RESPONSE                  0x1134  ACTIVE   [unattributed]  dynamic slot 540 installed by 0x60113B
 *   SMSG_SERVER_BUCK_DATA                          0x1142  DORMANT  [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1148                            0x1148  DOC      [unattributed]  special-control (ingress)
 *   SMSG_CONNECT_TO                                0x1149  DORMANT  [unattributed]  special-control (ingress)
 *   SMSG_MULTIPLE_PACKETS                          0x1168  DORMANT  [unattributed]  special-control (ingress)
 *   SMSG_UNKNOWN_0x123E                            0x123E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x12A2                            0x12A2  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x12AA                            0x12AA  DOC      [unattributed]  handler never installed in this build
 *   SMSG_GROUP_UNINVITE                            0x1313  ACTIVE   [unattributed]  dynamic slot 587 installed by 0x7C170E
 *   SMSG_UNKNOWN_0x140B                            0x140B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x141F                            0x141F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x142B                            0x142B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1433                            0x1433  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x143B                            0x143B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x143F                            0x143F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1442                            0x1442  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x14AB                            0x14AB  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x14C1                            0x14C1  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x14D2                            0x14D2  DOC      [unattributed]  handler never installed in this build
 *   SMSG_BATTLEFIELD_MANAGER_ENTERING              0x14E1  DORMANT  [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1541                            0x1541  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1543                            0x1543  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1561                            0x1561  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1568                            0x1568  DOC      [unattributed]  special-control (ingress)
 *   SMSG_UNKNOWN_0x15C1                            0x15C1  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x15E1                            0x15E1  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x163E                            0x163E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x168B                            0x168B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x169A                            0x169A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x16AA                            0x16AA  DOC      [unattributed]  handler never installed in this build
 *   SMSG_AUCTION_OWNER_LIST_RESULT                 0x1785  DORMANT  [unattributed]  dynamic slot 741 installed by 0x9CCA19
 *   SMSG_UPDATE_OBJECT                             0x1792  ACTIVE   [unattributed]  dynamic slot 746 installed by 0x79E441
 *   SMSG_GROUP_DECLINE                             0x17A3  DORMANT  [unattributed]  dynamic slot 755 installed by 0x7C170E
 *   SMSG_UNKNOWN_0x181A                            0x181A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x182A                            0x182A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x182F                            0x182F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x183F                            0x183F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x188B                            0x188B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x18AA                            0x18AA  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x18BB                            0x18BB  DOC      [unattributed]  handler never installed in this build
 *   SMSG_ITEM_COOLDOWN                             0x1904  ACTIVE  [Wow.exe binary: dynamic slot 772 handler sub_77D70B reads uint64 item GUID then uint32 spell ID]
 *   SMSG_UNKNOWN_0x1949                            0x1949  DOC      [unattributed]  special-control (ingress)
 *   SMSG_UNKNOWN_0x1968                            0x1968  DOC      [unattributed]  special-control (ingress)
 *   SMSG_PONG                                      0x1969  ACTIVE   [unattributed]  special-control (ingress)
 *   SMSG_UNKNOWN_0x1990                            0x1990  DOC      [unattributed]  dynamic slot 808 installed by 0x7C170E
 *   SMSG_UNKNOWN_0x1A3E                            0x1A3E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1A8A                            0x1A8A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1ABA                            0x1ABA  DOC      [unattributed]  handler never installed in this build
 *   SMSG_GROUP_DESTROYED                           0x1B27  ACTIVE   [unattributed]  dynamic slot 855 installed by 0x7C170E
 *   SMSG_TUTORIAL_FLAGS                            0x1B90  ACTIVE   [unattributed]  dynamic slot 872 installed by 0x40364E
 *   SMSG_GUILD_MAX_DAILY_XP                        0x1BE1  DORMANT  [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1C1A                            0x1C1A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1C1B                            0x1C1B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1C1E                            0x1C1E  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1C32                            0x1C32  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1C8A                            0x1C8A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1CAB                            0x1CAB  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1CBB                            0x1CBB  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1D23                            0x1D23  DOC      [unattributed]  dynamic slot 915 installed by 0x64B307
 *   SMSG_SUSPEND_COMMS                             0x1D48  DORMANT  [unattributed]  special-control (ingress)
 *   SMSG_UNKNOWN_0x1DA3                            0x1DA3  DOC      [unattributed]  dynamic slot, install UNPROVABLE
 *   SMSG_UNKNOWN_0x1E2A                            0x1E2A  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1E2B                            0x1E2B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1E3F                            0x1E3F  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1E8B                            0x1E8B  DOC      [unattributed]  handler never installed in this build
 *   SMSG_UNKNOWN_0x1E9B                            0x1E9B  DOC      [unattributed]
 *   SMSG_UNKNOWN_0x1EBE                            0x1EBE  DOC      [unattributed]  handler never installed in this build
 *   SMSG_SHOW_MAILBOX                              0x1F13  ACTIVE   [unattributed]  dynamic slot 971 installed by 0x9AADC7
 *   SMSG_CONTACT_LIST                              0x1F22  ACTIVE   [unattributed]  dynamic slot 978 installed by 0xA6C177
 *
 * ==== CMSG (595) ====
 *
 *  -- client-send (595) --
 *   CMSG_SET_ACHIEVEMENTS_HIDDEN                   0x0002  DOC
 *   CMSG_CHANNEL_MUTE                              0x000A  DORMANT
 *   CMSG_PING                                      0x0012  ACTIVE
 *   CMSG_UNACCEPT_TRADE                            0x0023  ACTIVE
 *   CMSG_DESTROY_ITEM                              0x0026  ACTIVE
 *   CMSG_NEUTRAL_PLAYER_SELECT_FACTION             0x0027  DOC
 *   CMSG_MESSAGECHAT_DND                           0x002E  ACTIVE
 *   CMSG_VIOLENCE_LEVEL                            0x0040  ACTIVE
 *   CMSG_TIME_SYNC_RESPONSE_FAILED                 0x0058  ACTIVE
 *   CMSG_UNKNOWN_0x0060                            0x0060  DOC
 *   CMSG_REALM_SPLIT                               0x0062  ACTIVE
 *   CMSG_UPDATE_ACCOUNT_DATA                       0x0068  ACTIVE
 *   CMSG_UNKNOWN_0x0069                            0x0069  DOC
 *   CMSG_LFD_LOCK_INFO_REQUEST                     0x006B  ACTIVE   server-binding=CMSG_LFG_LOCK_INFO_REQUEST
 *   CMSG_UNKNOWN_0x0071                            0x0071  DOC
 *   CMSG_MOVE_TELEPORT_ACK                         0x0078  ACTIVE
 *   CMSG_MOVE_STOP_PITCH                           0x007A  DORMANT
 *   CMSG_MOUNTSPECIAL_ANIM                         0x0082  DORMANT
 *   CMSG_SCENE_COMPLETED                           0x0087  DOC
 *   CMSG_MESSAGECHAT_ADDON_RAID                    0x009A  ACTIVE
 *   CMSG_CLEAR_TRADE_ITEM                          0x00A7  ACTIVE
 *   CMSG_CHANNEL_MODERATOR                         0x00AE  DORMANT
 *   CMSG_CHANNEL_OWNER                             0x00AF  DORMANT
 *   CMSG_AUTH_SESSION                              0x00B2  ACTIVE
 *   CMSG_UNKNOWN_0x00B3                            0x00B3  DOC
 *   CMSG_MESSAGECHAT_CHANNEL                       0x00BB  ACTIVE
 *   CMSG_MOVE_START_PITCH_UP                       0x00D8  DORMANT
 *   CMSG_MOVE_FALL_RESET                           0x00D9  DORMANT
 *   CMSG_FORCE_WALK_SPEED_CHANGE_ACK               0x00DB  ACTIVE
 *   CMSG_CHAR_ENUM                                 0x00E0  ACTIVE
 *   CMSG_UNKNOWN_0x00E3                            0x00E3  ACTIVE   server-binding=CMSG_LFG_LFR_LEAVE
 *   CMSG_UNKNOWN_0x00F0                            0x00F0  DOC
 *   CMSG_MOVE_KNOCK_BACK_ACK                       0x00F2  ACTIVE
 *   CMSG_ITEM_TEXT_QUERY                           0x0123  ACTIVE
 *   CMSG_OPENING_CINEMATIC                         0x0130  DORMANT
 *   CMSG_QUERY_VOID_STORAGE                        0x0140  DOC
 *   CMSG_REQUEST_VEHICLE_NEXT_SEAT                 0x0141  DORMANT
 *   CMSG_MOVE_TIME_SKIPPED                         0x0150  ACTIVE
 *   CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK           0x0158  ACTIVE
 *   CMSG_UNKNOWN_0x0160                            0x0160  DOC
 *   CMSG_BATTLE_PET_SET_BATTLE_SLOT                0x0163  DOC
 *   CMSG_MOVE_STOP_STRAFE                          0x0171  ACTIVE
 *   CMSG_FORCE_PITCH_RATE_CHANGE_ACK               0x0172  DORMANT
 *   CMSG_UNKNOWN_0x017A                            0x017A  DOC
 *   CMSG_UNKNOWN_0x01C0                            0x01C0  DOC
 *   CMSG_MOVE_START_TURN_LEFT                      0x01D0  ACTIVE
 *   CMSG_MOVE_START_DESCEND                        0x01D1  ACTIVE
 *   CMSG_TIME_SYNC_RESP                            0x01DB  ACTIVE
 *   CMSG_LFG_LEAVE                                 0x01E0  ACTIVE
 *   CMSG_SET_EVERYONE_IS_ASSISTANT                 0x01E1  ACTIVE
 *   CMSG_CALENDAR_EVENT_SIGNUP                     0x01E3  ACTIVE
 *   CMSG_MOVE_SET_FLY                              0x01F1  ACTIVE
 *   MSG_MOVE_HEARTBEAT                             0x01F2  ACTIVE
 *   CMSG_MOVE_START_STRAFE_LEFT                    0x01F8  ACTIVE
 *   CMSG_CAST_SPELL                                0x0206  ACTIVE
 *   CMSG_CHANNEL_UNMUTE                            0x022A  DORMANT
 *   CMSG_MAIL_MARK_AS_READ                         0x0241  ACTIVE
 *   CMSG_SET_LFG_BONUS_FACTION_ID                  0x0247  DOC
 *   CMSG_SETSHEATHED                               0x0249  ACTIVE
 *   CMSG_CANCEL_TEMP_ENCHANTMENT                   0x024B  DORMANT
 *   CMSG_GUILD_BANK_BUY_TAB                        0x0251  DORMANT
 *   CMSG_PETITION_QUERY                            0x0255  ACTIVE
 *   CMSG_LEAVE_BATTLEFIELD                         0x0257  ACTIVE
 *   CMSG_AUCTION_REMOVE_ITEM                       0x0259  DORMANT
 *   CMSG_PET_ACTION                                0x025B  ACTIVE
 *   CMSG_AUTOEQUIP_ITEM                            0x025F  ACTIVE
 *   CMSG_KEYBOUND_OVERRIDE                         0x0264  DOC
 *   CMSG_ZONEUPDATE                                0x0265  DORMANT
 *   CMSG_INITIATE_TRADE                            0x0267  ACTIVE
 *   CMSG_UNLEARN_SKILL                             0x0268  DORMANT
 *   CMSG_UNKNOWN_0x0274                            0x0274  DOC
 *   CMSG_CONFIRM_RESPEC_WIPE                       0x0275  ACTIVE
 *   CMSG_IGNORE_TRADE                              0x0276  DORMANT
 *   CMSG_RIDE_VEHICLE_INTERACT                     0x0277  DORMANT
 *   CMSG_SET_FACTION_ATWAR                         0x027B  DORMANT
 *   CMSG_NPC_TEXT_QUERY                            0x0287  ACTIVE   [high-conf]
 *   CMSG_MESSAGECHAT_ADDON_PARTY                   0x028E  ACTIVE
 *   CMSG_SUSPEND_TOKEN_RESPONSE                    0x0292  ACTIVE
 *   CMSG_UNREGISTER_ALL_ADDON_PREFIXES             0x029F  ACTIVE
 *   CMSG_GET_MIRRORIMAGE_DATA                      0x02A3  DORMANT
 *   CMSG_LEARN_TALENT                              0x02A7  DORMANT
 *   CMSG_REPAIR_ITEM                               0x02C1  DORMANT
 *   CMSG_SCENE_TRIGGER_EVENT                       0x02C4  DOC
 *   CMSG_SET_PVP                                   0x02C5  DOC
 *   CMSG_PET_BATTLE_REQUEST_PVP                    0x02C7  DOC
 *   CMSG_LIST_PETS                                 0x02CA  ACTIVE   server-binding=CMSG_REQUEST_STABLED_PETS
 *   CMSG_SOCKET_GEMS                               0x02CB  DORMANT
 *   CMSG_AUTOSTORE_BANK_ITEM                       0x02CF  DORMANT
 *   CMSG_BATTLEMASTER_JOIN_ARENA                   0x02D2  DORMANT
 *   CMSG_QUEST_QUERY                               0x02D5  ACTIVE   [high-conf]
 *   CMSG_UNKNOWN_0x02D6                            0x02D6  DOC
 *   CMSG_LIST_INVENTORY                            0x02D8  ACTIVE
 *   CMSG_AUCTION_LIST_PENDING_SALES                0x02DA  DORMANT
 *   CMSG_QUESTGIVER_HELLO                          0x02DB  ACTIVE   [high-conf]
 *   CMSG_WRAP_ITEM                                 0x02DF  DORMANT
 *   CMSG_TAXINODE_STATUS_QUERY                     0x02E1  ACTIVE
 *   CMSG_BUY_ITEM                                  0x02E2  ACTIVE
 *   CMSG_TAXIQUERYAVAILABLENODES                   0x02E3  ACTIVE
 *   CMSG_ATTACKSWING                               0x02E7  ACTIVE
 *   CMSG_EQUIPMENT_SET_DELETE                      0x02E8  DORMANT
 *   CMSG_BANKER_ACTIVATE                           0x02E9  DORMANT
 *   CMSG_AUCTION_LIST_ITEMS                        0x02EA  DORMANT
 *   CMSG_AUCTION_SELL_ITEM                         0x02EB  DORMANT
 *   CMSG_SPLIT_ITEM                                0x02EC  ACTIVE
 *   CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY          0x02F1  ACTIVE   [high-conf]
 *   CMSG_SHOWING_CLOAK                             0x02F2  ACTIVE
 *   CMSG_LOOT_MONEY                                0x02F6  ACTIVE
 *   CMSG_ACCEPT_LEVEL_GRANT                        0x02FB  DORMANT
 *   CMSG_TUTORIAL_RESET                            0x0307  ACTIVE
 *   CMSG_CALENDAR_GET_EVENT                        0x030C  ACTIVE
 *   CMSG_SUBMIT_COMPLAIN                           0x030D  DOC
 *   CMSG_COMPLAIN                                  0x0319  DORMANT
 *   CMSG_READY_FOR_ACCOUNT_DATA_TIMES              0x031C  ACTIVE
 *   CMSG_UNKNOWN_0x031D                            0x031D  DOC
 *   CMSG_NAME_QUERY                                0x0328  ACTIVE
 *   CMSG_CHAR_FACTION_CHANGE                       0x0329  DORMANT
 *   CMSG_GROUP_RAID_CONVERT                        0x032C  ACTIVE
 *   CMSG_LFG_GET_STATUS                            0x032D  ACTIVE
 *   CMSG_GM_RESPONSE_RESOLVE                       0x033D  DOC
 *   CMSG_SPIRIT_HEALER_ACTIVATE                    0x0340  ACTIVE
 *   CMSG_BATTLEFIELD_MGR_EXIT_REQUEST              0x0343  DORMANT
 *   CMSG_ATTACKSTOP                                0x0345  ACTIVE
 *   CMSG_TRAINER_LIST                              0x034B  DORMANT
 *   CMSG_TRAINER_BUY_SPELL                         0x0352  DORMANT
 *   CMSG_AUTOSTORE_LOOT_ITEM                       0x0354  ACTIVE
 *   CMSG_SWAP_ITEM                                 0x035D  ACTIVE
 *   CMSG_SELF_RES                                  0x0360  ACTIVE
 *   CMSG_AUCTION_LIST_OWNER_ITEMS                  0x0361  DORMANT
 *   CMSG_REQUEST_CONQUEST_FORMULA_CONSTANTS        0x0365  ACTIVE
 *   CMSG_QUESTGIVER_STATUS_QUERY                   0x036A  ACTIVE
 *   CMSG_USE_EQUIPMENT_SET                         0x036E  DORMANT
 *   CMSG_AUTOEQUIP_ITEM_SLOT                       0x036F  DORMANT
 *   CMSG_GUILD_BANKER_ACTIVATE                     0x0372  ACTIVE
 *   CMSG_QUERY_INSPECT_ACHIEVEMENTS                0x0373  DORMANT
 *   CMSG_USED_FOLLOW                               0x0374  DOC
 *   CMSG_REQUEST_PVP_REWARDS                       0x0375  DORMANT
 *   CMSG_PET_BATTLE_REQUEST_UPDATE                 0x0377  DOC
 *   CMSG_QUESTGIVER_REQUEST_REWARD                 0x0378  ACTIVE
 *   CMSG_AUCTION_HELLO                             0x0379  ACTIVE
 *   CMSG_PETITION_SHOWLIST                         0x037B  ACTIVE
 *   CMSG_REQUEST_VEHICLE_PREV_SEAT                 0x03C4  DORMANT
 *   CMSG_SET_TITLE                                 0x03C7  ACTIVE
 *   CMSG_AUCTION_PLACE_BID                         0x03C8  DORMANT
 *   CMSG_ACTIVATETAXI                              0x03C9  ACTIVE
 *   CMSG_PUSHQUESTTOPARTY                          0x03D2  ACTIVE
 *   CMSG_RECLAIM_CORPSE                            0x03D3  ACTIVE
 *   CMSG_SET_TRADE_ITEM                            0x03D5  ACTIVE
 *   CMSG_SWAP_INV_ITEM                             0x03DF  ACTIVE
 *   CMSG_DUEL_RESPONSE                             0x03E2  DOC
 *   CMSG_SET_CURRENCY_FLAGS                        0x03E4  ACTIVE
 *   CMSG_STANDSTATECHANGE                          0x03E6  ACTIVE
 *   CMSG_AREA_SPIRIT_HEALER_QUERY                  0x03F1  DORMANT
 *   CMSG_PLAYED_TIME                               0x03F6  ACTIVE
 *   CMSG_ADDON_REGISTERED_PREFIXES                 0x040E  ACTIVE
 *   CMSG_CHANNEL_UNMODERATOR                       0x041E  DORMANT
 *   CMSG_LEAVE_CHANNEL                             0x042A  DORMANT
 *   CMSG_UNLOCK_VOID_STORAGE                       0x0444  DOC
 *   CMSG_CHOICE_RESPONSE                           0x0447  DOC
 *   CMSG_PET_CAST_SPELL                            0x044D  DORMANT
 *   CMSG_QUERY_COUNTDOWN_TIMER                     0x044E  ACTIVE
 *   CMSG_UNKNOWN_0x0462                            0x0462  DOC
 *   CMSG_LFG_JOIN                                  0x046B  ACTIVE
 *   CMSG_QUERY_GUILD_RECIPES                       0x0478  DORMANT
 *   CMSG_GUILD_ADD_RANK                            0x047A  ACTIVE
 *   CMSG_CHAT_IGNORED                              0x048A  ACTIVE
 *   CMSG_UNKNOWN_0x049B                            0x049B  DOC
 *   CMSG_MESSAGECHAT_YELL                          0x04AA  ACTIVE
 *   CMSG_GUILD_NEWS_UPDATE_STICKY                  0x04D1  DOC
 *   CMSG_GUILD_LEAVE                               0x04D8  ACTIVE
 *   CMSG_GUILD_BANK_NOTE                           0x04D9  DOC
 *   CMSG_UNKNOWN_0x04DB                            0x04DB  DOC
 *   CMSG_CHAR_DELETE                               0x04E2  ACTIVE
 *   CMSG_CALENDAR_GUILD_FILTER                     0x04E3  ACTIVE
 *   CMSG_QUERY_GUILD_MEMBER_RECIPES                0x04F0  DORMANT
 *   CMSG_QUERY_GUILD_BANK_TEXT                     0x0550  DORMANT
 *   CMSG_LF_GUILD_GET_APPLICATIONS                 0x0558  DOC
 *   CMSG_GUILD_PROMOTE                             0x0571  ACTIVE
 *   CMSG_LF_GUILD_GET_RECRUITS                     0x057A  DOC
 *   CMSG_SET_RAID_DIFFICULTY                       0x0591  ACTIVE
 *   CMSG_GUILD_ASSIGN_MEMBER_RANK                  0x05D0  DOC
 *   CMSG_GUILD_SET_NOTE                            0x05DA  ACTIVE
 *   CMSG_BATTLE_PET_REQUEST_JOURNAL_LOCK           0x05E1  DOC
 *   CMSG_QUERY_GUILD_XP                            0x05F8  DOC
 *   CMSG_QUERY_TIME                                0x0640  ACTIVE
 *   CMSG_UNKNOWN_0x0643                            0x0643  ACTIVE   server-binding=CMSG_LOGOUT_REQUEST
 *   CMSG_TOGGLE_PVP                                0x0644  DORMANT
 *   CMSG_SET_FACTION_NOTATWAR                      0x064B  DOC
 *   CMSG_SWAP_VOID_ITEM                            0x0655  DOC
 *   CMSG_UNKNOWN_0x0656                            0x0656  DOC
 *   CMSG_QUESTGIVER_COMPLETE_QUEST                 0x0659  ACTIVE
 *   CMSG_PET_STOP_ATTACK                           0x065B  ACTIVE
 *   CMSG_BUYBACK_ITEM                              0x0661  ACTIVE
 *   CMSG_GRANT_LEVEL                               0x0662  DORMANT
 *   CMSG_SAVE_EQUIPMENT_SET                        0x0669  DORMANT
 *   CMSG_AUTOBANK_ITEM                             0x066D  DORMANT
 *   CMSG_SET_ACTIONBAR_TOGGLES                     0x0672  ACTIVE
 *   CMSG_TURN_IN_PETITION                          0x0673  ACTIVE
 *   CMSG_BATTLEMASTER_JOIN_RATED                   0x0674  DORMANT
 *   CMSG_BATTLE_PET_UPDATE_NOTIFY                  0x0675  DOC
 *   CMSG_SPELLCLICK                                0x067A  DORMANT
 *   CMSG_AUTOSTORE_BAG_ITEM                        0x067C  ACTIVE
 *   CMSG_CHANNEL_ANNOUNCEMENTS                     0x06AF  DORMANT
 *   CMSG_LOGOUT_CANCEL                             0x06C1  ACTIVE
 *   CMSG_QUERY_GUILD_REWARDS                       0x06C4  DOC
 *   CMSG_BATTLE_PET_WILD_REQUEST                   0x06C5  DOC
 *   CMSG_SET_PRIMARY_TALENT_TREE                   0x06C6  DOC
 *   CMSG_SET_WATCHED_FACTION                       0x06C9  ACTIVE
 *   CMSG_GUILD_AUTO_DECLINE                        0x06CB  DORMANT
 *   CMSG_QUESTGIVER_ACCEPT_QUEST                   0x06D1  ACTIVE   [high-conf]
 *   CMSG_JOIN_PET_BATTLE_QUEUE                     0x06D4  DOC
 *   CMSG_ON_MISSILE_TRAJECTORY_COLLISION           0x06D6  DORMANT
 *   CMSG_TRANSMOGRIFY_ITEMS                        0x06D7  DOC
 *   CMSG_GAMEOBJ_USE                               0x06D8  ACTIVE   [high-conf]
 *   CMSG_GAMEOBJ_REPORT_USE                        0x06D9  ACTIVE   [high-conf]
 *   CMSG_PETITION_SIGN                             0x06DA  ACTIVE
 *   CMSG_OPT_OUT_OF_LOOT                           0x06E0  DORMANT
 *   CMSG_REQUEST_CEMETERY_LIST                     0x06E4  ACTIVE
 *   CMSG_SAVE_CUF_PROFILES                         0x06E6  DOC
 *   CMSG_CONTROLLER_EJECT_PASSENGER                0x06E7  DORMANT
 *   CMSG_PET_SPELL_AUTOCAST                        0x06F0  DORMANT
 *   CMSG_REQUEST_FORCED_REACTIONS                  0x06F5  ACTIVE
 *   CMSG_REPORT_PVP_AFK                            0x06F9  DORMANT
 *   CMSG_MAIL_TAKE_MONEY                           0x06FA  ACTIVE
 *   CMSG_ACTIVATETAXIEXPRESS                       0x06FB  ACTIVE
 *   CMSG_CALENDAR_EVENT_MODERATOR_STATUS           0x0708  ACTIVE
 *   CMSG_UNKNOWN_0x0719                            0x0719  DOC
 *   CMSG_GROUP_INVITE                              0x072D  ACTIVE
 *   CMSG_DEL_IGNORE                                0x0737  ACTIVE
 *   CMSG_GMSURVEY_SUBMIT                           0x073C  DORMANT
 *   CMSG_UNKNOWN_0x073D                            0x073D  DOC
 *   CMSG_SET_SELECTION                             0x0740  ACTIVE
 *   CMSG_ENABLETAXI                                0x0741  DORMANT
 *   CMSG_UNKNOWN_0x0743                            0x0743  DOC
 *   CMSG_MOVE_SET_RUN_MODE                         0x0748  ACTIVE   server-binding=CMSG_GOSSIP_SELECT_OPTION
 *   CMSG_ITEM_PURCHASE_REFUND                      0x074B  DORMANT
 *   CMSG_BLACKMARKET_HELLO                         0x075A  DOC
 *   CMSG_SET_TAXI_BENCHMARK_MODE                   0x0762  DORMANT
 *   CMSG_BATTLEMASTER_JOIN                         0x0769  DORMANT
 *   CMSG_GUILD_BANK_DEPOSIT_MONEY                  0x0770  DORMANT
 *   CMSG_SET_FACTION_INACTIVE                      0x0778  DORMANT
 *   CMSG_QUESTLOG_REMOVE_QUEST                     0x0779  ACTIVE
 *   CMSG_GET_MAIL_LIST                             0x077A  ACTIVE
 *   CMSG_MAIL_QUERY_NEXT_TIME                      0x077B  ACTIVE
 *   CMSG_GUILD_BANK_UPDATE_TAB                     0x07C2  DORMANT
 *   CMSG_QUESTGIVER_CHOOSE_REWARD                  0x07CB  ACTIVE
 *   CMSG_PET_ABANDON                               0x07D0  DORMANT
 *   CMSG_TEXT_EMOTE                                0x07E9  ACTIVE
 *   CMSG_GUILD_BANK_WITHDRAW_MONEY                 0x07EA  DORMANT
 *   CMSG_UNKNOWN_0x07EB                            0x07EB  DOC
 *   CMSG_ALTER_APPEARANCE                          0x07F0  ACTIVE
 *   CMSG_REQUEST_PARTY_MEMBER_STATS                0x0806  ACTIVE
 *   CMSG_BATTLE_PAY_CONFIRM_PURCHASE_RESPONSE      0x0812  DOC
 *   CMSG_CALENDAR_GET_NUM_PENDING                  0x0813  ACTIVE
 *   CMSG_DO_READY_CHECK                            0x0817  ACTIVE
 *   CMSG_CHANNEL_UNBAN                             0x081F  DORMANT
 *   CMSG_REQUEST_RATED_BG_STATS                    0x0826  ACTIVE
 *   CMSG_MINIMAP_PING                              0x0837  ACTIVE
 *   CMSG_MESSAGECHAT_RAID                          0x083E  ACTIVE
 *   CMSG_LOOT_RELEASE                              0x0840  ACTIVE
 *   CMSG_CREATURE_QUERY                            0x0842  ACTIVE
 *   CMSG_MOVE_HOVER_ACK                            0x0858  DORMANT
 *   CMSG_SUBMIT_BUG                                0x0861  DOC
 *   CMSG_GUILD_INVITE                              0x0869  ACTIVE   [high-conf]  thunk sub_661C2D; 9-bit name length plus raw name
 *   CMSG_UNKNOWN_0x0870                            0x0870  DOC
 *   CMSG_MOVE_SET_CAN_TURN_WHILE_FALLING_ACK       0x0873  DOC
 *   CMSG_UNKNOWN_0x087A                            0x087A  DOC
 *   CMSG_INSPECT_RATED_BG_STATS                    0x0882  DORMANT
 *   CMSG_RAID_TARGET_UPDATE                        0x0886  ACTIVE
 *   CMSG_UNKNOWN_0x0896                            0x0896  DOC
 *   CMSG_LFG_SET_ROLES                             0x08A2  ACTIVE
 *   CMSG_RANDOM_ROLL                               0x08A3  ACTIVE
 *   CMSG_REORDER_CHARACTERS                        0x08A7  DORMANT
 *   CMSG_MESSAGECHAT_ADDON_INSTANCE                0x08AF  ACTIVE
 *   CMSG_BATTLEFIELD_MANAGER_EXIT_REQUEST          0x08B3  DORMANT
 *   CMSG_CHANNEL_BAN                               0x08BF  DORMANT
 *   CMSG_CANCEL_CHANNELLING                        0x08C0  DORMANT
 *   CMSG_LEAVE_PET_BATTLE_QUEUE                    0x08C1  DOC
 *   CMSG_MOVE_FEATHER_FALL_ACK                     0x08D0  DORMANT
 *   CMSG_UNKNOWN_0x08D1                            0x08D1  DOC
 *   CMSG_MOVE_APPLY_MOVEMENT_FORCE_ACK             0x08D3  DOC
 *   CMSG_MOVE_START_PITCH_DOWN                     0x08D8  DORMANT
 *   CMSG_UNKNOWN_0x08E1                            0x08E1  DOC
 *   CMSG_UNKNOWN_0x08E2                            0x08E2  DOC
 *   CMSG_BATTLE_CHAR_BOOST                         0x08E3  DOC
 *   CMSG_MOVE_STOP                                 0x08F1  ACTIVE
 *   CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE        0x08F8  DORMANT
 *   CMSG_MOVE_FALL_LAND                            0x08FA  ACTIVE
 *   CMSG_SET_CONTACT_NOTES                         0x0937  ACTIVE
 *   CMSG_UNKNOWN_0x0942                            0x0942  DOC
 *   CMSG_MOVE_STOP_SWIM                            0x0950  DORMANT
 *   CMSG_MOVE_START_FORWARD                        0x095A  ACTIVE
 *   CMSG_CALENDAR_EVENT_REMOVE_INVITE              0x0962  ACTIVE
 *   CMSG_CHAR_RENAME                               0x0963  DORMANT
 *   CMSG_UNKNOWN_0x0973                            0x0973  DOC
 *   CMSG_UNKNOWN_0x0979                            0x0979  DOC
 *   CMSG_ADD_FRIEND                                0x09A6  ACTIVE
 *   CMSG_MOVE_GRAVITY_DISABLE_ACK                  0x09D3  DORMANT
 *   CMSG_MOVE_START_BACKWARD                       0x09D8  ACTIVE
 *   CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK             0x09DA  ACTIVE
 *   CMSG_MOVE_CHNG_TRANSPORT                       0x09DB  ACTIVE
 *   CMSG_BUG                                       0x09E1  DORMANT
 *   CMSG_SET_PLAYER_DECLINED_NAMES                 0x09E2  DORMANT
 *   CMSG_UNKNOWN_0x09E3                            0x09E3  DOC
 *   CMSG_SET_ACTIVE_MOVER                          0x09F0  ACTIVE
 *   CMSG_DISMISS_CONTROLLED_VEHICLE                0x09FA  DORMANT
 *   CMSG_MOVE_SET_COLLISION_HGT_ACK                0x09FB  DORMANT
 *   CMSG_SUGGESTION_SUBMIT                         0x0A12  DOC
 *   CMSG_CHAR_CUSTOMIZE                            0x0A13  ACTIVE
 *   CMSG_CORPSE_MAP_POSITION_QUERY                 0x0A16  ACTIVE
 *   CMSG_CHANNEL_PASSWORD                          0x0A1E  DORMANT
 *   CMSG_REQUEST_PVP_OPTIONS_ENABLED               0x0A22  DORMANT
 *   CMSG_BATTLE_PET_REQUEST_JOURNAL                0x0A23  ACTIVE
 *   CMSG_GMTICKET_UPDATETEXT                       0x0A26  DORMANT
 *   CMSG_PET_RENAME                                0x0A32  DORMANT
 *   CMSG_SUMMON_RESPONSE                           0x0A33  DORMANT
 *   CMSG_CALENDAR_ADD_EVENT                        0x0A37  ACTIVE
 *   CMSG_GMTICKET_SYSTEMSTATUS                     0x0A82  ACTIVE
 *   CMSG_REQUEST_RAID_INFO                         0x0A87  ACTIVE
 *   CMSG_UNKNOWN_0x0A8F                            0x0A8F  DOC
 *   CMSG_BATTLEFIELD_MANAGER_QUEUE_INVITE_RESPONSE 0x0A97  DORMANT
 *   CMSG_MESSAGECHAT_SAY                           0x0A9A  ACTIVE
 *   CMSG_UNKNOWN_0x0AB2                            0x0AB2  DORMANT
 *   CMSG_UNKNOWN_0x0AB6                            0x0AB6  DOC
 *   CMSG_UNKNOWN_0x0ABA                            0x0ABA  DOC
 *   CMSG_MESSAGECHAT_OFFICER                       0x0ABF  ACTIVE
 *   CMSG_RESURRECT_RESPONSE                        0x0B0C  ACTIVE
 *   CMSG_GENERATE_RANDOM_CHARACTER_NAME            0x0B1C  ACTIVE
 *   CMSG_BATTLE_PAY_ACK_FAILED_RESPONSE            0x0B38  DOC
 *   CMSG_CONTACT_LIST                              0x0BB4  ACTIVE
 *   CMSG_CHANNEL_LIST                              0x0C1B  ACTIVE
 *   CMSG_UNKNOWN_0x0C1F                            0x0C1F  DOC
 *   CMSG_SET_TRADE_CURRENCY                        0x0C44  DOC
 *   CMSG_REFORGE_ITEM                              0x0C4F  DORMANT
 *   CMSG_CALENDAR_REMOVE_EVENT                     0x0C61  ACTIVE
 *   CMSG_UNKNOWN_0x0C62                            0x0C62  DOC
 *   CMSG_LF_GUILD_ADD_APPLICATION                  0x0C63  DOC
 *   CMSG_SET_SAVED_INSTANCE_EXTEND                 0x0C68  DORMANT
 *   CMSG_RESET_INSTANCES                           0x0C69  ACTIVE
 *   CMSG_GUILD_INFO_TEXT                           0x0C70  ACTIVE
 *   CMSG_UNKNOWN_0x0C79                            0x0C79  DOC
 *   CMSG_GUILD_SET_RANK                            0x0C7A  ACTIVE
 *   CMSG_MESSAGECHAT_GUILD                         0x0CAE  ACTIVE
 *   CMSG_GUILD_REPLACE_GUILD_MASTER                0x0CD0  DOC
 *   CMSG_GUILD_SWITCH_RANK                         0x0CD1  ACTIVE
 *   CMSG_GUILD_BANK_LOG_QUERY                      0x0CD3  DORMANT
 *   CMSG_GUILD_REMOVE                              0x0CD8  ACTIVE
 *   CMSG_GROUP_UNINVITE_GUID                       0x0CE1  ACTIVE
 *   CMSG_GUILD_SET_ACHIEVEMENT_TRACKING            0x0CF0  ACTIVE
 *   CMSG_QUERY_GUILD_MEMBERS_FOR_RECIPE            0x0CFA  DORMANT
 *   CMSG_READ_ITEM                                 0x0D00  DORMANT
 *   CMSG_ADD_IGNORE                                0x0D20  ACTIVE
 *   CMSG_GUILD_QUERY_RANKS                         0x0D50  ACTIVE
 *   CMSG_GROUP_INVITE_RESPONSE                     0x0D61  ACTIVE
 *   CMSG_GUILD_DISBAND                             0x0D73  ACTIVE
 *   CMSG_GUILD_DEL_RANK                            0x0D79  ACTIVE
 *   CMSG_BATTLE_PAY_GET_PRODUCT_LIST               0x0DE0  ACTIVE
 *   CMSG_LOOT_METHOD                               0x0DE1  ACTIVE
 *   CMSG_CHANNEL_KICK                              0x0E0B  DORMANT
 *   CMSG_UNKNOWN_0x0E0E                            0x0E0E  DOC
 *   CMSG_MESSAGECHAT_ADDON_GUILD                   0x0E3B  ACTIVE
 *   CMSG_MESSAGECHAT_AFK                           0x0EAB  ACTIVE
 *   CMSG_MESSAGECHAT_ADDON_WHISPER                 0x0EBB  ACTIVE
 *   CMSG_CHAR_CREATE                               0x0F1D  ACTIVE
 *   CMSG_TUTORIAL_CLEAR                            0x0F23  ACTIVE
 *   CMSG_AUTH_CONTINUED_SESSION                    0x0F49  DORMANT
 *   CMSG_PAGE_TEXT_QUERY                           0x1022  ACTIVE   [high-conf]
 *   CMSG_MESSAGECHAT_EMOTE                         0x103E  ACTIVE
 *   CMSG_ITEM_UPGRADE                              0x1042  DOC
 *   CMSG_MOVE_SET_FACING                           0x1050  ACTIVE
 *   CMSG_FORCE_MOVE_UNROOT_ACK                     0x1051  ACTIVE
 *   CMSG_MOVE_SET_CAN_FLY_ACK                      0x1052  ACTIVE
 *   CMSG_MOVE_START_STRAFE_RIGHT                   0x1058  ACTIVE
 *   CMSG_UNKNOWN_0x105A                            0x105A  DOC
 *   CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK        0x105B  ACTIVE
 *   CMSG_OBJECT_UPDATE_FAILED                      0x1061  ACTIVE
 *   CMSG_PET_BATTLE_FINAL_NOTIFY                   0x1063  DOC
 *   CMSG_FORCE_MOVE_ROOT_ACK                       0x107A  ACTIVE
 *   CMSG_MOVE_START_TURN_RIGHT                     0x107B  ACTIVE
 *   CMSG_SCENE_PLAYBACK_CANCELED                   0x1087  DOC
 *   CMSG_QUEUED_MESSAGES_END                       0x1093  DOC
 *   CMSG_MESSAGECHAT_PARTY                         0x109A  ACTIVE
 *   CMSG_UNKNOWN_0x10A2                            0x10A2  DOC
 *   CMSG_SET_PET_SLOT                              0x10A7  DORMANT
 *   CMSG_CHANNEL_INVITE                            0x10AB  DORMANT
 *   CMSG_UNKNOWN_0x10AE                            0x10AE  DOC
 *   CMSG_LOG_DISCONNECT                            0x10B3  ACTIVE
 *   CMSG_RESET_FACTION_CHEAT                       0x10B6  DORMANT
 *   CMSG_UNKNOWN_0x10BB                            0x10BB  DOC
 *   CMSG_QUEST_POI_QUERY                           0x10C2  ACTIVE
 *   CMSG_GUILD_REQUEST_PARTY_STATE                 0x10C3  ACTIVE
 *   CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK          0x10D1  ACTIVE
 *   MSG_MOVE_SET_SWIM_SPEED_CHEAT                  0x10D3  ACTIVE   server-binding=CMSG_TIME_SYNC_RESPONSE_DROPPED
 *   CMSG_MOVE_REMOVE_MOVEMENT_FORCE_ACK            0x10DB  DOC
 *   CMSG_UNKNOWN_0x10E3                            0x10E3  DOC      cand CMSG_CANCEL_MOUNT_AURA bound to 0x1552
 *   CMSG_MOVE_WATER_WALK_ACK                       0x10F2  ACTIVE
 *   CMSG_FORCE_RUN_SPEED_CHANGE_ACK                0x10F3  ACTIVE
 *   CMSG_DEL_FRIEND                                0x1103  ACTIVE
 *   CMSG_NEXT_CINEMATIC_CAMERA                     0x1124  ACTIVE
 *   CMSG_REQUEST_VEHICLE_SWITCH_SEAT               0x1143  DORMANT
 *   CMSG_MOVE_JUMP                                 0x1153  ACTIVE
 *   CMSG_MOVE_STOP_ASCEND                          0x115A  ACTIVE
 *   CMSG_DISCARDED_TIME_SYNC_ACKS                  0x115B  ACTIVE
 *   CMSG_UNKNOWN_0x1160                            0x1160  DOC
 *   CMSG_MOVE_STOP_TURN                            0x1170  ACTIVE
 *   CMSG_TABARD_VENDOR_ACTIVATE                    0x11C3  ACTIVE
 *   CMSG_MOVE_GRAVITY_ENABLE_ACK                   0x11D8  DORMANT
 *   CMSG_MOVE_SPLINE_DONE                          0x11D9  ACTIVE
 *   CMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY_ACK 0x11DB  DORMANT
 *   CMSG_MOVE_START_ASCEND                         0x11FA  ACTIVE
 *   CMSG_REQUEST_CATEGORY_COOLDOWNS                0x1203  ACTIVE   [high-conf]
 *   CMSG_UNKNOWN_0x1207                            0x1207  DOC
 *   CMSG_MESSAGECHAT_WHISPER                       0x123E  ACTIVE
 *   CMSG_BINDER_ACTIVATE                           0x1248  ACTIVE
 *   CMSG_QUEST_CONFIRM_ACCEPT                      0x124B  ACTIVE
 *   CMSG_GET_ITEM_PURCHASE_DATA                    0x1258  DORMANT
 *   CMSG_INSPECT                                   0x1259  ACTIVE
 *   CMSG_SET_LOOT_SPECIALIZATION                   0x1260  DOC
 *   CMSG_TOTEM_DESTROYED                           0x1263  ACTIVE
 *   CMSG_SHOWING_HELM                              0x126B  ACTIVE
 *   CMSG_MAIL_CREATE_TEXT_ITEM                     0x1270  ACTIVE
 *   CMSG_CANCEL_AUTO_REPEAT_SPELL                  0x1272  DORMANT
 *   CMSG_PETITION_DECLINE                          0x1279  DOC
 *   CMSG_BLACKMARKET_REQUEST_ITEMS                 0x127A  DOC
 *   CMSG_BATTLEFIELD_MGR_QUEUE_REQUEST             0x1283  DORMANT
 *   CMSG_UNKNOWN_0x129B                            0x129B  DOC
 *   CMSG_ENABLE_NAGLE                              0x12B3  DORMANT
 *   CMSG_INSTANCE_LOCK_RESPONSE                    0x12C0  DORMANT
 *   CMSG_BLACKMARKET_BID                           0x12C8  DOC
 *   CMSG_AUCTION_LIST_BIDDER_ITEMS                 0x12D0  DORMANT
 *   CMSG_AREA_SPIRIT_HEALER_QUEUE                  0x12D8  DORMANT
 *   CMSG_PETITION_BUY                              0x12D9  ACTIVE
 *   CMSG_PET_CANCEL_AURA                           0x12DA  DORMANT
 *   CMSG_DISMISS_CRITTER                           0x12DB  DORMANT
 *   CMSG_PET_SET_ACTION                            0x12E9  ACTIVE
 *   CMSG_RETURN_TO_GRAVEYARD                       0x12EA  ACTIVE
 *   CMSG_QUESTGIVER_QUERY_QUEST                    0x12F0  ACTIVE   [high-conf]
 *   CMSG_BUY_BANK_SLOT                             0x12F2  DORMANT
 *   CMSG_GOSSIP_HELLO                              0x12F3  ACTIVE
 *   CMSG_FAR_SIGHT                                 0x1341  ACTIVE
 *   CMSG_LOGOUT_REQUEST                            0x1349  ACTIVE   server-binding=CMSG_LOGOUT_REQUEST_IDLE
 *   CMSG_REPOP_REQUEST                             0x134A  ACTIVE
 *   CMSG_SELL_ITEM                                 0x1358  ACTIVE
 *   CMSG_REQUEST_PET_INFO                          0x135B  ACTIVE
 *   CMSG_COMPLETE_MOVIE                            0x1362  DORMANT
 *   CMSG_GUILD_BANK_SWAP_ITEMS                     0x136A  DORMANT
 *   CMSG_PETITION_SHOW_SIGNATURES                  0x136B  ACTIVE
 *   CMSG_QUEST_PUSH_RESULT                         0x1370  ACTIVE
 *   CMSG_MAIL_TAKE_ITEM                            0x1371  ACTIVE
 *   CMSG_GUILD_BANK_QUERY_TAB                      0x1372  ACTIVE
 *   CMSG_BATTLEFIELD_PORT                          0x1379  DORMANT
 *   CMSG_UNKNOWN_0x140A                            0x140A  DOC
 *   CMSG_CHANNEL_SET_OWNER                         0x141A  DORMANT
 *   CMSG_VOID_STORAGE_TRANSFER                     0x1440  DOC
 *   CMSG_CLEAR_RAID_MARKER                         0x1443  DORMANT
 *   CMSG_UNKNOWN_0x1446                            0x1446  DOC
 *   CMSG_ACCEPT_TRADE                              0x144D  DORMANT
 *   CMSG_LF_GUILD_POST_REQUEST                     0x1450  DORMANT
 *   CMSG_GET_CHALLENGE_MODE_REWARDS                0x1452  DOC
 *   CMSG_GUILD_ROSTER                              0x1459  ACTIVE
 *   CMSG_GUILD_PERMISSIONS                         0x145A  ACTIVE
 *   CMSG_GAMEOBJECT_QUERY                          0x1461  ACTIVE
 *   CMSG_LEARN_PET_SPECIALIZATION_GROUP            0x1463  DOC
 *   CMSG_GUILD_ACHIEVEMENT_MEMBERS                 0x1470  DORMANT
 *   CMSG_GUILD_MOTD                                0x1473  ACTIVE
 *   CMSG_GUILD_REQUEST_CHALLENGE_UPDATE            0x147A  ACTIVE
 *   CMSG_GUILD_DECLINE                             0x147B  ACTIVE
 *   CMSG_JOIN_CHANNEL                              0x148E  ACTIVE
 *   CMSG_PVP_LOG_DATA                              0x14C2  DORMANT
 *   CMSG_UNKNOWN_0x14D1                            0x14D1  DOC
 *   CMSG_GUILD_BANK_MONEY_WITHDRAWN                0x14DB  ACTIVE
 *   CMSG_BUSY_TRADE                                0x14E0  DORMANT
 *   CMSG_MAIL_DELETE                               0x14E2  ACTIVE
 *   CMSG_SET_TRADE_GOLD                            0x14E3  ACTIVE
 *   CMSG_LF_GUILD_DECLINE_RECRUIT                  0x14F3  DOC
 *   CMSG_BATTLE_PET_LEARN                          0x1540  DOC
 *   CMSG_CANCEL_MOUNT_AURA                         0x1552  DORMANT
 *   CMSG_GUILD_DEMOTE                              0x1553  ACTIVE
 *   CMSG_CAGE_BATTLE_PET                           0x1561  DOC
 *   CMSG_UNKNOWN_0x1570                            0x1570  DOC
 *   CMSG_RAID_READY_CHECK_CONFIRM                  0x158B  ACTIVE
 *   CMSG_REQUEST_HOTFIX                            0x158D  ACTIVE
 *   CMSG_PLAYER_LOGIN                              0x158F  ACTIVE
 *   CMSG_LF_GUILD_BROWSE                           0x159A  DOC
 *   CMSG_GM_UPDATE_TICKET_STATUS                   0x15A8  ACTIVE
 *   CMSG_VOICE_SESSION_ENABLE                      0x15A9  ACTIVE
 *   CMSG_UNKNOWN_0x15AA                            0x15AA  DOC
 *   CMSG_UI_TIME_REQUEST                           0x15AB  ACTIVE
 *   CMSG_UNKNOWN_0x15AF                            0x15AF  DOC
 *   CMSG_UNKNOWN_0x15BA                            0x15BA  DOC
 *   CMSG_GROUP_SET_LEADER                          0x15BB  ACTIVE
 *   CMSG_OFFER_PETITION                            0x15BE  ACTIVE
 *   CMSG_LOOT_ROLL                                 0x15C2  ACTIVE
 *   CMSG_GUILD_EVENT_LOG_QUERY                     0x15D9  ACTIVE
 *   CMSG_CHALLENGE_MODE_REQUEST_LEADERS            0x15DB  DOC
 *   CMSG_REQUEST_RESEARCH_HISTORY                  0x15E2  DOC
 *   CMSG_UNKNOWN_0x160F                            0x160F  DOC
 *   CMSG_MESSAGECHAT_INSTANCE                      0x162A  ACTIVE
 *   CMSG_MESSAGECHAT_RAID_WARNING                  0x16AB  ACTIVE
 *   CMSG_UNKNOWN_0x1788                            0x1788  DOC
 *   CMSG_UNKNOWN_0x1789                            0x1789  DOC
 *   CMSG_GROUP_REQUEST_JOIN_UPDATES                0x178A  ACTIVE
 *   CMSG_UNKNOWN_0x178C                            0x178C  DOC
 *   CMSG_GROUP_DISBAND                             0x1798  ACTIVE
 *   CMSG_GROUP_CHANGE_SUB_GROUP                    0x1799  ACTIVE
 *   CMSG_UNKNOWN_0x179D                            0x179D  DOC
 *   CMSG_SHOW_TRADE_SKILL                          0x179F  DOC
 *   CMSG_UNKNOWN_0x17AA                            0x17AA  DOC
 *   CMSG_BATTLE_PET_SET_FLAGS                      0x17AC  DOC
 *   CMSG_PET_BATTLE_INPUT                          0x17BA  DOC
 *   CMSG_LFG_BOOT_PLAYER_VOTE                      0x17BE  ACTIVE
 *   CMSG_SET_PARTY_ASSIGNMENT                      0x1802  ACTIVE
 *   CMSG_BATTLEFIELD_MANAGER_ENTRY_INVITE_RESPONSE 0x1806  DORMANT
 *   CMSG_MESSAGECHAT_ADDON_OFFICER                 0x180B  ACTIVE
 *   CMSG_WARDEN_DATA                               0x1816  ACTIVE
 *   CMSG_UNLEARN_SPECIALIZATION                    0x1841  DORMANT
 *   CMSG_FORCE_SWIM_SPEED_CHANGE_ACK               0x1853  ACTIVE
 *   CMSG_MOVE_START_SWIM                           0x1858  DORMANT
 *   CMSG_FORCE_TURN_RATE_CHANGE_ACK                0x185A  DORMANT
 *   CMSG_SET_VEHICLE_REC_ID_ACK                    0x185B  DORMANT
 *   CMSG_CANCEL_AURA                               0x1861  ACTIVE
 *   CMSG_GROUP_INITIATE_ROLE_POLL                  0x1882  ACTIVE
 *   CMSG_BATTLE_PAY_START_PURCHASE                 0x1886  DOC
 *   CMSG_BATTLE_PET_MODIFY_NAME                    0x1887  DOC
 *   CMSG_BATTLE_PET_SUMMON_COMPANION               0x1896  DOC
 *   CMSG_GROUP_ASSISTANT_LEADER                    0x1897  ACTIVE
 *   CMSG_GUILD_ACCEPT                              0x18A2  ACTIVE
 *   CMSG_WHO                                       0x18A3  ACTIVE
 *   CMSG_BATTLE_PAY_GET_PURCHASE_LIST              0x18B2  ACTIVE
 *   CMSG_BATTLE_PET_DELETE_PET                     0x18B6  DOC
 *   CMSG_CANCEL_CAST                               0x18C0  DORMANT
 *   CMSG_EMOTE                                     0x1924  ACTIVE
 *   CMSG_CANCEL_TRADE                              0x1941  ACTIVE
 *   CMSG_PET_BATTLE_QUEUE_PROPOSE_MATCH_RESULT     0x19C2  DOC
 *   CMSG_INSPECT_HONOR_STATS                       0x19C3  DORMANT
 *   CMSG_UNKNOWN_0x19E3                            0x19E3  DOC
 *   CMSG_PET_BATTLE_SET_FRONT_PET                  0x1A07  DOC
 *   CMSG_QUERY_REALM_NAME                          0x1A16  ACTIVE   server-binding=CMSG_REALM_NAME_QUERY
 *   CMSG_QUERY_SCENARIO_POI                        0x1A22  DOC
 *   CMSG_GMTICKET_DELETETICKET                     0x1A23  DORMANT
 *   CMSG_DUEL_PROPOSED                             0x1A26  DOC
 *   CMSG_SET_DUNGEON_DIFFICULTY                    0x1A36  ACTIVE
 *   CMSG_UNKNOWN_0x1A37                            0x1A37  DOC
 *   CMSG_GUILD_SET_GUILD_MASTER                    0x1A83  DOC
 *   CMSG_GMTICKET_CREATE                           0x1A86  ACTIVE
 *   CMSG_KEEP_ALIVE                                0x1A87  ACTIVE
 *   CMSG_GROUP_SET_ROLES                           0x1A92  ACTIVE
 *   CMSG_CALENDAR_COPY_EVENT                       0x1A97  ACTIVE
 *   CMSG_UNKNOWN_0x1AA2                            0x1AA2  ACTIVE   server-binding=CMSG_LFG_LFR_JOIN
 *   CMSG_LFG_TELEPORT                              0x1AA6  ACTIVE
 *   CMSG_UNKNOWN_0x1AA7                            0x1AA7  DOC
 *   CMSG_CALENDAR_EVENT_STATUS                     0x1AB3  ACTIVE
 *   CMSG_GUILD_QUERY                               0x1AB6  ACTIVE
 *   CMSG_BATTLEFIELD_LIST                          0x1C41  DORMANT
 *   CMSG_AREATRIGGER                               0x1C44  ACTIVE   [high-conf]
 *   CMSG_PET_BATTLE_QUIT_NOTIFY                    0x1C45  DOC
 *   CMSG_LF_GUILD_REMOVE_APPLICATION               0x1C53  DOC
 *   CMSG_GUILD_QUERY_NEWS                          0x1C58  DOC
 *   CMSG_CHALLENGE_MODE_REQUEST_MAP_STATS          0x1C5A  DOC
 *   CMSG_PET_NAME_QUERY                            0x1C62  ACTIVE
 *   CMSG_UNKNOWN_0x1C63                            0x1C63  DOC
 *   CMSG_USE_ITEM                                  0x1CC1  ACTIVE
 *   CMSG_BATTLE_PET_QUERY_NAME                     0x1CE0  DOC
 *   CMSG_LOOT                                      0x1CE2  ACTIVE
 *   CMSG_BEGIN_TRADE                               0x1CE3  ACTIVE
 *   CMSG_OPEN_ITEM                                 0x1D10  DORMANT
 *   CMSG_SAVE_GUILD_EMBLEM                         0x1D60  ACTIVE
 *   CMSG_RESET_CHALLENGE_MODE                      0x1D61  DOC
 *   CMSG_REQUEST_ACCOUNT_DATA                      0x1D8A  ACTIVE
 *   CMSG_UPDATE_CLIENT_SETTINGS                    0x1D8D  DOC
 *   CMSG_CALENDAR_EVENT_INVITE                     0x1D8E  ACTIVE
 *   CMSG_UNKNOWN_0x1D9B                            0x1D9B  DOC
 *   CMSG_LFG_PROPOSAL_RESPONSE                     0x1D9D  ACTIVE
 *   CMSG_LF_GUILD_SET_GUILD_POST                   0x1D9F  DOC
 *   CMSG_QUEST_NPC_QUERY                           0x1DAE  ACTIVE
 *   CMSG_UNKNOWN_0x1DB9                            0x1DB9  DOC
 *   CMSG_SEND_MAIL                                 0x1DBA  ACTIVE
 *   CMSG_LOAD_SCREEN                               0x1DBD  ACTIVE
 *   CMSG_REQUEST_VEHICLE_EXIT                      0x1DC3  ACTIVE
 *   CMSG_CANCEL_MOD_SPEED_NO_CONTROL_AURAS         0x1DE0  DOC
 *   CMSG_LOOT_MASTER_GIVE                          0x1DE1  DORMANT
 *   CMSG_COMPLETE_CINEMATIC                        0x1F34  ACTIVE
 *   CMSG_UNKNOWN_0x1F88                            0x1F88  DOC
 *   CMSG_GMTICKET_GETTICKET                        0x1F89  ACTIVE
 *   CMSG_SET_ACTION_BUTTON                         0x1F8C  DORMANT
 *   CMSG_CALENDAR_UPDATE_EVENT                     0x1F8D  ACTIVE
 *   CMSG_CHANGEPLAYER_DIFFICULTY                   0x1F8E  DORMANT
 *   CMSG_CALENDAR_COMPLAIN                         0x1F8F  ACTIVE
 *   CMSG_PETITION_RENAME                           0x1F9A  DOC
 *   CMSG_BATTLEFIELD_STATUS                        0x1F9E  ACTIVE
 *   CMSG_CALENDAR_GET_CALENDAR                     0x1F9F  ACTIVE
 *   CMSG_MAIL_RETURN_TO_SENDER                     0x1FA8  ACTIVE
 *   CMSG_UNKNOWN_0x1FAD                            0x1FAD  ACTIVE   server-binding=MSG_MOVE_WORLDPORT_ACK
 *   CMSG_CALENDAR_EVENT_RSVP                       0x1FB8  ACTIVE
 *   CMSG_UNKNOWN_0x1FB9                            0x1FB9  DOC
 *   CMSG_CORPSE_TRANSPORT_QUERY                    0x1FBE  ACTIVE   server-binding=CMSG_CORPSE_QUERY
 */

// Inert declarations for DOC rows (client vocabulary absent from Opcodes.h).
// enum class => scoped, so these can never collide with the real OpcodesList.
enum class OpcodesReferenceDoc : uint16
{
    CMSG_SET_ACHIEVEMENTS_HIDDEN                     = 0x0002,
    CMSG_NEUTRAL_PLAYER_SELECT_FACTION               = 0x0027,
    CMSG_VIOLENCE_LEVEL                              = 0x0040,
    CMSG_TIME_SYNC_RESPONSE_FAILED                   = 0x0058,
    CMSG_UNKNOWN_0x0060                              = 0x0060,
    CMSG_UNKNOWN_0x0069                              = 0x0069,
    CMSG_LFD_LOCK_INFO_REQUEST                       = 0x006B,
    CMSG_UNKNOWN_0x0071                              = 0x0071,
    CMSG_SCENE_COMPLETED                             = 0x0087,
    CMSG_UNKNOWN_0x00B3                              = 0x00B3,
    CMSG_UNKNOWN_0x00E3                              = 0x00E3,
    CMSG_UNKNOWN_0x00F0                              = 0x00F0,
    CMSG_QUERY_VOID_STORAGE                          = 0x0140,
    CMSG_UNKNOWN_0x0160                              = 0x0160,
    CMSG_BATTLE_PET_SET_BATTLE_SLOT                  = 0x0163,
    CMSG_UNKNOWN_0x017A                              = 0x017A,
    CMSG_UNKNOWN_0x01C0                              = 0x01C0,
    CMSG_SET_EVERYONE_IS_ASSISTANT                   = 0x01E1,
    CMSG_SET_LFG_BONUS_FACTION_ID                    = 0x0247,
    CMSG_KEYBOUND_OVERRIDE                           = 0x0264,
    CMSG_UNKNOWN_0x0274                              = 0x0274,
    CMSG_CONFIRM_RESPEC_WIPE                         = 0x0275,
    CMSG_SUSPEND_TOKEN_RESPONSE                      = 0x0292,
    CMSG_UNREGISTER_ALL_ADDON_PREFIXES               = 0x029F,
    CMSG_SCENE_TRIGGER_EVENT                         = 0x02C4,
    CMSG_SET_PVP                                     = 0x02C5,
    CMSG_PET_BATTLE_REQUEST_PVP                      = 0x02C7,
    CMSG_LIST_PETS                                   = 0x02CA,
    CMSG_UNKNOWN_0x02D6                              = 0x02D6,
    CMSG_SUBMIT_COMPLAIN                             = 0x030D,
    CMSG_UNKNOWN_0x031D                              = 0x031D,
    CMSG_GM_RESPONSE_RESOLVE                         = 0x033D,
    CMSG_BATTLEFIELD_MGR_EXIT_REQUEST                = 0x0343,
    CMSG_REQUEST_CONQUEST_FORMULA_CONSTANTS          = 0x0365,
    CMSG_USED_FOLLOW                                 = 0x0374,
    CMSG_PET_BATTLE_REQUEST_UPDATE                   = 0x0377,
    CMSG_AUCTION_HELLO                               = 0x0379,
    CMSG_DUEL_RESPONSE                               = 0x03E2,
    CMSG_ADDON_REGISTERED_PREFIXES                   = 0x040E,
    CMSG_UNLOCK_VOID_STORAGE                         = 0x0444,
    CMSG_CHOICE_RESPONSE                             = 0x0447,
    CMSG_QUERY_COUNTDOWN_TIMER                       = 0x044E,
    CMSG_UNKNOWN_0x0462                              = 0x0462,
    CMSG_UNKNOWN_0x049B                              = 0x049B,
    CMSG_GUILD_NEWS_UPDATE_STICKY                    = 0x04D1,
    CMSG_GUILD_BANK_NOTE                             = 0x04D9,
    CMSG_UNKNOWN_0x04DB                              = 0x04DB,
    CMSG_LF_GUILD_GET_APPLICATIONS                   = 0x0558,
    CMSG_LF_GUILD_GET_RECRUITS                       = 0x057A,
    CMSG_SET_RAID_DIFFICULTY                         = 0x0591,
    CMSG_GUILD_ASSIGN_MEMBER_RANK                    = 0x05D0,
    CMSG_BATTLE_PET_REQUEST_JOURNAL_LOCK             = 0x05E1,
    CMSG_QUERY_GUILD_XP                              = 0x05F8,
    CMSG_UNKNOWN_0x0643                              = 0x0643,
    CMSG_SET_FACTION_NOTATWAR                        = 0x064B,
    CMSG_SWAP_VOID_ITEM                              = 0x0655,
    CMSG_UNKNOWN_0x0656                              = 0x0656,
    CMSG_BATTLE_PET_UPDATE_NOTIFY                    = 0x0675,
    CMSG_QUERY_GUILD_REWARDS                         = 0x06C4,
    CMSG_BATTLE_PET_WILD_REQUEST                     = 0x06C5,
    CMSG_SET_PRIMARY_TALENT_TREE                     = 0x06C6,
    CMSG_JOIN_PET_BATTLE_QUEUE                       = 0x06D4,
    CMSG_TRANSMOGRIFY_ITEMS                          = 0x06D7,
    CMSG_REQUEST_CEMETERY_LIST                       = 0x06E4,
    CMSG_SAVE_CUF_PROFILES                           = 0x06E6,
    CMSG_REQUEST_FORCED_REACTIONS                    = 0x06F5,
    CMSG_UNKNOWN_0x0719                              = 0x0719,
    CMSG_UNKNOWN_0x073D                              = 0x073D,
    CMSG_UNKNOWN_0x0743                              = 0x0743,
    CMSG_BLACKMARKET_HELLO                           = 0x075A,
    CMSG_MAIL_QUERY_NEXT_TIME                        = 0x077B,
    CMSG_UNKNOWN_0x07EB                              = 0x07EB,
    CMSG_BATTLE_PAY_CONFIRM_PURCHASE_RESPONSE        = 0x0812,
    CMSG_DO_READY_CHECK                              = 0x0817,
    CMSG_MINIMAP_PING                                = 0x0837,
    CMSG_SUBMIT_BUG                                  = 0x0861,
    CMSG_UNKNOWN_0x0870                              = 0x0870,
    CMSG_MOVE_SET_CAN_TURN_WHILE_FALLING_ACK         = 0x0873,
    CMSG_UNKNOWN_0x087A                              = 0x087A,
    CMSG_RAID_TARGET_UPDATE                          = 0x0886,
    CMSG_UNKNOWN_0x0896                              = 0x0896,
    CMSG_RANDOM_ROLL                                 = 0x08A3,
    CMSG_LEAVE_PET_BATTLE_QUEUE                      = 0x08C1,
    CMSG_UNKNOWN_0x08D1                              = 0x08D1,
    CMSG_MOVE_APPLY_MOVEMENT_FORCE_ACK               = 0x08D3,
    CMSG_UNKNOWN_0x08E1                              = 0x08E1,
    CMSG_UNKNOWN_0x08E2                              = 0x08E2,
    CMSG_BATTLE_CHAR_BOOST                           = 0x08E3,
    CMSG_UNKNOWN_0x0942                              = 0x0942,
    CMSG_UNKNOWN_0x0973                              = 0x0973,
    CMSG_UNKNOWN_0x0979                              = 0x0979,
    CMSG_UNKNOWN_0x09E3                              = 0x09E3,
    CMSG_SUGGESTION_SUBMIT                           = 0x0A12,
    CMSG_CORPSE_MAP_POSITION_QUERY                   = 0x0A16,
    CMSG_BATTLE_PET_REQUEST_JOURNAL                  = 0x0A23,
    CMSG_UNKNOWN_0x0A8F                              = 0x0A8F,
    CMSG_UNKNOWN_0x0AB2                              = 0x0AB2,
    CMSG_UNKNOWN_0x0AB6                              = 0x0AB6,
    CMSG_UNKNOWN_0x0ABA                              = 0x0ABA,
    CMSG_GENERATE_RANDOM_CHARACTER_NAME              = 0x0B1C,
    CMSG_BATTLE_PAY_ACK_FAILED_RESPONSE              = 0x0B38,
    CMSG_UNKNOWN_0x0C1F                              = 0x0C1F,
    CMSG_SET_TRADE_CURRENCY                          = 0x0C44,
    CMSG_UNKNOWN_0x0C62                              = 0x0C62,
    CMSG_LF_GUILD_ADD_APPLICATION                    = 0x0C63,
    CMSG_UNKNOWN_0x0C79                              = 0x0C79,
    CMSG_GUILD_REPLACE_GUILD_MASTER                  = 0x0CD0,
    CMSG_GUILD_SET_ACHIEVEMENT_TRACKING              = 0x0CF0,
    CMSG_BATTLE_PAY_GET_PRODUCT_LIST                 = 0x0DE0,
    CMSG_UNKNOWN_0x0E0E                              = 0x0E0E,
    CMSG_ITEM_UPGRADE                                = 0x1042,
    CMSG_UNKNOWN_0x105A                              = 0x105A,
    CMSG_PET_BATTLE_FINAL_NOTIFY                     = 0x1063,
    CMSG_SCENE_PLAYBACK_CANCELED                     = 0x1087,
    CMSG_QUEUED_MESSAGES_END                         = 0x1093,
    CMSG_UNKNOWN_0x10A2                              = 0x10A2,
    CMSG_UNKNOWN_0x10AE                              = 0x10AE,
    CMSG_UNKNOWN_0x10BB                              = 0x10BB,
    CMSG_GUILD_REQUEST_PARTY_STATE                   = 0x10C3,
    CMSG_MOVE_REMOVE_MOVEMENT_FORCE_ACK              = 0x10DB,
    CMSG_UNKNOWN_0x10E3                              = 0x10E3,
    CMSG_DISCARDED_TIME_SYNC_ACKS                    = 0x115B,
    CMSG_UNKNOWN_0x1160                              = 0x1160,
    CMSG_TABARD_VENDOR_ACTIVATE                      = 0x11C3,
    CMSG_REQUEST_CATEGORY_COOLDOWNS                  = 0x1203,
    CMSG_UNKNOWN_0x1207                              = 0x1207,
    CMSG_SET_LOOT_SPECIALIZATION                     = 0x1260,
    CMSG_PETITION_DECLINE                            = 0x1279,
    CMSG_BLACKMARKET_REQUEST_ITEMS                   = 0x127A,
    CMSG_UNKNOWN_0x129B                              = 0x129B,
    CMSG_BLACKMARKET_BID                             = 0x12C8,
    CMSG_QUEST_PUSH_RESULT                           = 0x1370,
    CMSG_UNKNOWN_0x140A                              = 0x140A,
    CMSG_VOID_STORAGE_TRANSFER                       = 0x1440,
    CMSG_UNKNOWN_0x1446                              = 0x1446,
    CMSG_GET_CHALLENGE_MODE_REWARDS                  = 0x1452,
    CMSG_LEARN_PET_SPECIALIZATION_GROUP              = 0x1463,
    CMSG_GUILD_REQUEST_CHALLENGE_UPDATE              = 0x147A,
    CMSG_UNKNOWN_0x14D1                              = 0x14D1,
    CMSG_LF_GUILD_DECLINE_RECRUIT                    = 0x14F3,
    CMSG_BATTLE_PET_LEARN                            = 0x1540,
    CMSG_CAGE_BATTLE_PET                             = 0x1561,
    CMSG_UNKNOWN_0x1570                              = 0x1570,
    CMSG_RAID_READY_CHECK_CONFIRM                    = 0x158B,
    CMSG_LF_GUILD_BROWSE                             = 0x159A,
    CMSG_UNKNOWN_0x15AA                              = 0x15AA,
    CMSG_UNKNOWN_0x15AF                              = 0x15AF,
    CMSG_UNKNOWN_0x15BA                              = 0x15BA,
    CMSG_CHALLENGE_MODE_REQUEST_LEADERS              = 0x15DB,
    CMSG_REQUEST_RESEARCH_HISTORY                    = 0x15E2,
    CMSG_UNKNOWN_0x160F                              = 0x160F,
    CMSG_UNKNOWN_0x1788                              = 0x1788,
    CMSG_UNKNOWN_0x1789                              = 0x1789,
    CMSG_UNKNOWN_0x178C                              = 0x178C,
    CMSG_UNKNOWN_0x179D                              = 0x179D,
    CMSG_SHOW_TRADE_SKILL                            = 0x179F,
    CMSG_UNKNOWN_0x17AA                              = 0x17AA,
    CMSG_BATTLE_PET_SET_FLAGS                        = 0x17AC,
    CMSG_PET_BATTLE_INPUT                            = 0x17BA,
    CMSG_SET_PARTY_ASSIGNMENT                        = 0x1802,
    CMSG_GROUP_INITIATE_ROLE_POLL                    = 0x1882,
    CMSG_BATTLE_PAY_START_PURCHASE                   = 0x1886,
    CMSG_BATTLE_PET_MODIFY_NAME                      = 0x1887,
    CMSG_BATTLE_PET_SUMMON_COMPANION                 = 0x1896,
    CMSG_BATTLE_PAY_GET_PURCHASE_LIST                = 0x18B2,
    CMSG_BATTLE_PET_DELETE_PET                       = 0x18B6,
    CMSG_PET_BATTLE_QUEUE_PROPOSE_MATCH_RESULT       = 0x19C2,
    CMSG_UNKNOWN_0x19E3                              = 0x19E3,
    CMSG_PET_BATTLE_SET_FRONT_PET                    = 0x1A07,
    CMSG_QUERY_REALM_NAME                            = 0x1A16,
    CMSG_QUERY_SCENARIO_POI                          = 0x1A22,
    CMSG_DUEL_PROPOSED                               = 0x1A26,
    CMSG_SET_DUNGEON_DIFFICULTY                      = 0x1A36,
    CMSG_UNKNOWN_0x1A37                              = 0x1A37,
    CMSG_GUILD_SET_GUILD_MASTER                      = 0x1A83,
    CMSG_GROUP_SET_ROLES                             = 0x1A92,
    CMSG_UNKNOWN_0x1AA2                              = 0x1AA2,
    CMSG_UNKNOWN_0x1AA7                              = 0x1AA7,
    CMSG_PET_BATTLE_QUIT_NOTIFY                      = 0x1C45,
    CMSG_LF_GUILD_REMOVE_APPLICATION                 = 0x1C53,
    CMSG_GUILD_QUERY_NEWS                            = 0x1C58,
    CMSG_CHALLENGE_MODE_REQUEST_MAP_STATS            = 0x1C5A,
    CMSG_UNKNOWN_0x1C63                              = 0x1C63,
    CMSG_BATTLE_PET_QUERY_NAME                       = 0x1CE0,
    CMSG_SAVE_GUILD_EMBLEM                           = 0x1D60,
    CMSG_RESET_CHALLENGE_MODE                        = 0x1D61,
    CMSG_UPDATE_CLIENT_SETTINGS                      = 0x1D8D,
    CMSG_UNKNOWN_0x1D9B                              = 0x1D9B,
    CMSG_LF_GUILD_SET_GUILD_POST                     = 0x1D9F,
    CMSG_QUEST_NPC_QUERY                             = 0x1DAE,
    CMSG_UNKNOWN_0x1DB9                              = 0x1DB9,
    CMSG_CANCEL_MOD_SPEED_NO_CONTROL_AURAS           = 0x1DE0,
    CMSG_UNKNOWN_0x1F88                              = 0x1F88,
    CMSG_PETITION_RENAME                             = 0x1F9A,
    CMSG_UNKNOWN_0x1FAD                              = 0x1FAD,
    CMSG_UNKNOWN_0x1FB9                              = 0x1FB9,
    SMSG_UNKNOWN_0x0002                              = 0x0002,
    SMSG_UNKNOWN_0x0003                              = 0x0003,
    SMSG_UNKNOWN_0x001B                              = 0x001B,
    SMSG_UNKNOWN_0x001E                              = 0x001E,
    SMSG_UNKNOWN_0x002B                              = 0x002B,
    SMSG_UNKNOWN_0x002F                              = 0x002F,
    SMSG_UNKNOWN_0x003B                              = 0x003B,
    SMSG_UNKNOWN_0x003F                              = 0x003F,
    SMSG_MOVE_UPDATE_WALK_SPEED                      = 0x0047,
    SMSG_UNKNOWN_0x0050                              = 0x0050,
    SMSG_UNKNOWN_0x006B                              = 0x006B,
    SMSG_SOR_START_EXPERIENCE_INCOMPLETE             = 0x0083,
    SMSG_UNKNOWN_0x0086                              = 0x0086,
    SMSG_RAID_MARKERS_CHANGED                        = 0x008A,
    SMSG_VOID_STORAGE_CONTENTS                       = 0x008B,
    SMSG_UNKNOWN_0x009B                              = 0x009B,
    SMSG_DISPLAY_PROMOTION                           = 0x00A3,
    SMSG_PET_BATTLE_QUEUE_STATUS                     = 0x00A6,
    SMSG_INSTANCE_LOCK_WARNING_QUERY                 = 0x00A7,
    SMSG_UNKNOWN_0x00AB                              = 0x00AB,
    SMSG_BLACK_MARKET_OPEN_RESULT                    = 0x00AE,
    SMSG_UNKNOWN_0x00AF                              = 0x00AF,
    SMSG_UNKNOWN_0x00BA                              = 0x00BA,
    SMSG_MAP_OBJ_EVENTS                              = 0x00BB,
    SMSG_ARENA_PREP_OPPONENT_SPECIALIZATIONS         = 0x00BE,
    SMSG_UNKNOWN_0x00BF                              = 0x00BF,
    SMSG_SPELL_EXECUTE_LOG                           = 0x00D8,
    SMSG_MOVE_UPDATE_FLIGHT_SPEED                    = 0x00E1,
    SMSG_UNKNOWN_0x00F2                              = 0x00F2,
    SMSG_UNKNOWN_0x00F3                              = 0x00F3,
    SMSG_UNKNOWN_0x00F9                              = 0x00F9,
    SMSG_UNKNOWN_0x0115                              = 0x0115,
    SMSG_UNKNOWN_0x0170                              = 0x0170,
    SMSG_CATEGORY_COOLDOWN                           = 0x01DB,
    SMSG_MOVE_UPDATE_SWIM_SPEED                      = 0x01E2,
    SMSG_UNKNOWN_0x0202                              = 0x0202,
    SMSG_BATTLE_PET_JOURNAL_LOCK_DENINED             = 0x0203,
    SMSG_GM_TICKET_RESPONSE                          = 0x0207,
    SMSG_UNKNOWN_0x020A                              = 0x020A,
    SMSG_BATTLE_PAY_DELIVERY_ENDED                   = 0x020B,
    SMSG_UNKNOWN_0x020E                              = 0x020E,
    SMSG_UNKNOWN_0x020F                              = 0x020F,
    SMSG_UNKNOWN_0x021B                              = 0x021B,
    SMSG_UNKNOWN_0x021F                              = 0x021F,
    SMSG_UNKNOWN_0x0222                              = 0x0222,
    SMSG_UNKNOWN_0x0226                              = 0x0226,
    SMSG_UNKNOWN_0x022B                              = 0x022B,
    SMSG_UNKNOWN_0x022E                              = 0x022E,
    SMSG_PET_BATTLE_REQUEST_FAILED                   = 0x022F,
    SMSG_BATTLE_PAY_GET_PURCHASE_LIST_RESPONSE       = 0x023A,
    SMSG_UNKNOWN_0x023B                              = 0x023B,
    SMSG_WEEKLY_RESET_CURRENCY                       = 0x023E,
    SMSG_UNKNOWN_0x023F                              = 0x023F,
    SMSG_MOVE_UPDATE_SWIM_BACK_SPEED                 = 0x025A,
    SMSG_RAID_TARGET_UPDATE_ALL                      = 0x0283,
    SMSG_PETITION_ALREADY_SIGNED                     = 0x0286,
    SMSG_UNKNOWN_0x028A                              = 0x028A,
    SMSG_UNKNOWN_0x029B                              = 0x029B,
    SMSG_UNKNOWN_0x029F                              = 0x029F,
    SMSG_UNKNOWN_0x02A3                              = 0x02A3,
    SMSG_GM_TICKET_UPDATE                            = 0x02A6,
    SMSG_RESPEC_WIPE_CONFIRM                         = 0x02AB,
    SMSG_MESSAGE_BOX                                 = 0x02AE,
    SMSG_RAID_READY_CHECK_CONFIRM                    = 0x02AF,
    SMSG_UNKNOWN_0x02BA                              = 0x02BA,
    SMSG_UNKNOWN_0x02BB                              = 0x02BB,
    SMSG_UNKNOWN_0x02BE                              = 0x02BE,
    SMSG_UNKNOWN_0x02EF                              = 0x02EF,
    SMSG_MOVE_REMOVE_MOVEMENT_FORCE                  = 0x0341,
    SMSG_UNKNOWN_0x0354                              = 0x0354,
    SMSG_UNKNOWN_0x0364                              = 0x0364,
    SMSG_MOVE_UPDATE_FLIGHT_BACK_SPEED               = 0x036A,
    SMSG_QUEST_NPC_QUERY_RESPONSE                    = 0x036D,
    SMSG_UNKNOWN_0x03EC                              = 0x03EC,
    SMSG_UNKNOWN_0x040A                              = 0x040A,
    SMSG_UNKNOWN_0x040F                              = 0x040F,
    SMSG_UNKNOWN_0x0413                              = 0x0413,
    SMSG_BATTLE_PET_PET_UPDATES                      = 0x041A,
    SMSG_UNKNOWN_0x041B                              = 0x041B,
    SMSG_UNKNOWN_0x041E                              = 0x041E,
    SMSG_INSPECT_RATED_BG_STATS                      = 0x041F,
    SMSG_REQUEST_CEMETERY_LIST_RESPONSE              = 0x042A,
    SMSG_UNKNOWN_0x042F                              = 0x042F,
    SMSG_UNKNOWN_0x043A                              = 0x043A,
    SMSG_UNKNOWN_0x043B                              = 0x043B,
    SMSG_BATTLE_PAY_GET_DISTRIBUTION_LIST_RESPONSE   = 0x043F,
    SMSG_UNKNOWN_0x0470                              = 0x0470,
    SMSG_CALENDAR_EVENT_MODERATOR_STATUS             = 0x048F,
    SMSG_UNKNOWN_0x049A                              = 0x049A,
    SMSG_UNKNOWN_0x049F                              = 0x049F,
    SMSG_UNKNOWN_0x04AA                              = 0x04AA,
    SMSG_UNKNOWN_0x04AE                              = 0x04AE,
    SMSG_PET_BATTLE_FINISHED                         = 0x04BB,
    SMSG_MIRROR_IMAGE_CREATURE_DATA                  = 0x04D0,
    SMSG_MIRROR_IMAGE_COMPONENTED_DATA               = 0x04D9,
    SMSG_UNKNOWN_0x04F0                              = 0x04F0,
    SMSG_UNKNOWN_0x0569                              = 0x0569,
    SMSG_SET_RAID_DIFFICULTY                         = 0x0591,
    SMSG_UNKNOWN_0x05D8                              = 0x05D8,
    SMSG_WAIT_QUEUE_FINISH                           = 0x060E,
    SMSG_UNKNOWN_0x060F                              = 0x060F,
    SMSG_BATTLE_PAY_START_PURCHASE_RESPONSE          = 0x0612,
    SMSG_PET_BATTLE_FIRST_ROUND                      = 0x0613,
    SMSG_UNKNOWN_0x061F                              = 0x061F,
    SMSG_CLEAR_BOSS_EMOTES                           = 0x062B,
    SMSG_UNKNOWN_0x062E                              = 0x062E,
    SMSG_UNKNOWN_0x0633                              = 0x0633,
    SMSG_REALM_NAME_QUERY_RESPONSE                   = 0x063E,
    SMSG_UNKNOWN_0x068B                              = 0x068B,
    SMSG_UNKNOWN_0x069A                              = 0x069A,
    SMSG_PVP_SEASON                                  = 0x069B,
    SMSG_UNKNOWN_0x069E                              = 0x069E,
    SMSG_SET_PLAY_HOVER_ANIM                         = 0x069F,
    SMSG_UNKNOWN_0x06BA                              = 0x06BA,
    SMSG_UNKNOWN_0x06BE                              = 0x06BE,
    SMSG_QUEST_PUSH_RESULT                           = 0x074D,
    SMSG_UNKNOWN_0x07C5                              = 0x07C5,
    SMSG_UNKNOWN_0x07F5                              = 0x07F5,
    SMSG_BATTLEFIELD_MGR_ENTERED                     = 0x081B,
    SMSG_PETITION_RENAME_RESULT                      = 0x082A,
    SMSG_PET_BATTLE_FINALIZE_LOCATION                = 0x082E,
    SMSG_BATTLEFIELD_MGR_STATE_CHANGE                = 0x083A,
    SMSG_CHARACTER_UPGRADE_COMPLETE                  = 0x083B,
    SMSG_UNKNOWN_0x083E                              = 0x083E,
    SMSG_UNKNOWN_0x083F                              = 0x083F,
    SMSG_MOVE_SET_VEHICLE_REC_ID                     = 0x0861,
    SMSG_UNKNOWN_0x088F                              = 0x088F,
    SMSG_MAIL_QUERY_NEXT_TIME_RESULT                 = 0x089B,
    SMSG_UNKNOWN_0x089F                              = 0x089F,
    SMSG_MOVE_UPDATE_RUN_BACK_SPEED                  = 0x08A3,
    SMSG_RESEARCH_SETUP_HISTORY                      = 0x08AB,
    SMSG_UNKNOWN_0x08AE                              = 0x08AE,
    SMSG_BATTLE_PAY_START_DISTRIBUTION_ASSIGN_TO_TARGET_RESPONSE = 0x08AF,
    SMSG_UNKNOWN_0x08BA                              = 0x08BA,
    SMSG_UNKNOWN_0x08BB                              = 0x08BB,
    SMSG_UNKNOWN_0x08BF                              = 0x08BF,
    SMSG_UNKNOWN_0x08F3                              = 0x08F3,
    SMSG_UNKNOWN_0x08FB                              = 0x08FB,
    SMSG_MOVE_UPDATE_PITCH_RATE                      = 0x09E2,
    SMSG_UNKNOWN_0x0A02                              = 0x0A02,
    SMSG_UNKNOWN_0x0A03                              = 0x0A03,
    SMSG_UNKNOWN_0x0A0A                              = 0x0A0A,
    SMSG_UNKNOWN_0x0A0B                              = 0x0A0B,
    SMSG_UNKNOWN_0x0A1A                              = 0x0A1A,
    SMSG_UNKNOWN_0x0A1B                              = 0x0A1B,
    SMSG_CHAT_SERVER_RECONNECTED                     = 0x0A2E,
    SMSG_UNKNOWN_0x0A2F                              = 0x0A2F,
    SMSG_UNKNOWN_0x0A3B                              = 0x0A3B,
    SMSG_TABARD_VENDOR_ACTIVATE                      = 0x0A3E,
    SMSG_GUILD_RANKS_UPDATE                          = 0x0A60,
    SMSG_UNKNOWN_0x0A69                              = 0x0A69,
    SMSG_GUILD_EVENT_BANK_TAB_TEXT_CHANGED           = 0x0A70,
    SMSG_GUILD_PARTY_STATE_RESPONSE                  = 0x0A78,
    SMSG_UNKNOWN_0x0A8B                              = 0x0A8B,
    SMSG_UNKNOWN_0x0A8E                              = 0x0A8E,
    SMSG_ALL_ACCOUNT_CRITERIA                        = 0x0A9E,
    SMSG_MOVE_UPDATE_APPLY_MOVEMENT_FORCE            = 0x0AB6,
    SMSG_UNKNOWN_0x0ABB                              = 0x0ABB,
    SMSG_LF_GUILD_MEMBERSHIP_LIST_UPDATED            = 0x0AE0,
    SMSG_GUILD_MOVE_STARTING                         = 0x0AE1,
    SMSG_GUILD_NEWS_UPDATE                           = 0x0AE8,
    SMSG_GUILD_CHALLENGE_UPDATED                     = 0x0AE9,
    SMSG_GUILD_XP                                    = 0x0AF0,
    SMSG_UNKNOWN_0x0AF1                              = 0x0AF1,
    SMSG_SPLINE_MOVE_SET_NORMAL_FALL                 = 0x0B08,
    SMSG_UNKNOWN_0x0B68                              = 0x0B68,
    SMSG_UNKNOWN_0x0B69                              = 0x0B69,
    SMSG_UNKNOWN_0x0B70                              = 0x0B70,
    SMSG_LF_GUILD_APPLICANT_LIST_UPDATED             = 0x0B71,
    SMSG_GUILD_MEMBER_UPDATE_NOTE                    = 0x0BE1,
    SMSG_GUILD_MOVE_COMPLETE                         = 0x0BE8,
    SMSG_UNKNOWN_0x0BE9                              = 0x0BE9,
    SMSG_GUILD_MEMBERS_FOR_RECIPE                    = 0x0BF0,
    SMSG_GUILD_EVENT_BANK_TAB_MODIFIED               = 0x0BF1,
    SMSG_GUILD_EVENT_PLAYER_LEFT                     = 0x0BF8,
    SMSG_RESEARCH_COMPLETE                           = 0x0C0E,
    SMSG_MONEY_NOTIFY                                = 0x0C0F,
    SMSG_LFG_SLOT_INVALID                            = 0x0C12,
    SMSG_UNKNOWN_0x0C13                              = 0x0C13,
    SMSG_PET_BATTLE_ROUND_RESULT                     = 0x0C1A,
    SMSG_NEW_WORLD_ABORT                             = 0x0C1B,
    SMSG_UNKNOWN_0x0C1F                              = 0x0C1F,
    SMSG_UNKNOWN_0x0C2B                              = 0x0C2B,
    SMSG_LFG_UPDATE_STATUS                           = 0x0C2E,
    SMSG_WAIT_QUEUE_UPDATE                           = 0x0C2F,
    SMSG_AE_LOOT_TARGETS                             = 0x0C32,
    SMSG_UNKNOWN_0x0C33                              = 0x0C33,
    SMSG_UNKNOWN_0x0C3B                              = 0x0C3B,
    SMSG_UNKNOWN_0x0C59                              = 0x0C59,
    SMSG_UNKNOWN_0x0C5B                              = 0x0C5B,
    SMSG_MOVE_SET_ACTIVE_MOVER                       = 0x0C6D,
    SMSG_GAME_OBJECT_ACTIVATE_ANIM_KIT               = 0x0C8A,
    SMSG_UNKNOWN_0x0C8B                              = 0x0C8B,
    SMSG_UNKNOWN_0x0C8E                              = 0x0C8E,
    SMSG_UNKNOWN_0x0C9A                              = 0x0C9A,
    SMSG_UNKNOWN_0x0C9E                              = 0x0C9E,
    SMSG_SET_MOVEMENT_ANIM_KIT                       = 0x0CAA,
    SMSG_WARGAME_REQUEST_SENT                        = 0x0CAE,
    SMSG_UNKNOWN_0x0CBA                              = 0x0CBA,
    SMSG_VIGNETTE_UPDATE                             = 0x0CBE,
    SMSG_SPELL_PERIODIC_AURA_LOG                     = 0x0CF2,
    SMSG_UNKNOWN_0x0D48                              = 0x0D48,
    SMSG_UNKNOWN_0x0D51                              = 0x0D51,
    SMSG_MOVE_UNSET_CAN_TURN_WHILE_FALLING           = 0x0D61,
    SMSG_MOVE_UPDATE_TURN_RATE                       = 0x0D62,
    SMSG_UNKNOWN_0x0DA7                              = 0x0DA7,
    SMSG_UNKNOWN_0x0E0E                              = 0x0E0E,
    SMSG_UNKNOWN_0x0E1A                              = 0x0E1A,
    SMSG_PET_BATTLE_INITIAL_UPDATE                   = 0x0E1E,
    SMSG_UNKNOWN_0x0E2B                              = 0x0E2B,
    SMSG_UNKNOWN_0x0E2E                              = 0x0E2E,
    SMSG_UNKNOWN_0x0E2F                              = 0x0E2F,
    SMSG_LOAD_CUF_PROFILES                           = 0x0E32,
    SMSG_ITEM_EXPIRE_PURCHASE_REFUND                 = 0x0E33,
    SMSG_UNKNOWN_0x0E3E                              = 0x0E3E,
    SMSG_LF_GUILD_RECRUIT_LIST_UPDATED               = 0x0E68,
    SMSG_UNKNOWN_0x0E69                              = 0x0E69,
    SMSG_GUILD_RENAMED                               = 0x0E70,
    SMSG_UNKNOWN_0x0E71                              = 0x0E71,
    SMSG_UNKNOWN_0x0E8E                              = 0x0E8E,
    SMSG_UNKNOWN_0x0E9A                              = 0x0E9A,
    SMSG_UNKNOWN_0x0E9E                              = 0x0E9E,
    SMSG_CONQUEST_FORMULA_CONSTANTS                  = 0x0EAB,
    SMSG_UNKNOWN_0x0EAE                              = 0x0EAE,
    SMSG_UNKNOWN_0x0EAF                              = 0x0EAF,
    SMSG_BATTLEFIELD_RATED_INFO                      = 0x0EBA,
    SMSG_UNKNOWN_0x0EBE                              = 0x0EBE,
    SMSG_UNKNOWN_0x0EBF                              = 0x0EBF,
    SMSG_GUILD_MEMBER_RECIPES                        = 0x0EE1,
    SMSG_UNKNOWN_0x0EF0                              = 0x0EF0,
    SMSG_GUILD_ACHIEVEMENT_DATA                      = 0x0EF8,
    SMSG_UNKNOWN_0x0EF9                              = 0x0EF9,
    SMSG_UNKNOWN_0x0F27                              = 0x0F27,
    SMSG_GUILD_EVENT_BANK_MONEY_CHANGED              = 0x0F68,
    SMSG_LF_GUILD_BROWSE_UPDATED                     = 0x0F69,
    SMSG_GUILD_NEWS_DELETED                          = 0x0F70,
    SMSG_GUILD_XP_GAIN                               = 0x0FE0,
    SMSG_GUILD_INVITE_CANCEL                         = 0x0FE1,
    SMSG_GUILD_FLAGGED_FOR_RENAME                    = 0x0FE9,
    SMSG_GUILD_RECIPES                               = 0x0FF1,
    SMSG_UNKNOWN_0x1003                              = 0x1003,
    SMSG_GROUP_ROLE_POLL_INFORM                      = 0x1007,
    SMSG_UNKNOWN_0x100B                              = 0x100B,
    SMSG_LOOT_ROLLS_COMPLETE                         = 0x101B,
    SMSG_UNKNOWN_0x101E                              = 0x101E,
    SMSG_UNKNOWN_0x101F                              = 0x101F,
    SMSG_UNKNOWN_0x1023                              = 0x1023,
    SMSG_UNKNOWN_0x1027                              = 0x1027,
    SMSG_UNKNOWN_0x102F                              = 0x102F,
    SMSG_BATTLE_PAY_ACK_FAILED                       = 0x103E,
    SMSG_BLACK_MARKET_OUTBID                         = 0x1040,
    SMSG_UNKNOWN_0x1041                              = 0x1041,
    SMSG_UNKNOWN_0x1042                              = 0x1042,
    SMSG_BLACK_MARKET_WON                            = 0x1060,
    SMSG_MOVE_SET_CAN_TURN_WHILE_FALLING             = 0x1065,
    SMSG_UNKNOWN_0x1086                              = 0x1086,
    SMSG_UNKNOWN_0x108A                              = 0x108A,
    SMSG_UNKNOWN_0x108E                              = 0x108E,
    SMSG_UNKNOWN_0x108F                              = 0x108F,
    SMSG_UNKNOWN_0x109B                              = 0x109B,
    SMSG_UNKNOWN_0x109E                              = 0x109E,
    SMSG_AUCTION_HELLO                               = 0x10A7,
    SMSG_UNKNOWN_0x10AB                              = 0x10AB,
    SMSG_UNKNOWN_0x10AF                              = 0x10AF,
    SMSG_UNKNOWN_0x10BA                              = 0x10BA,
    SMSG_UNKNOWN_0x10BB                              = 0x10BB,
    SMSG_UNKNOWN_0x10BF                              = 0x10BF,
    SMSG_UNKNOWN_0x10C0                              = 0x10C0,
    SMSG_UNKNOWN_0x10C1                              = 0x10C1,
    SMSG_UNKNOWN_0x10C3                              = 0x10C3,
    SMSG_UNKNOWN_0x1148                              = 0x1148,
    SMSG_ARCHAEOLOGY_SURVERY_CAST                    = 0x1160,
    SMSG_UNKNOWN_0x1162                              = 0x1162,
    SMSG_UNKNOWN_0x117A                              = 0x117A,
    SMSG_UNKNOWN_0x11C2                              = 0x11C2,
    SMSG_ATTACKSWING_ERROR                           = 0x11E1,
    SMSG_PLAY_SPELL_VISUAL_KIT                       = 0x11E3,
    SMSG_PET_BATTLE_QUEUE_PROPOSE_MATCH              = 0x1202,
    SMSG_MISSILE_CANCEL                              = 0x1203,
    SMSG_DIFFERENT_INSTANCE_FROM_PARTY               = 0x120B,
    SMSG_CANCEL_SCENE                                = 0x120E,
    SMSG_ENCOUNTER_END                               = 0x120F,
    SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN           = 0x121E,
    SMSG_LOOT_CONTENTS                               = 0x121F,
    SMSG_UNKNOWN_0x1223                              = 0x1223,
    SMSG_UNKNOWN_0x122A                              = 0x122A,
    SMSG_UNKNOWN_0x122F                              = 0x122F,
    SMSG_UNKNOWN_0x123A                              = 0x123A,
    SMSG_UNKNOWN_0x123E                              = 0x123E,
    SMSG_UNKNOWN_0x1282                              = 0x1282,
    SMSG_SET_DUNGEON_DIFFICULTY                      = 0x1283,
    SMSG_UNKNOWN_0x128B                              = 0x128B,
    SMSG_PLAYER_DIFFICULTY_CHANGE                    = 0x128E,
    SMSG_UPDATE_CURRENCY                             = 0x129E,
    SMSG_UNKNOWN_0x12A2                              = 0x12A2,
    SMSG_DEBUG_RUNE_REGEN                            = 0x12A7,
    SMSG_UNKNOWN_0x12AA                              = 0x12AA,
    SMSG_TITLE_LOST                                  = 0x12BF,
    SMSG_UNKNOWN_0x140B                              = 0x140B,
    SMSG_UNKNOWN_0x140E                              = 0x140E,
    SMSG_UNKNOWN_0x1412                              = 0x1412,
    SMSG_RANDOM_ROLL                                 = 0x141A,
    SMSG_UNKNOWN_0x141F                              = 0x141F,
    SMSG_UNKNOWN_0x142B                              = 0x142B,
    SMSG_UNKNOWN_0x1433                              = 0x1433,
    SMSG_UNKNOWN_0x143B                              = 0x143B,
    SMSG_UNKNOWN_0x143E                              = 0x143E,
    SMSG_UNKNOWN_0x143F                              = 0x143F,
    SMSG_UNKNOWN_0x1441                              = 0x1441,
    SMSG_UNKNOWN_0x1442                              = 0x1442,
    SMSG_MOVE_UPDATE_REMOVE_MOVEMENT_FORCE           = 0x1464,
    SMSG_BLACK_MARKET_BID_RESULT                             = 0x148A,
    SMSG_GM_TICKET_CASE_STATUS                       = 0x148E,
    SMSG_UNKNOWN_0x148F                              = 0x148F,
    SMSG_UNKNOWN_0x149E                              = 0x149E,
    SMSG_UNKNOWN_0x14AB                              = 0x14AB,
    SMSG_VOID_STORAGE_TRANSFER_CHANGES               = 0x14BA,
    SMSG_UNKNOWN_0x14C1                              = 0x14C1,
    SMSG_UNKNOWN_0x14D2                              = 0x14D2,
    SMSG_UNKNOWN_0x14E0                              = 0x14E0,
    SMSG_BATTLE_PAY_PURCHASE_UPDATE                  = 0x14E2,
    SMSG_BATTLE_PAY_CONFIRM_PURCHASE                 = 0x14E3,
    SMSG_BATTLE_PET_QUERY_NAME_RESPONSE              = 0x1540,
    SMSG_UNKNOWN_0x1541                              = 0x1541,
    SMSG_BATTLE_PET_JOURNAL                          = 0x1542,
    SMSG_UNKNOWN_0x1543                              = 0x1543,
    SMSG_UNKNOWN_0x1553                              = 0x1553,
    SMSG_UNKNOWN_0x1561                              = 0x1561,
    SMSG_UNKNOWN_0x1568                              = 0x1568,
    SMSG_MOVE_UPDATE_RUN_SPEED                       = 0x158E,
    SMSG_MOVE_UPDATE_TELEPORT                        = 0x15A9,
    SMSG_MOVE_COLLISION_DISABLE                      = 0x15B8,
    SMSG_UNKNOWN_0x15C1                              = 0x15C1,
    SMSG_RAID_READY_CHECK_COMPLETED                  = 0x15C2,
    SMSG_SHOW_NEURTRAL_PLAYER_FACTION_SELECT_UI      = 0x15E0,
    SMSG_UNKNOWN_0x15E1                              = 0x15E1,
    SMSG_RAID_TARGET_UPDATE_SINGLE                   = 0x160B,
    SMSG_UNKNOWN_0x1612                              = 0x1612,
    SMSG_PET_STABLE_LIST                             = 0x1613,
    SMSG_UNKNOWN_0x161A                              = 0x161A,
    SMSG_UNKNOWN_0x161F                              = 0x161F,
    SMSG_CLEAR_COOLDOWN                              = 0x162A,
    SMSG_UNKNOWN_0x162F                              = 0x162F,
    SMSG_UNKNOWN_0x163E                              = 0x163E,
    SMSG_UNKNOWN_0x168A                              = 0x168A,
    SMSG_UNKNOWN_0x168B                              = 0x168B,
    SMSG_MINIMAP_PING                                = 0x168F,
    SMSG_UNKNOWN_0x169A                              = 0x169A,
    SMSG_RANDOMIZE_CHAR_NAME                         = 0x169F,
    SMSG_UNKNOWN_0x16AA                              = 0x16AA,
    SMSG_CALENDAR_EVENT_INITIAL_INVITE               = 0x16AE,
    SMSG_BATTLE_PET_SLOT_UPDATE                      = 0x16AF,
    SMSG_MOVE_UPDATE_COLLISION_HEIGHT                = 0x1812,
    SMSG_UNKNOWN_0x181A                              = 0x181A,
    SMSG_UNKNOWN_0x181B                              = 0x181B,
    SMSG_DISPLAY_GAME_ERROR                          = 0x181F,
    SMSG_SPLINE_MOVE_SET_WATER_WALK                  = 0x1823,
    SMSG_MOVE_COLLISION_ENABLE                       = 0x1826,
    SMSG_UNKNOWN_0x182A                              = 0x182A,
    SMSG_UNKNOWN_0x182E                              = 0x182E,
    SMSG_UNKNOWN_0x182F                              = 0x182F,
    SMSG_LFG_BOOT_PROPOSAL_UPDATE                    = 0x183A,
    SMSG_UNKNOWN_0x183F                              = 0x183F,
    SMSG_UNKNOWN_0x1841                              = 0x1841,
    SMSG_UNKNOWN_0x1842                              = 0x1842,
    SMSG_STREAMING_MOVIE                             = 0x1843,
    SMSG_SPELLINTERRUPTLOG                           = 0x1851,
    SMSG_UNKNOWN_0x186F                              = 0x186F,
    SMSG_CHARACTER_UPGRADE_STARTED                   = 0x188A,
    SMSG_UNKNOWN_0x188B                              = 0x188B,
    SMSG_UNKNOWN_0x188F                              = 0x188F,
    SMSG_SPLINE_MOVE_SET_FEATHER_FALL                = 0x1893,
    SMSG_ACCOUNT_CRITERIA_UPDATE                     = 0x189E,
    SMSG_UNKNOWN_0x18AA                              = 0x18AA,
    SMSG_BATTLE_PET_DELETED                          = 0x18AB,
    SMSG_UNKNOWN_0x18AE                              = 0x18AE,
    SMSG_UNKNOWN_0x18AF                              = 0x18AF,
    SMSG_SPLINE_MOVE_SET_LAND_WALK                   = 0x18B6,
    SMSG_UNKNOWN_0x18BA                              = 0x18BA,
    SMSG_UNKNOWN_0x18BB                              = 0x18BB,
    SMSG_BATTLEFIELD_MGR_EJECTED                     = 0x18C2,
    SMSG_UNKNOWN_0x18E0                              = 0x18E0,
    SMSG_UNKNOWN_0x18E1                              = 0x18E1,
    SMSG_UNKNOWN_0x1941                              = 0x1941,
    SMSG_UNKNOWN_0x1942                              = 0x1942,
    SMSG_UNKNOWN_0x1949                              = 0x1949,
    SMSG_UNKNOWN_0x1962                              = 0x1962,
    SMSG_UNKNOWN_0x1968                              = 0x1968,
    SMSG_UNKNOWN_0x1990                              = 0x1990,
    SMSG_UNKNOWN_0x19C2                              = 0x19C2,
    SMSG_UNKNOWN_0x19C3                              = 0x19C3,
    SMSG_BATTLE_PET_JOURNAL_LOCK_ACQUIRED            = 0x1A0F,
    SMSG_UNKNOWN_0x1A1A                              = 0x1A1A,
    SMSG_UNKNOWN_0x1A2B                              = 0x1A2B,
    SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE          = 0x1A3A,
    SMSG_UNKNOWN_0x1A3E                              = 0x1A3E,
    SMSG_UNKNOWN_0x1A3F                              = 0x1A3F,
    SMSG_UNKNOWN_0x1A61                              = 0x1A61,
    SMSG_GUILD_REWARDS_LIST                          = 0x1A69,
    SMSG_LF_GUILD_APPLICATIONS_LIST_CHANGED          = 0x1A70,
    SMSG_GUILD_REPUTATION_WEEKLY_CAP                 = 0x1A71,
    SMSG_LF_GUILD_COMMAND_RESULT                     = 0x1A79,
    SMSG_UNKNOWN_0x1A8A                              = 0x1A8A,
    SMSG_SETUP_CURRENCY                              = 0x1A8B,
    SMSG_UNKNOWN_0x1A9F                              = 0x1A9F,
    SMSG_UNKNOWN_0x1AAA                              = 0x1AAA,
    SMSG_UNKNOWN_0x1AAB                              = 0x1AAB,
    SMSG_UNKNOWN_0x1ABA                              = 0x1ABA,
    SMSG_BATTLE_PAY_GET_PRODUCT_LIST_RESPONSE        = 0x1ABF,
    SMSG_GUILD_BANK_QUERY_TEXT_RESULT                = 0x1AE0,
    SMSG_UNKNOWN_0x1AF0                              = 0x1AF0,
    SMSG_GUILD_CHALLENGE_COMPLETED                   = 0x1AF8,
    SMSG_GUILD_CRITERIA_DELETED                      = 0x1B60,
    SMSG_UNKNOWN_0x1B69                              = 0x1B69,
    SMSG_GUILD_ACHIEVEMENT_MEMBERS                   = 0x1B70,
    SMSG_LF_GUILD_POST_UPDATED                       = 0x1B71,
    SMSG_GUILD_MAX_DAILY_XP                          = 0x1BE1,
    SMSG_GUILD_MEMBER_DAILY_RESET                    = 0x1BE8,
    SMSG_GUILD_EVENT_BANK_TAB_ADDED                  = 0x1BE9,
    SMSG_GUILD_CRITERIA_DATA                         = 0x1BF0,
    SMSG_GUILD_ACHIEVEMENT_EARNED                    = 0x1BF1,
    SMSG_UNKNOWN_0x1C0E                              = 0x1C0E,
    SMSG_UNKNOWN_0x1C1A                              = 0x1C1A,
    SMSG_UNKNOWN_0x1C1B                              = 0x1C1B,
    SMSG_UNKNOWN_0x1C1E                              = 0x1C1E,
    SMSG_UNKNOWN_0x1C2B                              = 0x1C2B,
    SMSG_PET_BATTLE_FINAL_ROUND                      = 0x1C2F,
    SMSG_UNKNOWN_0x1C32                              = 0x1C32,
    SMSG_PLAY_SCENE                                  = 0x1C3A,
    SMSG_UNKNOWN_0x1C8A                              = 0x1C8A,
    SMSG_UNKNOWN_0x1C8B                              = 0x1C8B,
    SMSG_RAID_READY_CHECK                            = 0x1C8E,
    SMSG_CALENDAR_EVENT_INVITE_STATUS                = 0x1C9B,
    SMSG_VOID_TRANSFER_RESULT                        = 0x1C9E,
    SMSG_UNKNOWN_0x1C9F                              = 0x1C9F,
    SMSG_UNKNOWN_0x1CAB                              = 0x1CAB,
    SMSG_CUSTOM_LOAD_SCREEN                          = 0x1CAF,
    SMSG_UNKNOWN_0x1CBA                              = 0x1CBA,
    SMSG_UNKNOWN_0x1CBB                              = 0x1CBB,
    SMSG_UNKNOWN_0x1D23                              = 0x1D23,
    SMSG_UNKNOWN_0x1DA3                              = 0x1DA3,
    SMSG_MOVE_APPLY_MOVEMENT_FORCE                   = 0x1DBE,
    SMSG_UNKNOWN_0x1E0A                              = 0x1E0A,
    SMSG_PET_BATTLE_PVP_CHALLENGE                    = 0x1E0B,
    SMSG_UNKNOWN_0x1E12                              = 0x1E12,
    SMSG_UNKNOWN_0x1E13                              = 0x1E13,
    SMSG_BATTLE_PAY_DISTRIBUTION_UPDATE              = 0x1E1B,
    SMSG_GROUP_SET_ROLE                              = 0x1E1F,
    SMSG_UNKNOWN_0x1E2A                              = 0x1E2A,
    SMSG_UNKNOWN_0x1E2B                              = 0x1E2B,
    SMSG_BATTLE_PAY_DELIVERY_STARTED                 = 0x1E32,
    SMSG_UNKNOWN_0x1E33                              = 0x1E33,
    SMSG_UNKNOWN_0x1E3F                              = 0x1E3F,
    SMSG_GUILD_ACHIEVEMENT_DELETED                   = 0x1E61,
    SMSG_UNKNOWN_0x1E68                              = 0x1E68,
    SMSG_ENCOUNTER_START                             = 0x1E8A,
    SMSG_UNKNOWN_0x1E8B                              = 0x1E8B,
    SMSG_UNKNOWN_0x1E9B                              = 0x1E9B,
    SMSG_UNKNOWN_0x1E9F                              = 0x1E9F,
    SMSG_HOTFIX_NOTIFY_BLOB                          = 0x1EBA,
    SMSG_UNKNOWN_0x1EBB                              = 0x1EBB,
    SMSG_UNKNOWN_0x1EBE                              = 0x1EBE,
    SMSG_VOID_ITEM_SWAP_RESPONSE                     = 0x1EBF,
};

#endif // OPCODES_REFERENCE_H
