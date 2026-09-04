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

#ifndef MANGOS_H_OPCODES
#define MANGOS_H_OPCODES

#include "Common.h"
#include "Policies/Singleton.h"

/**
 * This is a list of Opcodes that are known for the client/server communication, it is used
 * to tell the server to do something or the client to do something. Every opcode is handled
 * in some way, and you can find what functions handle what opcode in the implementation of
 * \ref Opcodes::BuildOpcodeList
 *
 * To send messages the following functions can be used: \ref WorldObject::SendMessageToSet,
 * \ref WorldObject::SendMessageToSetExcept, \ref WorldObject::SendMessageToSetInRange
 *
 * \see WorldPacket
 * \todo Replace the Pack GUID part with a packed GUID, ie: it's shorter than usual?
 */
enum OpcodesList
{
    // === MoP 5.4.8 (18414) opcode-value provenance — CMSG classified 2026-07-18 ===
    //   // 5.4.8 18414 (Wow.exe binary[, via CMSG_X]) = recovered clean-room from the client
    //        binary (independent oracle); 'via' = fork alias the binary value was name-matched to.
    //   // 5.4.8 18414 (fork tables) | // 5.4.8 18414      = value from the reference forks / MoP port.
    //   // 5.4.8 18414 (reference-derived)             = fork value, NOT independently binary-confirmed.
    //   // not in 5.4.8 (legacy[; handler retained])   = absent from all 5.4.8 refs (removed/renamed);
    //        harmless unregistered dead code (dispatch is by value via DefC(), not by enum name).
    //   // ... NYI in 5.4.8 refs (unverified)          = refs list the name but no value; the shown
    //        number is a stale (4.3.4) fallback, unverified for 5.4.8.
    // CMSG totals: 493 = 398 confirmed + 1 reference-derived + 68 legacy + 26 NYI/unverified.
    // CMSG_MOVE_* are bidirectional MSG_MOVE_* (client sends + server echoes) — already correct.
    // SMSG (reference-tier — landed from the 3 forks). CORRECTION: an initial probe called SMSG
    // 'not statically recoverable'; a cross-model re-review + decompiler check RETRACTED that as a
    // false negative. The client DOES statically gate inbound opcodes: sub_797CEE routes each wire
    // opcode via computed dispatchers (sub_659694 et al: mask/bit-permute -> dense switch -> parser/
    // handler) plus acceptance bitsets — a static accepted-set + handler-routing oracle exists.
    // Extraction of that oracle is pending; SMSG values stay reference-tier until it lands):
    // 456 values landed from fork consensus/single-source (228 of the 279 header-overflows fixed);
    // 107 legacy + 44 NYI tagged. Residual: 51 SMSG still >0x1FFF (24 NYI + 19 legacy + 8 review) —
    // must not be SENT until resolved; 32 need manual name-mapping; 174 MoP-new SMSG not yet added.
    // ================================================================================
    MSG_WOW_CONNECTION                           = 0x4F57,    // 5.4.8 18414 (handshake sentinel, deliberately outside the world opcode domain)
    SMSG_AUTH_CHALLENGE                          = 0x0949,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_AUTH_SESSION                            = 0x00B2, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AUTH_RESPONSE                           = 0x0ABA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_NULL_ACTION                              = 0x1001,    // (no client leaf)
    SMSG_DBLOOKUP                                = 0x1004,    // (legacy; no client leaf)
    SMSG_QUERY_OBJECT_ROTATION                   = 0x1008,    // (legacy; no client leaf)
    CMSG_WORLD_TELEPORT                          = 0x24B2, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    CMSG_TELEPORT_TO_UNIT                        = 0x4206, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_ZONE_MAP                                = 0x100C,    // (legacy; no client leaf)
    SMSG_CHECK_FOR_BOTS                          = 0x1016,    // (legacy; no client leaf)
    SMSG_FORCEACTIONSHOW                         = 0x101C,    // (legacy; no client leaf)
    SMSG_PETGODMODE                              = 0x1940,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_REFER_A_FRIEND_EXPIRED                  = 0x1143,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_GODMODE                                 = 0x1862,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_DEBUG_AISTATE                           = 0x0A2A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork; via SMSG_DEBUG_AI_STATE)
    CMSG_DESTROY_ITEM                            = 0x0026, // 5.4.8 18414 (writer sub_6919D8; live request correlated)
    SMSG_DESTRUCTIBLE_BUILDING_DAMAGE            = 0x14BF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CHAR_CREATE                             = 0x0F1D, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHAR_ENUM                               = 0x00E0, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHAR_DELETE                             = 0x04E2, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AUTH_SRP6_RESPONSE                      = 0x103A,    // 5.4.8 18414 (Wow.exe leaf)
    SMSG_CHAR_CREATE                             = 0x1CAA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CHAR_ENUM                               = 0x11C3,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_CHAR_DELETE                             = 0x0C9F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_PLAYER_LOGIN                            = 0x158F, // 5.4.8 18414 (Wow.exe binary)
    SMSG_NEW_WORLD                               = 0x1C3B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SUSPEND_TOKEN_RESPONSE                  = 0x0292,    // 5.4.8 18414 (Wow.exe writer sub_66F465; uint32 token)
    SMSG_SUSPEND_TOKEN                           = 0x18BA,    // 5.4.8 18414 (Wow.exe reader sub_6DAC04; uint32 token + 2-bit reason)
    SMSG_TRANSFER_PENDING                        = 0x061B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_TRANSFER_ABORTED                        = 0x0C8F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CHARACTER_LOGIN_FAILED                  = 0x1A0B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_LOGIN_SETTIMESPEED                      = 0x082B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_GAMETIME_UPDATE                         = 0x0E1B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_GAMETIME_SET                            = 0x0A0F,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_GAMESPEED_SET                           = 0x1048,    // (value unverified; no client leaf)
    SMSG_SERVERTIME                              = 0x1C3E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_PLAYER_LOGOUT                           = 0x104B, // not in 5.4.8 (legacy; handler retained)
    CMSG_LOGOUT_REQUEST                          = 0x0643, // 5.4.8 18414 (Wow.exe binary; manual "logout" API route)
    CMSG_LOGOUT_REQUEST_IDLE                     = 0x1349, // 5.4.8 18414 (Wow.exe binary; automatic-idle route)
    SMSG_LOGOUT_RESPONSE                         = 0x008F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LOGOUT_COMPLETE                         = 0x142F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_LOGOUT_CANCEL                           = 0x06C1, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LOGOUT_CANCEL_ACK                       = 0x0AAF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_NAME_QUERY                              = 0x0328, // 5.4.8 18414 (Wow.exe binary)
    SMSG_NAME_QUERY_RESPONSE                     = 0x169B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_PET_NAME_QUERY                          = 0x1C62, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PET_NAME_QUERY_RESPONSE                 = 0x0ABE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GUILD_QUERY                             = 0x1AB6, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_QUERY_RESPONSE                    = 0x1B79,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_PAGE_TEXT_QUERY                         = 0x1022, // 5.4.8 18414 (Wow.exe pagetextcache.wdb request writer)
    SMSG_PAGE_TEXT_QUERY_RESPONSE                = 0x081E, // 5.4.8 18414 (Wow.exe page-cache record reader and callback)
    CMSG_QUEST_QUERY                             = 0x02D5, // 5.4.8 18414 (Wow.exe questcache.wdb request writer)
    SMSG_QUEST_QUERY_RESPONSE                    = 0x0276, // 5.4.8 18414 (Wow.exe quest-cache record reader and cache callback)
    CMSG_GAMEOBJECT_QUERY                        = 0x1461, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GAMEOBJECT_QUERY_RESPONSE               = 0x06BF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CREATURE_QUERY                          = 0x0842, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CREATURE_QUERY_RESPONSE                 = 0x048B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_WHO                                     = 0x18A3, // 5.4.8 18414 (Wow.exe binary)
    SMSG_WHO                                     = 0x161B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_WHOIS                                   = 0x0308, // 5.3.0 17128 — NYI in 5.4.8 refs (unverified)
    SMSG_WHOIS                                   = 0x12BA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CONTACT_LIST                            = 0x0BB4, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CONTACT_LIST                            = 0x1F22,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_FRIEND_STATUS                           = 0x0532,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_ADD_FRIEND                              = 0x09A6, // 5.4.8 18414 (Wow.exe binary)
    CMSG_DEL_FRIEND                              = 0x1103, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SET_CONTACT_NOTES                       = 0x0937, // 5.4.8 18414 (Wow.exe binary)
    CMSG_ADD_IGNORE                              = 0x0D20, // 5.4.8 18414 (Wow.exe binary)
    CMSG_DEL_IGNORE                              = 0x0737, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GROUP_INVITE                            = 0x072D, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GROUP_INVITE                            = 0x0A8F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_GROUP_CANCEL                            = 0x4D25,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    CMSG_GROUP_INVITE_RESPONSE                   = 0x0D61, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GROUP_DECLINE                           = 0x17A3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GROUP_UNINVITE                          = 0x1076, // not in 5.4.8 (legacy)
    CMSG_GROUP_UNINVITE_GUID                     = 0x0CE1, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GROUP_UNINVITE                          = 0x1313,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GROUP_SET_LEADER                        = 0x15BB, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GROUP_SET_LEADER                        = 0x18BF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_LOOT_METHOD                             = 0x0DE1, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GROUP_DISBAND                           = 0x1798, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GROUP_DESTROYED                         = 0x1B27,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_GROUP_LIST                              = 0x0CBB,    // 5.4.8 18414 (Wow.exe binary; name reference-consensus)
    SMSG_PARTY_MEMBER_STATS                      = 0x0A9A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PARTY_COMMAND_RESULT                    = 0x0F86,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GUILD_CREATE                            = 0x1082, // not in 5.4.8 (legacy; handler retained)
    CMSG_GUILD_INVITE                            = 0x0869, // 5.4.8 18414 (live client: 9-bit name length plus raw name)
    SMSG_GUILD_INVITE                            = 0x0F71,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GUILD_ACCEPT                            = 0x18A2, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_DECLINE                           = 0x147B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_DECLINE                           = 0x1AF9,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_GUILD_ROSTER                            = 0x1459, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_ROSTER                            = 0x0BE0,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GUILD_PROMOTE                           = 0x0571, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_SET_RANK                          = 0x0C7A, // 5.4.8 18414 (Wow.exe binary, via CMSG_GUILD_SET_RANK_PERMISSIONS)
    CMSG_GUILD_SWITCH_RANK                       = 0x0CD1, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_SET_ACHIEVEMENT_TRACKING          = 0x0CF0, // 5.4.8 18414 (Wow.exe binary writer)
    CMSG_GUILD_DEMOTE                            = 0x1553, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_LEAVE                             = 0x04D8, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_REMOVE                            = 0x0CD8, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_DISBAND                           = 0x0D73, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_LEADER                            = 0x3034, // not in 5.4.8 (legacy; handler retained)
    CMSG_GUILD_MOTD                              = 0x1473, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_COMMAND_RESULT                    = 0x0EF1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GUILD_AUTO_DECLINE_TOGGLE               = 0x2034, // not in 5.4.8 (legacy)
    // Deliberately NOT registered. Thunk sub_6860AD, writer sub_688B4B (vtable
    // 0xD64428: slot +4 the writer, +8 the thunk, +12 the 0x00C84A3D signature).
    // That writer emits exactly ONE BIT -- the toggle state. It is a settings
    // toggle, not a per-invite decline, so routing it to
    // HandleGuildDeclineOpcode would treat "stop asking me" as "decline this
    // invitation". It needs its own handler.
    //
    // sub_688B4B is SHARED: 18 vtables point at it, because it is the generic
    // "write one bit from +16" writer. So for this opcode the writer does not
    // identify the packet -- only the thunk does. That breaks the property the
    // petition wave relied on, where every writer had exactly one xref, and it
    // is why two reviewers reached opposite conclusions here: one identified
    // sub_688B4B correctly, the other found the same function serving the LFG
    // boot-vote vtable at 0xD63364 and concluded it therefore was not this
    // opcode's. Both observations were true; sharing makes it both.
    //
    // Two earlier attributions in this note were wrong and are recorded so the
    // trace is not repeated: sub_688F62 is CMSG_ACTIVATETAXI's (0x03C9), reached
    // by converting 6851403 to hex by hand as 0x68908B instead of 0x688B4B; and
    // sub_C84A3D is not a body writer at all, it is the signature constant every
    // one of these vtables carries at slot +12.
    CMSG_GUILD_AUTO_DECLINE                      = 0x06CB, // 5.4.8 18414 (Wow.exe binary, via CMSG_AUTO_DECLINE_GUILD_INVITES)
    CMSG_GUILD_QUERY_RANKS                       = 0x0D50, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_QUERY_RANKS_RESULT                = 0x0A79,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_GUILD_RANKS)
    CMSG_MESSAGECHAT_ADDON_INSTANCE              = 0x08AF, // 5.4.8 18414 (Wow.exe binary, via CMSG_MESSAGECHAT_ADDON_INSTANCE_CHAT)
    CMSG_MESSAGECHAT_ADDON_GUILD                 = 0x0E3B, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_ADDON_OFFICER               = 0x180B, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_ADDON_PARTY                 = 0x028E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_ADDON_RAID                  = 0x009A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_ADDON_WHISPER               = 0x0EBB, // 5.4.8 18414 (Wow.exe binary)
    CMSG_UNREGISTER_ALL_ADDON_PREFIXES           = 0x029F, // 5.4.8 18414 (Wow.exe empty writer and registration-container call path)
    CMSG_ADDON_REGISTERED_PREFIXES               = 0x040E, // 5.4.8 18414 (Wow.exe writer: 24-bit count, 5-bit lengths, raw strings)
    CMSG_MESSAGECHAT_AFK                         = 0x0EAB, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_BATTLEGROUND                = 0x2156, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    CMSG_MESSAGECHAT_CHANNEL                     = 0x00BB, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_DND                         = 0x002E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_EMOTE                       = 0x103E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_GUILD                       = 0x0CAE, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_INSTANCE                    = 0x162A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_OFFICER                     = 0x0ABF, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_PARTY                       = 0x109A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_RAID                        = 0x083E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_RAID_WARNING                = 0x16AB, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_SAY                         = 0x0A9A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_WHISPER                     = 0x123E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MESSAGECHAT_YELL                        = 0x04AA, // 5.4.8 18414 (Wow.exe binary)
    SMSG_MESSAGECHAT                             = 0x1A9A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_JOIN_CHANNEL                            = 0x148E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_LEAVE_CHANNEL                           = 0x042A, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CHANNEL_NOTIFY                          = 0x0F06,    // 5.4.8 18414 (Wow.exe dynamic reader 0xCE1FAD)
    CMSG_CHANNEL_LIST                            = 0x0C1B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CHANNEL_LIST                            = 0x0B22,    // 5.4.8 18414 (Wow.exe dynamic reader 0xCDAE61)
    CMSG_CHANNEL_PASSWORD                        = 0x0A1E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_SET_OWNER                       = 0x141A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_OWNER                           = 0x00AF, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_MODERATOR                       = 0x00AE, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_UNMODERATOR                     = 0x041E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_MUTE                            = 0x000A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_UNMUTE                          = 0x022A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_INVITE                          = 0x10AB, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_KICK                            = 0x0E0B, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_BAN                             = 0x08BF, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_UNBAN                           = 0x081F, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_ANNOUNCEMENTS                   = 0x06AF, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CHANNEL_MODERATE                        = 0x2944, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_UPDATE_OBJECT                           = 0x1792,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_DESTROY_OBJECT                          = 0x14C2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_USE_ITEM                                = 0x1CC1, // 5.4.8 18414 (Wow.exe binary)
    CMSG_OPEN_ITEM                               = 0x1D10, // 5.4.8 18414 (Wow.exe binary)
    CMSG_READ_ITEM                               = 0x0D00, // 5.4.8 18414 (Wow.exe binary)
    SMSG_READ_ITEM_OK                            = 0x0305,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_READ_ITEM_RESULT_OK)
    SMSG_ITEM_COOLDOWN                           = 0x1904,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GAMEOBJ_USE                             = 0x06D8, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GAMEOBJECT_CUSTOM_ANIM                  = 0x001F,    // 5.4.8 18414 (Wow.exe binary; direct reader)
    CMSG_AREATRIGGER                             = 0x1C44, // 5.4.8 18414 (writer 0x690745; body 0x693143)
    CMSG_MOVE_START_FORWARD                      = 0x095A, // 5.4.8 18414
    CMSG_MOVE_START_BACKWARD                     = 0x09D8, // 5.4.8 18414
    CMSG_MOVE_STOP                               = 0x08F1, // 5.4.8 18414
    CMSG_MOVE_START_STRAFE_LEFT                  = 0x01F8, // 5.4.8 18414 (writer 0x670170; body 0x677C20)
    CMSG_MOVE_START_STRAFE_RIGHT                 = 0x1058, // 5.4.8 18414 (writer 0x670AF3; body 0x6817D5)
    CMSG_MOVE_STOP_STRAFE                        = 0x0171, // 5.4.8 18414 (writer 0x6701B4; body 0x678776)
    CMSG_MOVE_JUMP                               = 0x1153, // 5.4.8 18414 (writer 0x66FCFE; body 0x67545F)
    CMSG_MOVE_START_TURN_LEFT                    = 0x01D0, // 5.4.8 18414 (writer 0x66FBCB; body 0x67434E)
    CMSG_MOVE_START_TURN_RIGHT                   = 0x107B, // 5.4.8 18414 (writer 0x67046C; body 0x67BB09)
    CMSG_MOVE_STOP_TURN                          = 0x1170, // 5.4.8 18414 (writer 0x6706A9; body 0x67DD58)
    CMSG_MOVE_START_PITCH_UP                     = 0x00D8, // 5.4.8 18414
    CMSG_MOVE_START_PITCH_DOWN                   = 0x08D8, // 5.4.8 18414
    CMSG_MOVE_STOP_PITCH                         = 0x007A, // 5.4.8 18414
    MSG_MOVE_TOGGLE_LOGGING                      = 0x10C5,    // (no client leaf)
    SMSG_MOVE_TELEPORT                           = 0x0B39,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_MOVE_TELEPORT_CHEAT                      = 0x10C7,    // (no client leaf)
    CMSG_MOVE_TELEPORT_ACK                       = 0x0078, // 5.4.8 18414 (Wow.exe binary)
    MSG_MOVE_TOGGLE_FALL_LOGGING                 = 0x10C9,    // (no client leaf)
    CMSG_MOVE_FALL_LAND                          = 0x08FA, // 5.4.8 18414
    CMSG_MOVE_START_SWIM                         = 0x1858, // 5.4.8 18414
    CMSG_MOVE_STOP_SWIM                          = 0x0950, // 5.4.8 18414
    MSG_MOVE_SET_RUN_SPEED_CHEAT                 = 0x10CD,    // (no client leaf)
    SMSG_MOVE_SET_RUN_SPEED                      = 0x184C,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_MOVE_SET_RUN_BACK_SPEED_CHEAT            = 0x10CF,    // (no client leaf)
    SMSG_MOVE_SET_RUN_BACK_SPEED                 = 0x0A83,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_MOVE_SET_WALK_SPEED_CHEAT                = 0x10D1,    // (no client leaf)
    SMSG_MOVE_SET_WALK_SPEED                     = 0x0469,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_MOVE_SET_SWIM_SPEED_CHEAT                = 0x10D3,    // 5.4.8 18414 (Wow.exe leaf)
    SMSG_MOVE_SET_SWIM_SPEED                     = 0x0817,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_MOVE_SET_SWIM_BACK_SPEED_CHEAT           = 0x10D5,    // (no client leaf)
    SMSG_MOVE_SET_SWIM_BACK_SPEED                = 0x0962,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_MOVE_SET_ALL_SPEED_CHEAT                 = 0x10D7,    // (no client leaf)
    MSG_MOVE_SET_TURN_RATE_CHEAT                 = 0x10D8,    // 5.4.8 18414 (Wow.exe leaf)
    SMSG_MOVE_SET_TURN_RATE                      = 0x0069,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_MOVE_TOGGLE_COLLISION_CHEAT              = 0x0BC8,    // 5.4.1 17538 (no client leaf)
    CMSG_MOVE_SET_FACING                         = 0x1050, // 5.4.8 18414
    MSG_MOVE_WORLDPORT_ACK                       = 0x1FAD,    // 5.4.8 18414 (retail: 2022 zero-length CMSG, one per SMSG_NEW_WORLD)
    SMSG_MONSTER_MOVE                            = 0x1A07,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_MOVE_WATER_WALK                         = 0x1F9A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_MOVE_LAND_WALK                          = 0x086A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_FORCE_RUN_SPEED_CHANGE                  = 0x10E3,    // 5.4.8 18414 (Wow.exe leaf; name legacy)
    CMSG_FORCE_RUN_SPEED_CHANGE_ACK              = 0x10F3, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_RUN_SPEED_CHANGE_ACK)
    SMSG_FORCE_RUN_BACK_SPEED_CHANGE             = 0x10E5,    // (legacy; no client leaf)
    CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK         = 0x0158, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_RUN_BACK_SPEED_CHANGE_ACK)
    SMSG_FORCE_SWIM_SPEED_CHANGE                 = 0x10E7,    // (legacy; no client leaf)
    CMSG_FORCE_SWIM_SPEED_CHANGE_ACK             = 0x1853, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_SWIM_SPEED_CHANGE_ACK)
    SMSG_FORCE_MOVE_ROOT                         = 0x15AE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_MOVE_ROOT)
    CMSG_FORCE_MOVE_ROOT_ACK                     = 0x107A, // 5.4.8 18414 (Wow.exe binary)
    SMSG_FORCE_MOVE_UNROOT                       = 0x1FAE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_MOVE_UNROOT)
    CMSG_FORCE_MOVE_UNROOT_ACK                   = 0x1051, // 5.4.8 18414 (Wow.exe binary)
    MSG_MOVE_ROOT                                = 0x10ED,    // (no client leaf)
    MSG_MOVE_UNROOT                              = 0x10EE,    // (no client leaf)
    MSG_MOVE_HEARTBEAT                           = 0x01F2,    // 5.4.8 18414 (Wow.exe leaf)
    SMSG_MOVE_KNOCK_BACK                         = 0x0562,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_MOVE_KNOCK_BACK_ACK                     = 0x00F2, // 5.4.8 18414 (Wow.exe binary)
    SMSG_MOVE_UPDATE_KNOCK_BACK                  = 0x0251,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_MOVE_FEATHER_FALL                       = 0x0C60,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_MOVE_NORMAL_FALL                        = 0x08E0,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_MOVE_SET_HOVER                          = 0x1802,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_MOVE_UNSET_HOVER                        = 0x02D3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_MOVE_HOVER_ACK                          = 0x0858, // 5.4.8 18414 (Wow.exe binary)
    MSG_MOVE_HOVER                               = 0x10F8,    // (no client leaf)
    CMSG_OPENING_CINEMATIC                       = 0x0130, // 5.4.8 18414 (Wow.exe binary)
    SMSG_TRIGGER_CINEMATIC                       = 0x0B01,    // 5.4.8 18414 (Wow.exe binary; dynamic handler sub_7AD161)
    CMSG_NEXT_CINEMATIC_CAMERA                   = 0x1124, // 5.4.8 18414 (Wow.exe binary)
    CMSG_COMPLETE_CINEMATIC                      = 0x1F34, // 5.4.8 18414 (Wow.exe binary)
    SMSG_TUTORIAL_FLAGS                          = 0x1B90,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_TUTORIAL_FLAG                           = 0x1D36, // 5.4.8 18414 (Wow.exe inline writer sub_8E9B9A; one uint32)
    CMSG_TUTORIAL_CLEAR                          = 0x0F23, // 5.4.8 18414 (Wow.exe inline writer sub_8E9C2F; empty)
    CMSG_TUTORIAL_RESET                          = 0x0307, // 5.4.8 18414 (Wow.exe inline writer sub_8E9CB3; empty)
    CMSG_STANDSTATECHANGE                        = 0x03E6, // 5.4.8 18414 (Wow.exe writer sub_6905C5; body sub_686F54)
    CMSG_EMOTE                                   = 0x1924, // 5.4.8 18414 (Wow.exe writer sub_7B0108; one uint32)
    SMSG_EMOTE                                   = 0x0987, // 5.4.8 18414 (dynamic handler 0x7AD956; uint32 then uint64)
    CMSG_TEXT_EMOTE                              = 0x07E9, // 5.4.8 18414 (writer sub_6886F6; two uint32 plus packed GUID)
    SMSG_TEXT_EMOTE                              = 0x002E, // 5.4.8 18414 (reader sub_6C4C44; two packed GUIDs plus two uint32)
    CMSG_AUTOEQUIP_GROUND_ITEM                   = 0x1107, // not in 5.4.8 (legacy)
    CMSG_AUTOSTORE_LOOT_ITEM                     = 0x0354, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUTOEQUIP_ITEM                          = 0x025F, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUTOSTORE_BAG_ITEM                      = 0x067C, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SWAP_ITEM                               = 0x035D, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SWAP_INV_ITEM                           = 0x03DF, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SPLIT_ITEM                              = 0x02EC, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUTOEQUIP_ITEM_SLOT                     = 0x036F, // 5.4.8 18414 (Wow.exe binary)
    SMSG_INVENTORY_CHANGE_FAILURE                = 0x0C1E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_OPEN_CONTAINER                          = 0x14BB,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_INSPECT                                 = 0x1259, // 5.4.8 18414 (Wow.exe binary)
    SMSG_INSPECT_RESULTS_UPDATE                  = 0x0B98,    // 5.3.0 17128 (no client leaf)
    CMSG_INITIATE_TRADE                          = 0x0267, // 5.4.8 18414 (Wow.exe binary)
    CMSG_BEGIN_TRADE                             = 0x1CE3, // 5.4.8 18414 (Wow.exe binary)
    CMSG_BUSY_TRADE                              = 0x14E0, // 5.4.8 18414 (Wow.exe binary)
    CMSG_IGNORE_TRADE                            = 0x0276, // 5.4.8 18414 (Wow.exe binary)
    CMSG_ACCEPT_TRADE                            = 0x144D, // 5.4.8 18414 (Wow.exe binary)
    CMSG_UNACCEPT_TRADE                          = 0x0023, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CANCEL_TRADE                            = 0x1941, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SET_TRADE_ITEM                          = 0x03D5, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CLEAR_TRADE_ITEM                        = 0x00A7, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SET_TRADE_GOLD                          = 0x14E3, // 5.4.8 18414 (Wow.exe binary)
    SMSG_TRADE_STATUS                            = 0x1963, // 5.4.8 18414 (Wow.exe reader/handler; compact trade state)
    SMSG_TRADE_STATUS_EXTENDED                   = 0x181E, // 5.4.8 18414 (Wow.exe reader/handler; trade contents)
    SMSG_INITIALIZE_FACTIONS                     = 0x0AAA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SET_FACTION_VISIBLE                     = 0x1E8E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SET_FACTION_STANDING                    = 0x10AA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_FACTION_ATWAR                       = 0x027B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SET_PROFICIENCY                         = 0x1440,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_ACTION_BUTTON                       = 0x1F8C, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ACTION_BUTTONS                          = 0x081A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_INITIAL_SPELLS                          = 0x045A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_REQUEST_CATEGORY_COOLDOWNS               = 0x1203, // 5.4.8 18414 (Wow.exe empty writer; local-player initialization path)
    SMSG_CATEGORY_COOLDOWN                        = 0x01DB, // 5.4.8 18414 (Wow.exe JamCategoryCooldown reader and player-state handler)
    SMSG_LEARNED_SPELL                           = 0x129A,    // 5.4.8 18414 (reader sub_70D8B8; add-spell leaf sub_7C0F79)
    SMSG_SUPERCEDED_SPELL                        = 0x1943,    // 5.4.8 18414 (reader sub_716563; replace-spell leaf sub_7C1033)
    CMSG_CAST_SPELL                              = 0x0206, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CANCEL_CAST                             = 0x18C0, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CAST_FAILED                             = 0x143A,    // 5.4.8 18414 (Wow.exe reader + Spell_C handler; name reference-consensus)
    SMSG_SPELL_START                             = 0x107A,    // 5.4.8 18414 (Wow.exe reader + Spell_C handler; name via bridge)
    SMSG_SPELL_GO                                = 0x09D8,    // 5.4.8 18414 (Wow.exe reader + Spell_C handler; name via bridge)
    SMSG_SPELL_FAILURE                           = 0x04AF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPELL_COOLDOWN                          = 0x0452,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_COOLDOWN_EVENT                          = 0x1163,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CANCEL_AURA                             = 0x1861, // 5.4.8 18414 (Wow.exe binary)
    SMSG_EQUIPMENT_SET_ID                        = 0x0006,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PET_CAST_FAILED                         = 0x149B,    // 5.4.8 18414 (Wow.exe reader + Spell_C handler; name reference-consensus)
    SMSG_CHANNEL_START                           = 0x10F9,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_SPELL_CHANNEL_START)
    SMSG_CHANNEL_UPDATE                          = 0x11D9,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_SPELL_CHANNEL_UPDATE)
    CMSG_CANCEL_CHANNELLING                      = 0x08C0, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AI_REACTION                             = 0x06AF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_SELECTION                           = 0x0740, // 5.4.8 18414 (Wow.exe writer sub_686CCE; body sub_68CB73)
    CMSG_EQUIPMENT_SET_DELETE                    = 0x02E8, // 5.4.8 18414 (Wow.exe binary)
    CMSG_INSTANCE_LOCK_RESPONSE                  = 0x12C0, // 5.4.8 18414 (Wow.exe binary, via CMSG_INSTANCE_LOCK_WARNING_RESPONSE)
    CMSG_ATTACKSWING                             = 0x02E7, // 5.4.8 18414 (writer sub_694D82; one packed GUID)
    CMSG_ATTACKSTOP                              = 0x0345, // 5.4.8 18414 (writer nullsub_2; empty)
    SMSG_ATTACKSTART                             = 0x1A9E, // 5.4.8 18414 (reader sub_6F591D; two packed GUIDs)
    SMSG_ATTACKSTOP                              = 0x12AF, // 5.4.8 18414 (reader sub_6E24A3; two packed GUIDs plus one bit)
    SMSG_ATTACKSWING_NOTINRANGE                  = 0x0B36,    // (legacy; no client leaf)
    SMSG_ATTACKSWING_BADFACING                   = 0x6C07,    // (legacy; no client leaf; unframable >0x1FFF)
    SMSG_PENDING_RAID_LOCK                       = 0x4F17,    // (legacy; no client leaf; unframable >0x1FFF)
    SMSG_ATTACKSWING_DEADTARGET                  = 0x2B26,    // (legacy; no client leaf; unframable >0x1FFF)
    SMSG_ATTACKSWING_CANT_ATTACK                 = 0x0016,    // (legacy; no client leaf)
    SMSG_ATTACKERSTATEUPDATE                     = 0x06AA,    // 5.4.8 18414 (direct nested reader and UnitCombat_C terminal)
    SMSG_BATTLEFIELD_PORT_DENIED                 = 0x149A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_RESUME_CAST_BAR                         = 0x01D2,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_CANCEL_COMBAT                           = 0x0E8B,    // 5.4.8 18414 (direct empty reader and local-player combat-cancel terminal)
    SMSG_SPELLBREAKLOG                           = 0x6B17,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_SPELLHEALLOG                            = 0x09FB,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_SPELLENERGIZELOG                        = 0x0D79,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_BREAK_TARGET                            = 0x021A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_BINDPOINTUPDATE                         = 0x0E3B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_BINDZONEREPLY                           = 0x1158,    // (legacy; no client leaf)
    SMSG_PLAYERBOUND                             = 0x088E,    // 5.4.8 18414 (Wow.exe retained literal and reader)
    SMSG_CLIENT_CONTROL_UPDATE                   = 0x1043,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REPOP_REQUEST                           = 0x134A, // 5.4.8 18414 (Wow.exe binary)
    SMSG_RESURRECT_REQUEST                       = 0x1062,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_RESURRECT_RESPONSE                      = 0x0B0C, // 5.4.8 18414 (Wow.exe binary)
    CMSG_RETURN_TO_GRAVEYARD                     = 0x12EA, // 5.4.8 18414 (Wow.exe binary)
    CMSG_REQUEST_CEMETERY_LIST                   = 0x06E4, // 5.4.8 18414 (Wow.exe binary)
    SMSG_REQUEST_CEMETERY_LIST_RESPONSE          = 0x042A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_LOOT                                    = 0x1CE2, // 5.4.8 18414 (Wow.exe binary)
    CMSG_LOOT_CURRENCY                           = 0x781C, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    CMSG_LOOT_MONEY                              = 0x02F6, // 5.4.8 18414 (Wow.exe binary)
    CMSG_LOOT_RELEASE                            = 0x0840, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LOOT_RESPONSE                           = 0x128A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LOOT_RELEASE_RESPONSE                   = 0x123F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LOOT_REMOVED                            = 0x0C3E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LOOT_MONEY_NOTIFY                       = 0x14C0,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_UNKNOWN_080F                            = 0x080F,    // 5.4.8 18414 (Wow.exe leaf; direct reader is a loot-roll batch, exact name unresolved)
    SMSG_LOOT_CLEAR_MONEY                        = 0x1632,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_ITEM_PUSH_RESULT                        = 0x0E0A,    // 5.4.8 18414 (direct GameUI item-push reader/terminal)
    SMSG_DUEL_REQUESTED                          = 0x0022,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_DUEL_OUTOFBOUNDS                        = 0x001A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_DUEL_INBOUNDS                           = 0x163A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_DUEL_COMPLETE                           = 0x1C0A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_DUEL_WINNER                             = 0x10E1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_DUEL_ACCEPTED                           = 0x2136, // not in 5.4.8 (legacy; handler retained)
    CMSG_DUEL_CANCELLED                          = 0x6624, // not in 5.4.8 (legacy; handler retained)
    SMSG_MOUNTRESULT                             = 0x0E0F,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_DISMOUNTRESULT                          = 0x062F,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_REMOVED_FROM_PVP_QUEUE                  = 0x1171,    // (legacy; no client leaf)
    CMSG_MOUNTSPECIAL_ANIM                       = 0x0082, // 5.4.8 18414 (Wow.exe binary)
    SMSG_MOUNTSPECIAL_ANIM                       = 0x003A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PET_TAME_FAILURE                        = 0x040E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_PET_SET_ACTION                          = 0x12E9, // 5.4.8 18414 (Wow.exe binary)
    CMSG_PET_ACTION                              = 0x025B, // 5.4.8 18414 (Wow.exe binary)
    CMSG_PET_ABANDON                             = 0x07D0, // 5.4.8 18414 (Wow.exe binary)
    CMSG_PET_RENAME                              = 0x0A32, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PET_NAME_INVALID                        = 0x028E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PET_SPELLS                              = 0x095A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_PET_SPELLS_MESSAGE)
    SMSG_PET_MODE                                = 0x163F,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_GOSSIP_HELLO                            = 0x12F3, // 5.4.8 18414 (writer sub_68C04B; packed GUID)
    CMSG_GOSSIP_SELECT_OPTION                    = 0x0748, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GOSSIP_MESSAGE                          = 0x0244,    // 5.4.8 18414 (reader sub_6B5DFB; gossip and quest collections)
    SMSG_GOSSIP_COMPLETE                         = 0x034E, // 5.4.8 18414 (Wow.exe direct empty reader + gossip-close path)
    CMSG_NPC_TEXT_QUERY                          = 0x0287, // 5.4.8 18414 (writer 0x69056F; WNPC cache sender 0x5FFD2D)
    SMSG_NPC_TEXT_UPDATE                         = 0x140A,    // 5.4.8 18414 (reader 0x6B2BEE; BroadcastText.db2 record)
    SMSG_NPC_WONT_TALK                           = 0x1182,    // (legacy; no client leaf)
    CMSG_QUESTGIVER_STATUS_QUERY                 = 0x036A, // 5.4.8 18414 (Wow.exe writer sub_686DC6; body sub_68CFAD)
    SMSG_QUESTGIVER_STATUS                       = 0x1275,    // 5.4.8 18414 (Wow.exe reader sub_6AEF41; Player_C.cpp leaf 0x7ADDBD)
    CMSG_QUESTGIVER_HELLO                        = 0x02DB, // 5.4.8 18414 (Wow.exe direct writer + semantic path)
    SMSG_QUESTGIVER_QUEST_LIST                   = 0x02D4, // 5.4.8 18414 (Wow.exe direct reader + QuestFrame path)
    CMSG_QUESTGIVER_QUERY_QUEST                  = 0x12F0, // 5.4.8 18414 (Wow.exe direct writer + retained UI binding)
    CMSG_QUESTGIVER_QUEST_AUTOLAUNCH             = 0x1188, // not in 5.4.8 (legacy; handler retained)
    SMSG_QUESTGIVER_QUEST_DETAILS                = 0x134C, // 5.4.8 18414 (Wow.exe direct reader + QuestFrame path)
    CMSG_QUESTGIVER_ACCEPT_QUEST                 = 0x06D1, // 5.4.8 18414 (Wow.exe direct writer + AcceptQuest path)
    CMSG_QUESTGIVER_COMPLETE_QUEST               = 0x0659, // 5.4.8 18414 (Wow.exe binary)
    SMSG_QUESTGIVER_REQUEST_ITEMS                = 0x0277,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_QUESTGIVER_REQUEST_REWARD               = 0x0378, // 5.4.8 18414 (Wow.exe binary)
    SMSG_QUESTGIVER_OFFER_REWARD                 = 0x074F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_QUESTGIVER_CHOOSE_REWARD                = 0x07CB, // 5.4.8 18414 (Wow.exe binary)
    SMSG_QUESTGIVER_QUEST_INVALID                = 0x027D, // 5.4.8 18414 (Wow.exe nullable-string + reason reader; retained error callback)
    CMSG_QUESTGIVER_CANCEL                       = 0x1191, // not in 5.4.8 (legacy; handler retained)
    SMSG_QUESTGIVER_QUEST_COMPLETE               = 0x0346,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_QUESTGIVER_QUEST_FAILED                 = 0x12DE, // 5.4.8 18414 (Wow.exe quest/reason reader; quest-cache failure callback)
    CMSG_QUESTLOG_SWAP_QUEST                     = 0x1194, // not in 5.4.8 (legacy; handler retained)
    CMSG_QUESTLOG_REMOVE_QUEST                   = 0x0779, // 5.4.8 18414 (Wow.exe binary)
    SMSG_QUESTLOG_FULL                           = 0x07FD, // 5.4.8 18414 (Wow.exe empty reader; ERR_QUEST_LOG_FULL callback)
    SMSG_QUESTUPDATE_FAILED                      = 0x07DD,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_QUESTUPDATE_FAILEDTIMER                 = 0x06FF, // 5.4.8 18414 (Wow.exe uint32 quest reader; timed-quest failure callback)
    SMSG_QUESTUPDATE_COMPLETE                    = 0x0776,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_QUESTUPDATE_ADD_KILL                    = 0x1645, // 5.4.8 18414 (Wow.exe quest-progress reader; name reference-backed)
    SMSG_QUESTUPDATE_ADD_ITEM_OBSOLETE           = 0x119B,    // (legacy; no client leaf)
    CMSG_QUEST_CONFIRM_ACCEPT                    = 0x124B, // 5.4.8 18414 (Wow.exe writer; ConfirmAcceptQuest path)
    SMSG_QUEST_CONFIRM_ACCEPT                    = 0x13C7, // 5.4.8 18414 (Wow.exe reader; QUEST_ACCEPT_CONFIRM event)
    SMSG_INITIAL_SETUP                           = 0x0A8B, // 5.4.8 18414 (Wow.exe leaf/semantics; name via 5.4.7 bridge)
    SMSG_SET_QUEST_COMPLETED_BIT                 = 0x0354, // 5.4.8 18414 (Wow.exe leaf/bitset semantics; name fork-sourced)
    SMSG_CLEAR_QUEST_COMPLETED_BIT               = 0x03EC, // 5.4.8 18414 (Wow.exe leaf/bitset semantics; name fork-sourced)
    SMSG_CLEAR_QUEST_COMPLETED_BITS              = 0x0364, // 5.4.8 18414 (Wow.exe leaf/bitset semantics; name fork-sourced)
    CMSG_PUSHQUESTTOPARTY                        = 0x03D2, // 5.4.8 18414 (Wow.exe writer; QuestLogPushQuest path)
    CMSG_QUEST_PUSH_RESULT                       = 0x1370, // 5.4.8 18414 (Wow.exe writer; DeclineQuest path)
    CMSG_LIST_INVENTORY                          = 0x02D8, // 5.4.8 18414 (writer sub_C86CB0; packed GUID)
    SMSG_LIST_INVENTORY                          = 0x1AAE, // 5.4.8 18414 (reader sub_73095E; 18-bit item list)
    CMSG_SELL_ITEM                               = 0x1358, // 5.4.8 18414 (writer sub_68C647; live request correlated)
    SMSG_SELL_ITEM                               = 0x048E, // 5.4.8 18414 (reader sub_6D0990; terminal sub_7B206F sale errors)
    CMSG_BUY_ITEM                                = 0x02E2, // 5.4.8 18414 (writer sub_68E11F)
    SMSG_BUY_ITEM                                = 0x101A, // 5.4.8 18414 (reader sub_6E1DCB; terminal sub_7B204A; name reference-consensus)
    SMSG_BUY_FAILED                              = 0x1563, // 5.4.8 18414 (reader sub_6EB397; terminal sub_7AB1AA; name reference-consensus)
    SMSG_SHOWTAXINODES                           = 0x1E1A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; via SMSG_SHOW_TAXI_NODES)
    CMSG_TAXINODE_STATUS_QUERY                   = 0x02E1, // 5.4.8 18414 (Wow.exe binary, via CMSG_TAXI_NODE_STATUS_QUERY)
    SMSG_TAXINODE_STATUS                         = 0x169E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; via SMSG_TAXI_NODE_STATUS)
    CMSG_TAXIQUERYAVAILABLENODES                 = 0x02E3, // 5.4.8 18414 (Wow.exe binary, via CMSG_TAXI_QUERY_AVAILABLE_NODES)
    CMSG_ACTIVATETAXI                            = 0x03C9, // 5.4.8 18414 (Wow.exe binary, via CMSG_ACTIVATE_TAXI)
    SMSG_ACTIVATETAXIREPLY                       = 0x02A7,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; via SMSG_ACTIVATE_TAXI_REPLY)
    SMSG_NEW_TAXI_PATH                           = 0x141B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_TRAINER_LIST                            = 0x034B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_TRAINER_LIST                            = 0x189F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_TRAINER_BUY_SPELL                       = 0x0352, // 5.4.8 18414 (Wow.exe binary)
    SMSG_TRAINER_SERVICE                         = 0x6A05,    // (legacy; no client leaf; unframable >0x1FFF)
    SMSG_TRAINER_BUY_FAILED                      = 0x042E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_BINDER_ACTIVATE                         = 0x1248, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PLAYERBINDERROR                         = 0x6A24,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    CMSG_BANKER_ACTIVATE                         = 0x02E9, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SHOW_BANK                               = 0x0007,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_BUY_BANK_SLOT                           = 0x12F2, // 5.4.8 18414 (Wow.exe binary)
    CMSG_PETITION_SHOWLIST                       = 0x037B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PETITION_SHOWLIST                       = 0x10A3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_PETITION_BUY                            = 0x12D9, // 5.4.8 18414 (Wow.exe binary)
    CMSG_PETITION_SHOW_SIGNATURES                = 0x136B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PETITION_SHOW_SIGNATURES                = 0x00AA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_PETITION_SIGN                           = 0x06DA, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PETITION_SIGN_RESULTS                   = 0x06AE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_OFFER_PETITION                          = 0x15BE, // 5.4.8 18414 (Wow.exe binary)
    CMSG_TURN_IN_PETITION                        = 0x0673, // 5.4.8 18414 (Wow.exe binary)
    SMSG_TURN_IN_PETITION_RESULTS                = 0x0E13,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_PETITION_QUERY                          = 0x0255, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PETITION_QUERY_RESPONSE                 = 0x1083,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_FISH_NOT_HOOKED                         = 0x10BE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_FISH_ESCAPED                            = 0x0227,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_BUG                                     = 0x09E1, // 5.4.8 18414 (Wow.exe binary)
    SMSG_NOTIFICATION                            = 0x0C2A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_PLAYED_TIME                             = 0x03F6, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PLAYED_TIME                             = 0x11E2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_QUERY_TIME                              = 0x0640, // 5.4.8 18414 (Wow.exe binary)
    SMSG_QUERY_TIME_RESPONSE                     = 0x100F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LOG_XPGAIN                              = 0x1E9A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_AURACASTLOG                             = 0x11D2,    // (legacy; no client leaf)
    CMSG_RECLAIM_CORPSE                          = 0x03D3, // 5.4.8 18414 (Wow.exe binary)
    CMSG_WRAP_ITEM                               = 0x02DF, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LEVELUP_INFO                            = 0x1961,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_MINIMAP_PING                            = 0x0837, // 5.4.8 18414 (Wow.exe binary serializer)
    SMSG_RESISTLOG                               = 0x11D7,    // (legacy; no client leaf)
    SMSG_ENCHANTMENTLOG                          = 0x12A3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; via SMSG_ENCHANTMENT_LOG)
    SMSG_START_MIRROR_TIMER                      = 0x0E12,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PAUSE_MIRROR_TIMER                      = 0x162E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_STOP_MIRROR_TIMER                       = 0x1026,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_PING                                    = 0x0012, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PONG                                    = 0x1969,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CLEAR_COOLDOWNS                         = 0x1458,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_GAMEOBJECT_PAGETEXT                     = 0x14AF,    // 5.4.8 18414 (Wow.exe binary; direct reader)
    CMSG_SETSHEATHED                             = 0x0249, // 5.4.8 18414 (Wow.exe writer sub_6867F6; body sub_66659D)
    SMSG_COOLDOWN_CHEAT                          = 0x0432,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPELL_DELAYED                           = 0x087A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_QUEST_POI_QUERY                         = 0x10C2, // 5.4.8 18414 (Wow.exe binary)
    CMSG_QUEST_NPC_QUERY                         = 0x1DAE, // 5.4.8 18414 (live client sends it; name reference-consensus)
    SMSG_QUEST_POI_QUERY_RESPONSE                = 0x067F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_INVALID_PROMOTION_CODE                  = 0x1A0E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    MSG_GM_BIND_OTHER                            = 0x11E9,    // (no client leaf)
    MSG_GM_SUMMON                                = 0x11EA,    // (no client leaf)
    SMSG_ITEM_TIME_UPDATE                        = 0x18C1,    // 5.4.8 18414 (Wow.exe binary; reader sub_6F06A8)
    SMSG_ITEM_ENCHANT_TIME_UPDATE                = 0x10A2,    // 5.4.8 18414 (Wow.exe binary; reader sub_6C8203)
    MSG_GM_SHOWLABEL                             = 0x11F0,    // (no client leaf)
    CMSG_PET_CAST_SPELL                          = 0x044D, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SAVE_GUILD_EMBLEM                      = 0x1D60, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SAVE_GUILD_EMBLEM                      = 0x089F, // 5.4.8 18414 (Wow.exe binary)
    CMSG_TABARD_VENDOR_ACTIVATE                 = 0x11C3, // 5.4.8 18414 (Wow.exe binary)
    MSG_SAVE_GUILD_EMBLEM                        = 0x2404,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    MSG_TABARDVENDOR_ACTIVATE                    = 0x6926,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    SMSG_PLAY_SPELL_VISUAL                       = 0x061E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_ZONEUPDATE                              = 0x0265, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PARTYKILLLOG                            = 0x048A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_COMPRESSED_UPDATE_OBJECT                = 0x11F7,    // (no client leaf)
    SMSG_EXPLORATION_EXPERIENCE                  = 0x189A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_RANDOM_ROLL                              = 0x0905,    // 4.3.4 15595 (no client leaf; superseded by CMSG_RANDOM_ROLL)
    CMSG_RANDOM_ROLL                             = 0x08A3,    // 5.4.8 18414 (Wow.exe binary: sub_CC28BB -> sub_660C6B -> sub_661642 writes 2211)
    SMSG_ENVIRONMENTALDAMAGELOG                  = 0x0DF1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CHANGEPLAYER_DIFFICULTY                 = 0x1F8E, // 5.4.8 18414
    SMSG_RWHOIS                                  = 0x11FF,    // (value unverified; no client leaf)
    SMSG_LFG_PLAYER_REWARD                       = 0x121A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LFG_TELEPORT_DENIED                     = 0x063B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_UNLEARN_SKILL                           = 0x0268, // 5.4.8 18414 (Wow.exe binary)
    SMSG_REMOVED_SPELL                           = 0x14C3,    // 5.4.8 18414 (reader sub_6B0AD9; remove-spell leaf sub_7BFC9F)
    CMSG_GMTICKET_CREATE                         = 0x1A86, // 5.4.8 18414 (Wow.exe binary, via CMSG_GM_TICKET_CREATE)
    SMSG_GMTICKET_CREATE                         = 0x2107,    // (legacy; no client leaf; unframable >0x1FFF)
    CMSG_GMTICKET_UPDATETEXT                     = 0x0A26, // 5.4.8 18414 (Wow.exe binary, via CMSG_GM_TICKET_UPDATE_TEXT)
    SMSG_GMTICKET_UPDATETEXT                     = 0x6535,    // (legacy; no client leaf; unframable >0x1FFF)
    SMSG_ACCOUNT_DATA_TIMES                      = 0x162B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REQUEST_ACCOUNT_DATA                    = 0x1D8A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_UPDATE_ACCOUNT_DATA                     = 0x0068, // 5.4.8 18414 (Wow.exe binary)
    SMSG_UPDATE_ACCOUNT_DATA                     = 0x0AAE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CLEAR_FAR_SIGHT_IMMEDIATE               = 0x2A04,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_CHANGEPLAYER_DIFFICULTY_RESULT          = 0x2217,    // (legacy; no client leaf; unframable >0x1FFF)
    CMSG_GMTICKET_GETTICKET                      = 0x1F89, // 5.4.8 18414 (Wow.exe binary, via CMSG_GM_TICKET_GET_TICKET)
    SMSG_GMTICKET_GETTICKET                      = 0x129B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; via SMSG_GM_TICKET_GET_TICKET)
    SMSG_INSTANCE_ENCOUNTER                      = 0x1215,    // (no client leaf)
    SMSG_GAMEOBJECT_DESPAWN_ANIM                 = 0x108B,    // 5.4.8 18414 (Wow.exe binary; direct reader)
    CMSG_GMTICKET_DELETETICKET                   = 0x1A23, // 5.4.8 18414 (Wow.exe binary, via CMSG_GM_TICKET_DELETE_TICKET)
    SMSG_GMTICKET_DELETETICKET                   = 0x6D17,    // (legacy; no client leaf; unframable >0x1FFF)
    CMSG_GMTICKET_SYSTEMSTATUS                   = 0x0A82, // 5.4.8 18414 (Wow.exe binary, via CMSG_GM_TICKET_SYSTEM_STATUS)
    SMSG_GMTICKET_SYSTEMSTATUS                   = 0x163B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; via SMSG_GM_TICKET_SYSTEM_STATUS)
    CMSG_SPIRIT_HEALER_ACTIVATE                  = 0x0340, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SPIRIT_HEALER_CONFIRM                   = 0x1EAA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_GOSSIP_POI                              = 0x0785,    // 5.4.8 18414 (Wow.exe dynamic slot 229; handler 0x9639BC)
    CMSG_CHAT_IGNORED                            = 0x048A, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GM_PLAYER_INFO                          = 0x102B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    // Confirmed absent from 5.4.8, three ways: no thunk writes 0x1024 anywhere in
    // Wow.exe.lst, the 18414 corpus has zero rows for it in either direction, and
    // the value survives here only as a pre-MoP declaration. Its handler was dead
    // 4.3.4 code and has been removed. Kept declared so the value is not reused.
    CMSG_GUILD_RANK                              = 0x1024, // not in 5.4.8 (legacy)
    CMSG_GUILD_ADD_RANK                          = 0x047A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_DEL_RANK                          = 0x0D79, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_SET_NOTE                          = 0x05DA, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LOGIN_VERIFY_WORLD                      = 0x1C0F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SEND_MAIL                               = 0x1DBA, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SEND_MAIL_RESULT                        = 0x1A9B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GET_MAIL_LIST                           = 0x077A, // 5.4.8 18414 (Wow.exe binary)
    SMSG_MAIL_LIST_RESULT                        = 0x1C0B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_BATTLEFIELD_LIST                        = 0x1C41, // 5.4.8 18414 (Wow.exe binary)
    SMSG_BATTLEFIELD_LIST                        = 0x160E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_FORCE_SET_VEHICLE_REC_ID                = 0x1240,    // (no client leaf)
    CMSG_SET_VEHICLE_REC_ID_ACK                  = 0x185B, // 5.4.8 18414 (Wow.exe binary)
    CMSG_ITEM_TEXT_QUERY                         = 0x0123, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ITEM_TEXT_QUERY_RESPONSE                = 0x1134,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_MAIL_TAKE_MONEY                         = 0x06FA, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MAIL_TAKE_ITEM                          = 0x1371, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MAIL_MARK_AS_READ                       = 0x0241, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MAIL_RETURN_TO_SENDER                   = 0x1FA8, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MAIL_DELETE                             = 0x14E2, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MAIL_CREATE_TEXT_ITEM                   = 0x1270, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SPELLLOGMISS                            = 0x1570,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPELLLOGEXECUTE                         = 0x0626,    // (legacy; no client leaf)
    SMSG_DEBUGAURAPROC                           = 0x124E,    // (legacy; no client leaf)
    SMSG_PERIODICAURALOG                         = 0x0416,    // (legacy; no client leaf)
    SMSG_SPELLDAMAGESHIELD                       = 0x05F3,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_SPELLNONMELEEDAMAGELOG                  = 0x1450,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_LEARN_TALENT                            = 0x02A7, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CONFIRM_RESPEC_WIPE                     = 0x0275, // 5.4.8 18414 (Wow.exe serializer; name reference-consensus)
    SMSG_RESURRECT_FAILED                        = 0x1253,    // (legacy; no client leaf)
    CMSG_TOGGLE_PVP                              = 0x0644, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ZONE_UNDER_ATTACK                       = 0x10C2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_AUCTION_HELLO                           = 0x0379, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AUCTION_HELLO                           = 0x10A7,    // 5.4.8 18414 (Wow.exe leaf and body)
    CMSG_AUCTION_SELL_ITEM                       = 0x02EB, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUCTION_REMOVE_ITEM                     = 0x0259, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUCTION_LIST_ITEMS                      = 0x02EA, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUCTION_LIST_OWNER_ITEMS                = 0x0361, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUCTION_PLACE_BID                       = 0x03C8, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AUCTION_COMMAND_RESULT                  = 0x1002,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_AUCTION_LIST_RESULT                     = 0x0982,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_AUCTION_OWNER_LIST_RESULT               = 0x1785,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_AUCTION_WON_NOTIFICATION                = 0x11C1,    // 5.4.8 18414 (Wow.exe won effect and body)
    SMSG_AUCTION_OWNER_NOTIFICATION              = 0x1A8E,    // 5.4.8 18414 (Wow.exe sold/expired effect and body)
    SMSG_AUCTION_BID_UPDATE_NOTIFICATION         = 0x18AE,    // 5.4.8 18414 (Wow.exe bid-update leaf and body)
    SMSG_AUCTION_OUTBID_NOTIFICATION             = 0x1A9F,    // 5.4.8 18414 (Wow.exe outbid effect and body)
    SMSG_PROCRESIST                              = 0x12BE,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_COMBAT_EVENT_FAILED                     = 0x18C3,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_DISPEL_FAILED                           = 0x085B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPELLOGDAMAGE_IMMUNE                    = 0x4507,    // (legacy; no client leaf; unframable >0x1FFF)
    CMSG_AUCTION_LIST_BIDDER_ITEMS               = 0x12D0, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AUCTION_BIDDER_LIST_RESULT              = 0x0B24,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SET_FLAT_SPELL_MODIFIER                 = 0x10F2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SET_PCT_SPELL_MODIFIER                  = 0x09D3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_AMMO                                = 0x1269, // not in 5.4.8 (legacy; handler retained)
    SMSG_CORPSE_RECLAIM_DELAY                    = 0x022A,    // 5.4.8 18414 (Wow.exe binary; reader sub_6D7781)
    CMSG_SET_ACTIVE_MOVER                        = 0x09F0, // 5.4.8 18414 (Wow.exe binary)
    CMSG_PET_CANCEL_AURA                         = 0x12DA, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CANCEL_AUTO_REPEAT_SPELL                = 0x1272, // 5.4.8 18414 (Wow.exe binary)
    MSG_GM_ACCOUNT_ONLINE                        = 0x0889,    // 5.3.0 17128 (no client leaf)
    MSG_LIST_STABLED_PETS                        = 0x0834,    // 4.3.4 15595 (no client leaf)
    CMSG_REQUEST_STABLED_PETS                    = 0x02CA, // 5.4.8 18414 (Wow.exe binary)
    CMSG_STABLE_PET                              = 0x1271, // not in 5.4.8 (legacy; handler retained)
    CMSG_UNSTABLE_PET                            = 0x1272, // not in 5.4.8 (legacy; handler retained)
    CMSG_BUY_STABLE_SLOT                         = 0x1273, // not in 5.4.8 (legacy; handler retained)
    SMSG_STABLE_RESULT                           = 0x14BE,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_STABLE_REVIVE_PET                       = 0x1275, // not in 5.4.8 (legacy; handler retained)
    CMSG_STABLE_SWAP_PET                         = 0x1276, // not in 5.4.8 (legacy; handler retained)
    MSG_QUEST_PUSH_RESULT                        = 0x4515,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    SMSG_PLAY_MUSIC                              = 0x0023,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PLAY_OBJECT_SOUND                       = 0x1443,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PLAY_ONE_SHOT_ANIM_KIT                  = 0x043E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REQUEST_PET_INFO                        = 0x135B, // 5.4.8 18414 (Wow.exe binary)
    CMSG_FAR_SIGHT                               = 0x1341, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SPELLDISPELLOG                          = 0x0DF9,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_DAMAGE_CALC_LOG                         = 0x2436,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    CMSG_GROUP_CHANGE_SUB_GROUP                  = 0x1799, // 5.4.8 18414 (Wow.exe binary)
    CMSG_REQUEST_PARTY_MEMBER_STATS              = 0x0806, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GROUP_SWAP_SUB_GROUP                    = 0x1281, // NYI in 5.4.8 refs (value unverified)
    CMSG_RESET_FACTION_CHEAT                     = 0x10B6, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUTOSTORE_BANK_ITEM                     = 0x02CF, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AUTOBANK_ITEM                           = 0x066D, // 5.4.8 18414 (Wow.exe binary)
    MSG_QUERY_NEXT_MAIL_TIME                     = 0x0F04,    // 4.3.4 15595 (no client leaf)
    CMSG_MAIL_QUERY_NEXT_TIME                    = 0x077B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_RECEIVED_MAIL                           = 0x182B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_RAID_GROUP_ONLY                         = 0x0D82,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_SET_PVP_RANK_CHEAT                      = 0x1289, // not in 5.4.8 (legacy)
    CMSG_SET_PVP_TITLE                           = 0x128C, // not in 5.4.8 (legacy)
    SMSG_PVP_CREDIT                              = 0x100A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GROUP_RAID_CONVERT                      = 0x032C, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GROUP_REQUEST_JOIN_UPDATES              = 0x178A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GROUP_ASSISTANT_LEADER                  = 0x1897, // 5.4.8 18414 (Wow.exe binary)
    CMSG_BUYBACK_ITEM                            = 0x0661, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SERVER_MESSAGE                          = 0x0302,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_SAVED_INSTANCE_EXTEND               = 0x0C68, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LFG_OFFER_CONTINUE                      = 0x1EAB,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_TEST_DROP_RATE_RESULT                   = 0x1296,    // (value unverified; no client leaf)
    CMSG_LFG_GET_STATUS                          = 0x032D, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SHOW_MAILBOX                            = 0x1F13,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_RESET_RANGED_COMBAT_TIMER               = 0x1299,    // (legacy; no client leaf)
    SMSG_CHAT_NOT_IN_PARTY                       = 0x0A8A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_CANCEL_GROWTH_AURA                      = 0x0237, // not in 5.4.8 (legacy)
    SMSG_CANCEL_AUTO_REPEAT                      = 0x1E0F,    // 5.4.8 18414 (direct packed-GUID reader and Unit_C clear terminal; name reference-consensus)
    SMSG_STANDSTATE_UPDATE                       = 0x1C12,    // 5.4.8 18414 (Wow.exe Unit_C.cpp leaf 0x810583; one uint8)
    SMSG_LOOT_ALL_PASSED                         = 0x0EBB,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LOOT_ROLL_WON                           = 0x0A3A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_LOOT_ROLL                               = 0x15C2, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LOOT_START_ROLL                         = 0x0EAA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LOOT_ROLL                               = 0x1840,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_LOOT_MASTER_GIVE                        = 0x1DE1, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LOOT_MASTER_LIST                        = 0x02BF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REQUEST_FORCED_REACTIONS                = 0x06F5,    // 5.4.8 18414 (Wow.exe empty writer; reputation initialization call path)
    SMSG_SET_FORCED_REACTIONS                    = 0x068F,    // 5.4.8 18414 (Wow.exe binary; reader sub_72C708)
    SMSG_SPELL_FAILED_OTHER                      = 0x040B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_GAMEOBJECT_RESET_STATE                  = 0x100E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_REPAIR_ITEM                             = 0x02C1, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CHAT_PLAYER_NOT_FOUND                   = 0x1082,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_TALENT_WIPE_CONFIRM                      = 0x0107,    // 4.3.4 15595 (no client leaf)
    SMSG_SUMMON_REQUEST                          = 0x081F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SUMMON_RESPONSE                         = 0x0A33, // 5.4.8 18414 (Wow.exe binary)
    MSG_DEV_SHOWLABEL                            = 0x12AE,    // 5.4.8 18414 (Wow.exe leaf)
    SMSG_MONSTER_MOVE_TRANSPORT                  = 0x2004,    // legacy only: merged into SMSG_MONSTER_MOVE in 5.4.8; never send
    SMSG_PET_BROKEN                              = 0x12B0,    // (value unverified; no client leaf)
    MSG_MOVE_FEATHER_FALL                        = 0x12B1,    // (no client leaf)
    MSG_MOVE_WATER_WALK                          = 0x12B2,    // (no client leaf)
    CMSG_SELF_RES                                = 0x0360, // 5.4.8 18414 (Wow.exe binary)
    SMSG_FEIGN_DEATH_RESISTED                    = 0x029E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SCRIPT_MESSAGE                          = 0x12B7,    // (legacy; no client leaf)
    SMSG_DUEL_COUNTDOWN                          = 0x129F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SHOWING_HELM                            = 0x126B, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SHOWING_CLOAK                           = 0x02F2, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ROLE_CHOSEN                             = 0x1A1F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_LFG_ROLE_CHOSEN)
    SMSG_PLAYER_SKINNED                          = 0x1463,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_DURABILITY_DAMAGE_DEATH                 = 0x1E3E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_ACTIONBAR_TOGGLES                   = 0x0672, // 5.4.8 18414 (Wow.exe binary)
    CMSG_VIOLENCE_LEVEL                         = 0x0040, // 5.4.8 18414 (Wow.exe writer sub_6904E5; literal in sub_C63D45)
    SMSG_INIT_WORLD_STATES                       = 0x1560,    // 5.4.8 18414 (Wow.exe binary; reader sub_7341EC/sub_732740)
    SMSG_UPDATE_WORLD_STATE                      = 0x121B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PET_ACTION_FEEDBACK                     = 0x080E, // 5.4.8 18414 (Wow.exe reader sub_6E3D04; semantic leaf sub_93E7D0)
    CMSG_CHAR_RENAME                             = 0x0963, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CHAR_RENAME                             = 0x0CBF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_MOVE_SPLINE_DONE                        = 0x11D9, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MOVE_FALL_RESET                         = 0x00D9, // 5.4.8 18414 (Wow.exe binary)
    SMSG_INSTANCE_SAVE_CREATED                   = 0x1EAE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_RAID_INSTANCE_INFO                      = 0x16BF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REQUEST_RAID_INFO                       = 0x0A87, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MOVE_TIME_SKIPPED                       = 0x0150, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MOVE_FEATHER_FALL_ACK                   = 0x08D0, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MOVE_WATER_WALK_ACK                     = 0x10F2, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PLAY_SOUND                              = 0x102A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_BATTLEFIELD_STATUS                      = 0x1F9E, // 5.4.8 18414 (Wow.exe binary)
    SMSG_BATTLEFIELD_STATUS                      = 0x0433,    // 5.4.8 18414 (Wow.exe NONE/removal semantics; token reference-sourced)
    SMSG_BATTLEFIELD_STATUS_ACTIVE               = 0x1AAF,    // 5.4.8 18414 (Wow.exe state 4 "active"; token reference-sourced)
    SMSG_BATTLEFIELD_STATUS_FAILED               = 0x1140,    // 5.4.8 18414 (Wow.exe failure reason/removal; token reference-sourced)
    SMSG_BATTLEFIELD_STATUS_QUEUED               = 0x122E,    // 5.4.8 18414 (Wow.exe state 1 "queued"; token reference-sourced)
    SMSG_BATTLEFIELD_STATUS_NEEDCONFIRMATION     = 0x1EAF,    // 5.4.8 18414 (Wow.exe state 2 "confirm"; token reference-sourced)
    SMSG_BATTLEFIELD_STATUS_ERROR                = 0x10A6,    // 5.4.8 18414 (Wow.exe state 3 "error"; exact historical token unresolved)
    CMSG_BATTLEFIELD_PORT                        = 0x1379, // 5.4.8 18414 (Wow.exe binary)
    CMSG_INSPECT_HONOR_STATS                     = 0x19C3, // 5.4.8 18414 (Wow.exe binary)
    SMSG_INSPECT_HONOR_STATS                     = 0x1A1E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_BATTLEMASTER_HELLO                      = 0x0234, // not in 5.4.8 (legacy; handler retained)
    SMSG_FORCE_WALK_SPEED_CHANGE                 = 0x12DB,    // (legacy; no client leaf)
    CMSG_FORCE_WALK_SPEED_CHANGE_ACK             = 0x00DB, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_WALK_SPEED_CHANGE_ACK)
    SMSG_FORCE_SWIM_BACK_SPEED_CHANGE            = 0x12DD,    // (legacy; no client leaf)
    CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK        = 0x10D1, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_SWIM_BACK_SPEED_CHANGE_ACK)
    SMSG_FORCE_TURN_RATE_CHANGE                  = 0x12DF,    // (legacy; no client leaf)
    CMSG_FORCE_TURN_RATE_CHANGE_ACK              = 0x185A, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_TURN_RATE_CHANGE_ACK)
    CMSG_PVP_LOG_DATA                            = 0x14C2, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PVP_LOG_DATA                            = 0x1E8F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_LEAVE_BATTLEFIELD                       = 0x0257, // 5.4.8 18414 (Wow.exe binary, via CMSG_BATTLEFIELD_LEAVE)
    CMSG_AREA_SPIRIT_HEALER_QUERY                = 0x03F1, // 5.4.8 18414 (Wow.exe binary)
    CMSG_AREA_SPIRIT_HEALER_QUEUE                = 0x12D8, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AREA_SPIRIT_HEALER_TIME                 = 0x188E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_WARDEN_DATA                             = 0x0C0A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_WARDEN_DATA                             = 0x1816, // 5.4.8 18414 (Wow.exe binary)
    CMSG_BATTLEGROUND_PLAYER_POSITIONS           = 0x3902, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_BATTLEGROUND_PLAYER_POSITIONS           = 0x060A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_PET_STOP_ATTACK                         = 0x065B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_BINDER_CONFIRM                          = 0x1287,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_BATTLEGROUND_PLAYER_JOINED              = 0x1E2F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_BATTLEGROUND_PLAYER_LEFT                = 0x0206,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_BATTLEMASTER_JOIN                       = 0x0769, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ADDON_INFO                              = 0x160A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PARTY_MEMBER_STATS_FULL                 = 0x0215,    // 4.3.4 15595 (no client leaf)
    CMSG_PET_SPELL_AUTOCAST                      = 0x06F0, // 5.4.8 18414 (Wow.exe binary) [was 0x12E9 alias of PET_SET_ACTION]
    SMSG_WEATHER                                 = 0x06AB,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PLAY_TIME_WARNING                       = 0x062A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_MINIGAME_SETUP                          = 0x12F7,    // (value unverified; no client leaf)
    SMSG_MINIGAME_STATE                          = 0x12F8,    // (value unverified; no client leaf)
    CMSG_MINIGAME_MOVE                           = 0x12F9, // NYI in 5.4.8 refs (value unverified)
    SMSG_MINIGAME_MOVE_FAILED                    = 0x12FA,    // (legacy; no client leaf)
    SMSG_RAID_INSTANCE_MESSAGE                   = 0x0CAF,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_COMPRESSED_MOVES                        = 0x0517,    // 4.3.4 15595 (unverified; no client leaf)
    CMSG_GUILD_INFO_TEXT                         = 0x0C70, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CHAT_RESTRICTED                         = 0x1A3B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_SET_RUN_SPEED                    = 0x12FF,    // (no client leaf)
    SMSG_SPLINE_SET_RUN_BACK_SPEED               = 0x1300,    // (no client leaf)
    SMSG_SPLINE_SET_SWIM_SPEED                   = 0x1301,    // (no client leaf)
    SMSG_SPLINE_SET_WALK_SPEED                   = 0x1302,    // (no client leaf)
    SMSG_SPLINE_SET_SWIM_BACK_SPEED              = 0x1303,    // (no client leaf)
    SMSG_SPLINE_SET_TURN_RATE                    = 0x1304,    // (no client leaf)
    SMSG_SPLINE_MOVE_UNROOT                      = 0x01E1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_FEATHER_FALL                = 0x3DA5,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    SMSG_SPLINE_MOVE_NORMAL_FALL                 = 0x38B2,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    SMSG_SPLINE_MOVE_SET_HOVER                   = 0x0258,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_UNSET_HOVER                 = 0x0CE1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_WATER_WALK                  = 0x50A2,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    SMSG_SPLINE_MOVE_LAND_WALK                   = 0x3DA7,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    SMSG_SPLINE_MOVE_START_SWIM                  = 0x0F29,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_STOP_SWIM                   = 0x1798,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_RUN_MODE                = 0x0B18,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_WALK_MODE               = 0x1865,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_GM_DESTROY_CORPSE                        = 0x1311,    // (no client leaf)
    CMSG_ACTIVATETAXIEXPRESS                     = 0x06FB, // 5.4.8 18414 (Wow.exe binary, via CMSG_ACTIVATE_TAXI_EXPRESS)
    SMSG_SET_FACTION_ATWAR                       = 0x0C9B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SET_FACTION_NOT_VISIBLE                 = 0x0000,    // (no client leaf)
    SMSG_GAMETIMEBIAS_SET                        = 0x1315,    // (legacy; no client leaf)
    CMSG_SET_FACTION_INACTIVE                    = 0x0778, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SET_WATCHED_FACTION                     = 0x06C9, // 5.4.8 18414 (Wow.exe binary)
    MSG_MOVE_TIME_SKIPPED                        = 0x7A0A,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    SMSG_SPLINE_MOVE_ROOT                        = 0x0728,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_INVALIDATE_PLAYER                       = 0x102E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_RESET_INSTANCES                         = 0x0C69, // 5.4.8 18414 (Wow.exe binary)
    SMSG_INSTANCE_RESET                          = 0x160F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_INSTANCE_RESET_FAILED                   = 0x0026,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_UPDATE_LAST_INSTANCE                    = 0x189B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_RAID_TARGET_UPDATE                      = 0x0886, // 5.4.8 18414 (Wow.exe binary serializer)
    MSG_RAID_READY_CHECK                         = 0x0DDC,    // 5.3.0 17128 (no client leaf)
    CMSG_DO_READY_CHECK                          = 0x0817, // 5.4.8 18414 (Wow-64.exe binary serializer)
    SMSG_PET_ACTION_SOUND                        = 0x15E2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PET_DISMISS_SOUND                       = 0x1ABB,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_GHOSTEE_GONE                            = 0x1327,    // (legacy; no client leaf)
    CMSG_GM_UPDATE_TICKET_STATUS                 = 0x15A8, // 5.4.8 18414 (Wow.exe binary, via CMSG_GM_TICKET_CASE_STATUS)
    SMSG_GM_TICKET_CASE_STATUS                   = 0x148E, // 5.4.8 18414 (Wow.exe binary reader; name reference-consensus)
    SMSG_GM_TICKET_STATUS_UPDATE                 = 0x000B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_GMSURVEY_SUBMIT                         = 0x073C, // 5.4.8 18414 (Wow.exe binary, via CMSG_GM_SURVEY_SUBMIT)
    SMSG_UPDATE_INSTANCE_OWNERSHIP               = 0x10E0,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CHAT_PLAYER_AMBIGUOUS                   = 0x061A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_DELAY_GHOST_TELEPORT                     = 0x132F,    // (no client leaf)
    SMSG_SPELLINSTAKILLLOG                       = 0x09F8,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPELL_UPDATE_CHAIN_TARGETS              = 0x0D52,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_CHAT_FILTERED                           = 0x0946, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_EXPECTED_SPAM_RECORDS                   = 0x18C0,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_LOTTERY_QUERY_RESULT_OBSOLETE           = 0x1336,    // (legacy; no client leaf)
    SMSG_LOTTERY_RESULT_OBSOLETE                 = 0x1338,    // (legacy; no client leaf)
    SMSG_CHARACTER_PROFILE                       = 0x1339,    // (legacy; no client leaf)
    SMSG_CHARACTER_PROFILE_REALM_CONNECTED       = 0x133A,    // (legacy; no client leaf)
    SMSG_DEFENSE_MESSAGE                         = 0x0A1F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_WORLD_SERVER_INFO                       = 0x0082,    // 5.4.8 18414 (Wow.exe binary; reader sub_6F470B)
    MSG_GM_RESETINSTANCELIMIT                    = 0x133D,    // (no client leaf)
    SMSG_MOTD                                    = 0x183B,    // 5.4.8 18414 (Wow.exe binary; reader sub_75B75A, handler sub_935E53)
    SMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY = 0x133F, // (no client leaf)
    SMSG_MOVE_UNSET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY = 0x0868, // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY_ACK = 0x11DB, // 5.4.8 18414 (Wow.exe binary)
    MSG_MOVE_START_SWIM_CHEAT                    = 0x1342,    // (no client leaf)
    MSG_MOVE_STOP_SWIM_CHEAT                     = 0x1343,    // (no client leaf)
    SMSG_MOVE_SET_CAN_FLY                        = 0x178D,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_MOVE_UNSET_CAN_FLY                      = 0x0162,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_MOVE_SET_CAN_FLY_ACK                    = 0x1052, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MOVE_SET_FLY                            = 0x01F1, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SOCKET_GEMS                             = 0x02CB, // 5.4.8 18414 (Wow.exe binary)
    MSG_MOVE_UPDATE_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY = 0x134B, // (no client leaf)
    CMSG_BATTLEMASTER_JOIN_ARENA                 = 0x02D2, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MOVE_START_ASCEND                       = 0x11FA, // 5.4.8 18414
    CMSG_MOVE_STOP_ASCEND                        = 0x115A, // 5.4.8 18414
    CMSG_LFG_JOIN                                = 0x046B, // 5.4.8 18414 (Wow.exe binary)
    CMSG_LFG_LEAVE                               = 0x01E0, // 5.4.8 18414 (Wow.exe binary)
    // Values and four-byte layouts are direct Wow.exe binary evidence; the
    // semantic LFR names come from the shipped UI/native call chain.
    CMSG_LFG_LFR_JOIN                           = 0x1AA2,
    CMSG_LFG_LFR_LEAVE                          = 0x00E3,
    SMSG_LFG_PROPOSAL_UPDATE                     = 0x1E3B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_LFG_PROPOSAL_RESPONSE                   = 0x1D9D, // 5.4.8 18414 (Wow.exe binary, via CMSG_LFG_PROPOSAL_RESULT)
    SMSG_LFG_ROLE_CHECK_UPDATE                   = 0x12BB,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_LFG_JOIN_RESULT                         = 0x18E3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LFG_QUEUE_STATUS                        = 0x1006,    // 5.4.8 18414 (Wow.exe binary; handler retains LFG_QUEUE_STATUS literal)
    CMSG_SET_LFG_COMMENT                         = 0x0AB2,    // 5.4.8 18414 (Wow.exe binary: sub_661542 writes 2738; 0x0530 was the 4.3.4 value and is never sent)
    SMSG_LFG_UPDATE_SEARCH                       = 0x1161,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_LFG_SET_ROLES                           = 0x08A2, // 5.4.8 18414 (Wow.exe binary)
    CMSG_LFG_BOOT_PLAYER_VOTE                    = 0x17BE, // 5.4.8 18414 (Wow.exe binary, via CMSG_LFG_SET_BOOT_VOTE)
    SMSG_LFG_BOOT_PLAYER                         = 0x183A,    // 5.4.8 18414 (Wow.exe decoded leaf; handler retains LFG_BOOT_PLAYER literal)
    CMSG_LFG_LOCK_INFO_REQUEST                   = 0x006B, // 5.4.8 18414 (Wow.exe binary; 0x7F byte plus player/party bit)
    SMSG_LFG_PLAYER_INFO                         = 0x1861,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_LFG_TELEPORT                            = 0x1AA6, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LFG_PARTY_INFO                          = 0x168E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_TITLE_EARNED                            = 0x068E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_TITLE                               = 0x03C7, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CANCEL_MOUNT_AURA                       = 0x1552, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ARENA_ERROR                             = 0x04BA,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_DEATH_RELEASE_LOC                       = 0x1063,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CANCEL_TEMP_ENCHANTMENT                 = 0x024B, // 5.4.8 18414 (Wow.exe binary)
    SMSG_FORCED_DEATH_UPDATE                     = 0x0E8F,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    MSG_MOVE_SET_FLIGHT_SPEED_CHEAT              = 0x137E,    // (no client leaf)
    SMSG_MOVE_SET_FLIGHT_SPEED                   = 0x006E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    MSG_MOVE_SET_FLIGHT_BACK_SPEED_CHEAT         = 0x1380,    // (no client leaf)
    SMSG_MOVE_SET_FLIGHT_BACK_SPEED              = 0x0319,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_FORCE_FLIGHT_SPEED_CHANGE               = 0x1382,    // (legacy; no client leaf)
    CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK           = 0x09DA, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_FLIGHT_SPEED_CHANGE_ACK)
    SMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE          = 0x1384,    // (legacy; no client leaf)
    CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK      = 0x105B, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK)
    SMSG_SPLINE_SET_FLIGHT_SPEED                 = 0x1386,    // (no client leaf)
    SMSG_SPLINE_SET_FLIGHT_BACK_SPEED            = 0x1387,    // (no client leaf)
    SMSG_FLIGHT_SPLINE_SYNC                      = 0x0063,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_TAXI_BENCHMARK_MODE                 = 0x0762, // 5.4.8 18414 (Wow.exe binary)
    SMSG_JOINED_BATTLEGROUND_QUEUE               = 0x138B,    // (legacy; no client leaf)
    SMSG_REALM_SPLIT                             = 0x1A2E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REALM_SPLIT                             = 0x0062, // 5.4.8 18414 (Wow.exe binary)
    CMSG_MOVE_CHNG_TRANSPORT                     = 0x09DB, // 5.4.8 18414 (Wow.exe binary)
    MSG_PARTY_ASSIGNMENT                         = 0x0424,    // 4.3.4 15595 (no client leaf)
    CMSG_SET_PARTY_ASSIGNMENT                    = 0x1802,    // 5.4.8 18414 (Wow.exe binary: sub_902C83 -> sub_66105D writes 6146)
    CMSG_SET_EVERYONE_IS_ASSISTANT               = 0x01E1,    // 5.4.8 18414 (Wow.exe binary: sub_661450 writes 481; no corpus traffic)
    CMSG_GROUP_SET_ROLES                         = 0x1A92,    // 5.4.8 18414 (Wow.exe binary: sub_660FCB writes 6802)
    CMSG_GROUP_INITIATE_ROLE_POLL                = 0x1882,    // 5.4.8 18414 (Wow.exe binary: sub_660E1F writes 6274)
    SMSG_OFFER_PETITION_ERROR                    = 0x161E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_TIME_SYNC_REQ                           = 0x1A8F,    // 5.4.8 18414 (Wow.exe binary; reader sub_6D9F28, handler sub_7AED46)
    CMSG_TIME_SYNC_RESP                          = 0x01DB, // 5.4.8 18414 (Wow.exe writer sub_670801, body sub_6707E0)
    CMSG_TIME_SYNC_RESPONSE_FAILED               = 0x0058, // 5.4.8 18414 (Wow.exe binary)
    CMSG_TIME_SYNC_RESPONSE_DROPPED              = 0x10D3, // 5.4.8 18414 (Wow.exe binary; writer sub_66FEC8, body sub_6707E0)
    CMSG_DISCARDED_TIME_SYNC_ACKS                = 0x115B, // 5.4.8 18414 (Wow.exe binary; writer sub_670A64, body sub_692FA0)
    SMSG_RESET_FAILED_NOTIFY                     = 0x10AE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LFG_DISABLED                            = 0x008E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_CHEAT_DUMP_ITEMS_DEBUG_ONLY_RESPONSE    = 0x139C,    // (legacy; no client leaf)
    SMSG_CHEAT_DUMP_ITEMS_DEBUG_ONLY_RESPONSE_WRITE_FILE = 0x139D, // (legacy; no client leaf)
    SMSG_UPDATE_COMBO_POINTS                     = 0x082F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_VOICE_SESSION_ROSTER_UPDATE             = 0x000E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_VOICE_SESSION_LEAVE                     = 0x15C0,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_VOICE_SESSION_ADJUST_PRIORITY           = 0x13A1,    // (legacy; no client leaf)
    SMSG_VOICE_SET_TALKER_MUTED                  = 0x13A3,    // (value unverified; no client leaf)
    SMSG_INIT_EXTRA_AURA_INFO_OBSOLETE           = 0x13A4,    // (legacy; no client leaf)
    SMSG_SET_EXTRA_AURA_INFO_OBSOLETE            = 0x13A5,    // (legacy; no client leaf)
    SMSG_SET_EXTRA_AURA_INFO_NEED_UPDATE_OBSOLETE = 0x13A6,   // (no client leaf)
    SMSG_CLEAR_EXTRA_AURA_INFO_OBSOLETE          = 0x13A7,    // (legacy; no client leaf)
    CMSG_MOVE_START_DESCEND                      = 0x01D1, // 5.4.8 18414
    SMSG_IGNORE_REQUIREMENTS_CHEAT               = 0x13AA,    // (legacy; no client leaf)
    SMSG_SPELL_CHANCE_PROC_LOG                   = 0x13AB,    // (legacy; no client leaf)
    SMSG_DISMOUNT                                = 0x0E3A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_MOVE_UPDATE_CAN_FLY                     = 0x3DA1,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    MSG_RAID_READY_CHECK_CONFIRM                 = 0x03C9,    // 5.3.0 17128 (no client leaf)
    CMSG_RAID_READY_CHECK_CONFIRM                = 0x158B, // 5.4.8 18414 (Wow-64.exe binary serializer)
    CMSG_VOICE_SESSION_ENABLE                    = 0x15A9, // 5.4.8 18414 (Wow.exe binary)
    SMSG_VOICE_SESSION_ENABLE                    = 0x13B1,    // (legacy; no client leaf)
    SMSG_VOICE_PARENTAL_CONTROLS                 = 0x04BF,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    MSG_GM_GEARRATING                            = 0x13B5,    // (no client leaf)
    CMSG_COMMENTATOR_ENABLE                      = 0x0B07, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_COMMENTATOR_STATE_CHANGED               = 0x0737,    // 4.3.4 15595 (unverified; no client leaf)
    CMSG_COMMENTATOR_GET_MAP_INFO                = 0x0026, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_COMMENTATOR_MAP_INFO                    = 0x1CBE,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_COMMENTATOR_GET_PLAYER_INFO             = 0x0D14, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_COMMENTATOR_GET_PLAYER_INFO             = 0x13BB,    // (legacy; no client leaf)
    SMSG_COMMENTATOR_PLAYER_INFO                 = 0x2F36,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    CMSG_COMMENTATOR_ENTER_INSTANCE              = 0x4105, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    CMSG_COMMENTATOR_EXIT_INSTANCE               = 0x6136, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    CMSG_COMMENTATOR_INSTANCE_COMMAND            = 0x0917, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_CLEAR_TARGET                            = 0x1061,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CROSSED_INEBRIATION_THRESHOLD           = 0x1E9E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CHEAT_PLAYER_LOGIN                      = 0x13C3, // not in 5.4.8 (legacy)
    SMSG_CHEAT_PLAYER_LOOKUP                     = 0x13C5,    // (legacy; no client leaf)
    SMSG_KICK_REASON                             = 0x0A1E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    MSG_RAID_READY_CHECK_FINISHED                = 0x1591,    // 5.3.0 17128 (no client leaf)
    CMSG_COMPLAIN                                = 0x0319, // 5.4.8 18414 (Wow.exe binary)
    SMSG_COMPLAIN_RESULT                         = 0x128F,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_FEATURE_SYSTEM_STATUS                   = 0x16BB,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CHANNEL_SILENCE_VOICE                   = 0x2D54, // not in 5.4.8 (legacy)
    CMSG_CHANNEL_SILENCE_ALL                     = 0x2154, // not in 5.4.8 (legacy)
    CMSG_CHANNEL_UNSILENCE_VOICE                 = 0x3146, // not in 5.4.8 (legacy)
    CMSG_CHANNEL_UNSILENCE_ALL                   = 0x2546, // not in 5.4.8 (legacy)
    CMSG_CHANNEL_DISPLAY_LIST                    = 0x2144, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    CMSG_SET_ACTIVE_VOICE_CHANNEL                = 0x4305, // not in 5.4.8 (legacy)
    CMSG_CHANNEL_VOICE_ON                        = 0x1144, // not in 5.4.8 (legacy)
    CMSG_CHANNEL_VOICE_OFF                       = 0x13D8, // not in 5.4.8 (legacy)
    SMSG_DEBUG_LIST_TARGETS                      = 0x13DA,    // (legacy; no client leaf)
    SMSG_AVAILABLE_VOICE_CHANNEL                 = 0x029A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_ADD_VOICE_IGNORE                        = 0x13DC, // not in 5.4.8 (legacy)
    CMSG_DEL_VOICE_IGNORE                        = 0x13DD, // not in 5.4.8 (legacy)
    CMSG_PARTY_SILENCE                           = 0x6B26, // not in 5.4.8 (legacy)
    CMSG_PARTY_UNSILENCE                         = 0x4D24, // not in 5.4.8 (legacy)
    MSG_NOTIFY_PARTY_SQUELCH                     = 0x4D06,    // 4.3.4 15595 (no client leaf; unframable >0x1FFF)
    SMSG_COMSAT_RECONNECT_TRY                    = 0x4D35,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_COMSAT_DISCONNECT                       = 0x0316,    // 4.3.4 15595 (unverified; no client leaf)
    SMSG_COMSAT_CONNECT_FAIL                     = 0x6317,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_VOICE_CHAT_STATUS                       = 0x10E2,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_REPORT_PVP_AFK                          = 0x06F9, // 5.4.8 18414 (Wow.exe binary)
    SMSG_REPORT_PVP_AFK_RESULT                   = 0x18BE,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_GUILD_BANKER_ACTIVATE                   = 0x0372, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_BANK_QUERY_TAB                    = 0x1372, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_BANK_LIST                         = 0x0B79,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GUILD_BANK_SWAP_ITEMS                   = 0x136A, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_BANK_BUY_TAB                      = 0x0251, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_BANK_UPDATE_TAB                   = 0x07C2, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_BANK_DEPOSIT_MONEY                = 0x0770, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_BANK_WITHDRAW_MONEY               = 0x07EA, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_BANK_LOG_QUERY                    = 0x0CD3, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_BANK_LOG_QUERY_RESULT             = 0x0FF0,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SET_CHANNEL_WATCH                       = 0x4517, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_USERLIST_ADD                            = 0x1462,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_USERLIST_REMOVE                         = 0x0AAB,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_USERLIST_UPDATE                         = 0x063A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CLEAR_CHANNEL_WATCH                     = 0x2604, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_INSPECT_RESULTS                         = 0x1842,    // 5.4.8 18414 (Wow.exe leaf; was 0x4014 in 4.3.4, unframable)
    SMSG_GOGOGO_OBSOLETE                         = 0x13F6,    // (legacy; no client leaf)
    SMSG_ECHO_PARTY_SQUELCH                      = 0x0814,    // 4.3.4 15595 (unverified; no client leaf)
    CMSG_SET_TITLE_SUFFIX                        = 0x13F8, // not in 5.4.8 (legacy)
    CMSG_SPELLCLICK                              = 0x067A, // 5.4.8 18414 (Wow.exe binary)
    SMSG_LOOT_LIST                               = 0x1C3F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_VOICESESSION_FULL                       = 0x6225,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    CMSG_GUILD_PERMISSIONS                       = 0x145A, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_PERMISSIONS                       = 0x0FF9,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_GUILD_PERMISSIONS_QUERY_RESULTS)
    CMSG_GUILD_BANK_MONEY_WITHDRAWN              = 0x14DB, // 5.4.8 18414 (Wow.exe binary, via CMSG_GUILD_BANK_MONEY_WITHDRAWN_QUERY)
    SMSG_GUILD_BANK_MONEY_WITHDRAWN              = 0x0B78,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GUILD_EVENT_LOG_QUERY                   = 0x15D9, // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_EVENT_LOG                         = 0x1AF1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_GUILD_EVENT_LOG_QUERY_RESULT)
    CMSG_GET_MIRRORIMAGE_DATA                    = 0x02A3, // 5.4.8 18414 (Wow.exe binary, via CMSG_GET_MIRROR_IMAGE_DATA)
    SMSG_FORCE_DISPLAY_UPDATE                    = 0x1404,    // (legacy; no client leaf)
    SMSG_SPELL_CHANCE_RESIST_PUSHBACK            = 0x1405,    // (legacy; no client leaf)
    SMSG_IGNORE_DIMINISHING_RETURNS_CHEAT        = 0x0125,    // (legacy; no client leaf)
    CMSG_KEEP_ALIVE                              = 0x1A87, // 5.4.8 18414 (Wow.exe binary)
    SMSG_RAID_READY_CHECK_ERROR                  = 0x1409,    // (no client leaf)
    CMSG_OPT_OUT_OF_LOOT                         = 0x06E0, // 5.4.8 18414 (Wow.exe binary)
    CMSG_QUERY_GUILD_BANK_TEXT                   = 0x0550, // 5.4.8 18414 (Wow.exe binary, via CMSG_GUILD_BANK_QUERY_TEXT)
    SMSG_GUILD_BANK_TEXT                         = 0x1AE0,    // 5.4.8 18414 (Wow.exe leaf; was 0x75A3 in 4.3.4, unframable)
    CMSG_SET_GUILD_BANK_TEXT                     = 0x3023, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    CMSG_GRANT_LEVEL                             = 0x0662, // 5.4.8 18414 (Wow.exe binary)
    MSG_GM_CHANGE_ARENA_RATING                   = 0x1410,    // (no client leaf)
    CMSG_DECLINE_CHANNEL_INVITE                  = 0x1411, // not in 5.4.8 (legacy)
    SMSG_GROUPACTION_THROTTLED                   = 0x0287,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_OVERRIDE_LIGHT                          = 0x068A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_TOTEM_CREATED                           = 0x1C8F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_TOTEM_DESTROYED                         = 0x1263, // 5.4.8 18414 (Wow.exe binary)
    CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY        = 0x02F1, // 5.4.8 18414 (null writer 0x690B78)
    SMSG_QUESTGIVER_STATUS_MULTIPLE              = 0x06CE,    // 5.4.8 18414 (reader 0x6B3D6A; quest-marker consumer 0x7ADD53)
    CMSG_SET_PLAYER_DECLINED_NAMES               = 0x09E2, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SET_PLAYER_DECLINED_NAMES_RESULT        = 0x180E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SERVER_BUCK_DATA                        = 0x1142,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_SEND_UNLEARN_SPELLS                     = 0x10F1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PROPOSE_LEVEL_GRANT                     = 0x109A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_ACCEPT_LEVEL_GRANT                      = 0x02FB, // 5.4.8 18414 (Wow.exe binary)
    SMSG_REFER_A_FRIEND_FAILURE                  = 0x021E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_FLYING                  = 0x1046,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_UNSET_FLYING                = 0x0DE2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SUMMON_CANCEL                           = 0x000A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_ALTER_APPEARANCE                        = 0x07F0, // 5.4.8 18414 (Wow.exe writer sub_6862FA; body sub_6862C2)
    SMSG_ENABLE_BARBER_SHOP                      = 0x1222,    // 5.4.8 18414 (Wow.exe binary: BARBER_SHOP_OPEN event)
    SMSG_BARBER_SHOP_RESULT                      = 0x0C3F,    // 5.4.8 18414 (Wow.exe reader sub_6D9F28; BARBER_SHOP_SUCCESS callback)
    CMSG_CALENDAR_GET_CALENDAR                   = 0x1F9F, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_GET_EVENT                      = 0x030C, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_GUILD_FILTER                   = 0x04E3, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_ADD_EVENT                      = 0x0A37, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_UPDATE_EVENT                   = 0x1F8D, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_REMOVE_EVENT                   = 0x0C61, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_COPY_EVENT                     = 0x1A97, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_EVENT_INVITE                   = 0x1D8E, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_EVENT_RSVP                     = 0x1FB8, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_EVENT_REMOVE_INVITE            = 0x0962, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_EVENT_STATUS                   = 0x1AB3, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_EVENT_MODERATOR_STATUS         = 0x0708, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CALENDAR_SEND_CALENDAR                  = 0x1A0A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_SEND_EVENT                     = 0x12AE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_EVENT_INVITE                   = 0x15C3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_EVENT_INVITE_REMOVED           = 0x00A2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_COMMAND_RESULT                 = 0x142A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_RAID_LOCKOUT_ADDED             = 0x0CAB,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_RAID_LOCKOUT_REMOVED           = 0x11E0,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_EVENT_INVITE_ALERT             = 0x0A9F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT     = 0x122B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT      = 0x0412,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_EVENT_REMOVED_ALERT            = 0x049B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_EVENT_UPDATED_ALERT            = 0x0A0E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CALENDAR_COMPLAIN                       = 0x1F8F, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CALENDAR_GET_NUM_PENDING                = 0x0813, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CALENDAR_SEND_NUM_PENDING               = 0x0A3F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_NOTIFY_DANCE                            = 0x4904,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_PLAY_DANCE                              = 0x4704,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    CMSG_STOP_DANCE                              = 0x2907, // not in 5.4.8 (legacy)
    SMSG_STOP_DANCE                              = 0x4637,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    CMSG_SYNC_DANCE                              = 0x0036, // not in 5.4.8 (legacy)
    CMSG_DANCE_QUERY                             = 0x4E07, // not in 5.4.8 (legacy)
    SMSG_DANCE_QUERY_RESPONSE                    = 0x2F06,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_INVALIDATE_DANCE                        = 0x0E27,    // 4.3.4 15595 (unverified; no client leaf)
    SMSG_INSPECT_RATED_BG_STATS                  = 0x041F,    // 5.4.8 18414 (Wow.exe leaf; name via 5.4.7 bridge; reader sub_7161E3)
    MSG_MOVE_SET_PITCH_RATE_CHEAT                = 0x145B,    // (no client leaf)
    SMSG_MOVE_SET_PITCH_RATE                     = 0x17AB,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_FORCE_PITCH_RATE_CHANGE                 = 0x145D,    // (legacy; no client leaf)
    CMSG_FORCE_PITCH_RATE_CHANGE_ACK             = 0x0172, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_FORCE_PITCH_RATE_CHANGE_ACK)
    SMSG_SPLINE_SET_PITCH_RATE                   = 0x145F,    // (no client leaf)
    CMSG_CALENDAR_EVENT_INVITE_NOTES             = 0x1460, // not in 5.4.8 (legacy)
    SMSG_CALENDAR_EVENT_INVITE_NOTES             = 0x11C0,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT       = 0x1286,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_UPDATE_MISSILE_TRAJECTORY               = 0x781E, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_TRIGGER_MOVIE                           = 0x1C2E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_COMPLETE_MOVIE                          = 0x1362, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ACHIEVEMENT_EARNED                      = 0x080B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_DYNAMIC_DROP_ROLL_RESULT                = 0x146A,    // (legacy; no client leaf)
    SMSG_CRITERIA_UPDATE                         = 0x0E9B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_QUERY_INSPECT_ACHIEVEMENTS              = 0x0373, // 5.4.8 18414 (Wow.exe binary)
    SMSG_RESPOND_INSPECT_ACHIEVEMENTS            = 0x009E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_DISMISS_CONTROLLED_VEHICLE              = 0x09FA, // 5.4.8 18414 (Wow.exe binary)
    SMSG_QUESTUPDATE_ADD_PVP_KILL                = 0x0256,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CALENDAR_RAID_LOCKOUT_UPDATED           = 0x0E1F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CHAR_CUSTOMIZE                          = 0x0A13, // 5.4.8 18414 (Wow.exe writer sub_6694E7)
    SMSG_CHAR_CUSTOMIZE                          = 0x1432, // 5.4.8 18414 (Wow.exe reader sub_6FB2D3)
    SMSG_PET_RENAMEABLE                          = 0x2B27,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    CMSG_REQUEST_VEHICLE_EXIT                    = 0x1DC3, // 5.4.8 18414 (Wow.exe binary)
    CMSG_REQUEST_VEHICLE_PREV_SEAT               = 0x03C4, // 5.4.8 18414 (Wow.exe binary)
    CMSG_REQUEST_VEHICLE_NEXT_SEAT               = 0x0141, // 5.4.8 18414 (Wow.exe binary)
    CMSG_REQUEST_VEHICLE_SWITCH_SEAT             = 0x1143, // 5.4.8 18414 (Wow.exe binary)
    CMSG_PET_LEARN_TALENT                        = 0x6725, // not in 5.4.8 (legacy)
    SMSG_SET_PHASE_SHIFT                         = 0x02A2,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_ALL_ACHIEVEMENT_DATA                    = 0x180A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_HEALTH_UPDATE                           = 0x148B,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_POWER_UPDATE                            = 0x109F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GAMEOBJ_REPORT_USE                      = 0x06D9, // 5.4.8 18414 (Wow.exe binary)
    SMSG_HIGHEST_THREAT_UPDATE                   = 0x14AE,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_THREAT_UPDATE                           = 0x0632,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_THREAT_REMOVE                           = 0x1960,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_THREAT_CLEAR                            = 0x180B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CONVERT_RUNE                            = 0x1A1B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_RESYNC_RUNES                            = 0x15E3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_ADD_RUNE_POWER                          = 0x1860,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REMOVE_GLYPH                            = 0x148B, // not in 5.4.8 (legacy; handler retained)
    SMSG_DUMP_OBJECTS_DATA                       = 0x148D,    // (legacy; no client leaf)
    CMSG_DISMISS_CRITTER                         = 0x12DB, // 5.4.8 18414 (Wow.exe binary)
    SMSG_NOTIFY_DEST_LOC_SPELL_CAST              = 0x1E0E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_AUCTION_LIST_PENDING_SALES              = 0x02DA, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AUCTION_LIST_PENDING_SALES              = 0x0B81,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_MODIFY_COOLDOWN                         = 0x1E2E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PET_UPDATE_COMBO_POINTS                 = 0x1206,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_ENABLETAXI                              = 0x0741, // 5.4.8 18414 (Wow.exe binary, via CMSG_ENABLE_TAXI)
    SMSG_PRE_RESURRECT                           = 0x19C0,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_AURA_UPDATE                             = 0x0072,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SERVER_FIRST_ACHIEVEMENT                = 0x028B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_PET_LEARNED_SPELL                       = 0x0282, // 5.4.8 18414 (reader sub_6B0AD9; handler sub_7B8F82; name reference-consensus)
    SMSG_PET_REMOVED_SPELL                       = 0x1CAE, // 5.4.8 18414 (reader sub_6B0AD9; handler sub_7B906D; name reference-consensus)
    CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE      = 0x08F8, // 5.4.8 18414 (Wow.exe binary)
    // Corrected from 0x0360 (which is CMSG_SELF_RES) on binary evidence: Lua
    // HearthAndResurrectFromArea -> sub_91064C -> class sub_6868A5 -> vtable
    // off_D64914 slot 2 = sub_6865BD, which writes 835 = 0x0343.
    // NOTE: Opcodes_reference.h, the inert clean-room enumeration, lists 0x0343
    // as CMSG_BATTLEFIELD_MGR_EXIT_REQUEST. That row is stale and the conflict
    // predates this correction: the compiled header already gives that opcode
    // 0x08B3 with its own binary provenance, so the two disagreed beforehand.
    // The reference header is not #included and cannot affect the build.
    CMSG_HEARTH_AND_RESURRECT                    = 0x0343, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ON_CANCEL_EXPECTED_RIDE_VEHICLE_AURA    = 0x1A2A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_CRITERIA_DELETED                        = 0x1C33,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_ACHIEVEMENT_DELETED                     = 0x1A2F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SERVER_INFO_RESPONSE                    = 0x103A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_SERVER_BUCK_DATA_START                  = 0x14A4,    // (no client leaf)
    SMSG_BATTLEGROUND_INFO_THROTTLED             = 0x1E1E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_SET_VEHICLE_REC_ID                      = 0x149F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_RIDE_VEHICLE_INTERACT                   = 0x0277, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CONTROLLER_EJECT_PASSENGER              = 0x06E7, // 5.4.8 18414 (Wow.exe binary, via CMSG_EJECT_PASSENGER)
    SMSG_PET_GUIDS                               = 0x1227,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_CLIENTCACHE_VERSION                     = 0x002A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SET_ITEM_PURCHASE_DATA                  = 0x1C9A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GET_ITEM_PURCHASE_DATA                  = 0x1258, // 5.4.8 18414 (Wow.exe binary)
    CMSG_ITEM_PURCHASE_REFUND                    = 0x074B, // 5.4.8 18414 (Wow.exe binary, via CMSG_ITEM_REFUND)
    SMSG_ITEM_PURCHASE_REFUND_RESULT             = 0x049E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CORPSE_QUERY                            = 0x1FBE, // 5.4.8 18414 (Wow.exe empty writer and corpse semantic path)
    SMSG_CORPSE_QUERY_RESPONSE                   = 0x0E0B,    // 5.4.8 18414 (Wow.exe corpse reader and semantic path)
    CMSG_CALENDAR_EVENT_SIGNUP                   = 0x01E3, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CALENDAR_CLEAR_PENDING_ACTION           = 0x1E3A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_LOAD_EQUIPMENT_SET                      = 0x18E2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_SAVE_EQUIPMENT_SET                      = 0x0669, // 5.4.8 18414 (Wow.exe binary, via CMSG_EQUIPMENT_SET_SAVE)
    CMSG_ON_MISSILE_TRAJECTORY_COLLISION         = 0x06D6, // 5.4.8 18414 (Wow.exe binary, via CMSG_MISSILE_TRAJECTORY_COLLISION)
    SMSG_NOTIFY_MISSILE_TRAJECTORY_COLLISION     = 0x120A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_TALENT_UPDATE                           = 0x0A9B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_UPDATE_TALENT_DATA)
    CMSG_LEARN_TALENT_GROUP                      = 0x2415, // not in 5.4.8 (legacy)
    CMSG_PET_LEARN_TALENT_GROUP                  = 0x6E24, // not in 5.4.8 (legacy)
    SMSG_DESTROY_ARENA_UNIT                      = 0x2637,    // (legacy; no client leaf; unframable >0x1FFF)
    SMSG_PROFILEDATA_RESPONSE                    = 0x14CB,    // (legacy; no client leaf)
    SMSG_COMPOUND_MOVE                           = 0x0061,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_MOVE_SET_COMPOUND_STATE)
    SMSG_MOVE_GRAVITY_DISABLE                    = 0x159F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_MOVE_GRAVITY_DISABLE_ACK                = 0x09D3, // 5.4.8 18414 (Wow.exe binary)
    SMSG_MOVE_GRAVITY_ENABLE                     = 0x0A27,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_MOVE_GRAVITY_ENABLE_ACK                 = 0x11D8, // 5.4.8 18414 (Wow.exe binary)
    MSG_MOVE_GRAVITY_CHNG                        = 0x14D3,    // (no client leaf)
    SMSG_SPLINE_MOVE_GRAVITY_DISABLE             = 0x0845,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_GRAVITY_ENABLE              = 0x0865,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_USE_EQUIPMENT_SET                       = 0x036E, // 5.4.8 18414 (Wow.exe binary, via CMSG_EQUIPMENT_SET_USE)
    SMSG_USE_EQUIPMENT_SET_RESULT                = 0x0A2B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_FORCE_ANIM                              = 0x4C05,    // (legacy; no client leaf; unframable >0x1FFF)
    CMSG_CHAR_FACTION_CHANGE                     = 0x0329, // 5.4.8 18414 (Wow.exe binary)
    SMSG_CHAR_FACTION_CHANGE                     = 0x4C06,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_PVP_QUEUE_STATS                         = 0x14DD,    // (legacy; no client leaf)
    SMSG_BATTLEFIELD_MANAGER_ENTRY_INVITE        = 0x1226,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_BATTLEFIELD_MGR_ENTRY_INVITE)
    CMSG_BATTLEFIELD_MANAGER_ENTRY_INVITE_RESPONSE = 0x1806, // 5.4.8 18414 (Wow.exe binary, via CMSG_BATTLEFIELD_MGR_ENTRY_INVITE_RESPONSE)
    SMSG_BATTLEFIELD_MANAGER_ENTERING            = 0x14E1,    // 5.4.8 18414 (Wow.exe leaf; name legacy)
    SMSG_BATTLEFIELD_MANAGER_QUEUE_INVITE        = 0x142E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_BATTLEFIELD_MGR_QUEUE_INVITE)
    CMSG_BATTLEFIELD_MANAGER_QUEUE_INVITE_RESPONSE = 0x0A97, // 5.4.8 18414 (Wow.exe binary, via CMSG_BATTLEFIELD_MGR_QUEUE_INVITE_RESPONSE)
    CMSG_BATTLEFIELD_MANAGER_QUEUE_REQUEST       = 0x710C, // not in 5.4.8 (legacy)
    SMSG_BATTLEFIELD_MANAGER_QUEUE_REQUEST_RESPONSE = 0x08BE, // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE)
    SMSG_BATTLEFIELD_MANAGER_EJECT_PENDING       = 0x003E,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork; fuzzy via SMSG_BATTLEFIELD_MGR_EJECT_PENDING)
    SMSG_BATTLEFIELD_MANAGER_EJECTED             = 0x14E7,    // (legacy; no client leaf)
    CMSG_BATTLEFIELD_MANAGER_EXIT_REQUEST        = 0x08B3, // 5.4.8 18414 (Wow.exe binary, via CMSG_BATTLEFIELD_MGR_EXIT_REQUEST)
    SMSG_BATTLEFIELD_MANAGER_STATE_CHANGED       = 0x14E9,    // (legacy; no client leaf)
    // MSG_SET_RAID_DIFFICULTY 0x0614 REMOVED. It was a 4.3.4 carry-over tagged "no client
    // leaf", and it is refuted for 18414: no leaf in the binary, and zero occurrences across
    // 1079 captures in either direction. The real inbound opcodes are declared with the other
    // CMSG values below. Nothing referenced the enum -- only a stale DEBUG_LOG string.
    CMSG_SET_DUNGEON_DIFFICULTY                  = 0x1A36,    // 5.4.8 18414 (Wow.exe body writer; one uint32 raw client DifficultyID)
    CMSG_SET_RAID_DIFFICULTY                     = 0x0591,    // 5.4.8 18414 (Wow.exe body writer; one uint32 raw client DifficultyID; value is BIDIRECTIONAL, see SMSG_SET_RAID_DIFFICULTY)
    SMSG_XPGAIN                                  = 0x14EE,    // (legacy; no client leaf)
    SMSG_GMTICKET_RESPONSE_ERROR                 = 0x14EF,    // (legacy; no client leaf)
    SMSG_GMTICKET_GET_RESPONSE                   = 0x2E34,    // (legacy; no client leaf; unframable >0x1FFF)
    SMSG_GMTICKET_RESOLVE_RESPONSE               = 0x1ABE,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork; via SMSG_GM_TICKET_RESOLVE_RESPONSE)
    SMSG_GMTICKET_CREATE_RESPONSE_TICKET         = 0x14F3,    // (legacy; no client leaf)
    CMSG_GM_CREATE_TICKET_RESPONSE               = 0x14F4, // not in 5.4.8 (legacy)
    SMSG_SERVERINFO                              = 0x1091,    // (legacy; no client leaf)
    CMSG_UI_TIME_REQUEST                         = 0x15AB, // 5.4.8 18414 (Wow.exe binary)
    SMSG_UI_TIME                                 = 0x0027,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CHAR_RACE_CHANGE                        = 0x0D24, // not in 5.4.8 (legacy)
    MSG_VIEW_PHASE_SHIFT                         = 0x14FA,    // (no client leaf)
    SMSG_TALENTS_INVOLUNTARILY_RESET             = 0x088A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_DEBUG_SERVER_GEO                        = 0x14FD,    // (legacy; no client leaf)
    SMSG_LOOT_UPDATE                             = 0x1863,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork; fuzzy via SMSG_UPDATE_DUNGEON_ENCOUNTER_FOR_LOOT)
    CMSG_READY_FOR_ACCOUNT_DATA_TIMES            = 0x031C, // 5.4.8 18414 (Wow.exe binary)
    SMSG_AFK_MONITOR_INFO_RESPONSE               = 0x1505,    // (legacy; no client leaf)
    SMSG_AREA_TRIGGER_NO_CORPSE                  = 0x089E,    // 5.4.8 18414 (empty reader 0x6BC12D; error-463 handler 0xCCBF8A)
    SMSG_CAMERA_SHAKE                            = 0x0C3A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_SOCKET_GEMS                             = 0x12A6,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_SOCKET_GEMS_RESULT)
    SMSG_CONNECT_TO                              = 0x1149,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_CONNECT_TO_FAILED                       = 0x2533, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_SUSPEND_COMMS                           = 0x1D48,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_RESUME_COMMS                            = 0x0969,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_AUTH_CONTINUED_SESSION                  = 0x0F49, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SEND_ALL_COMBAT_LOG                     = 0x1515,    // (legacy; no client leaf)
    SMSG_OPEN_LFG_DUNGEON_FINDER                 = 0x0E8A,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    SMSG_MOVE_SET_COLLISION_HGT                  = 0x0250,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_MOVE_SET_COLLISION_HEIGHT)
    CMSG_MOVE_SET_COLLISION_HGT_ACK              = 0x09FB, // 5.4.8 18414 (Wow.exe binary, via CMSG_MOVE_SET_COLLISION_HEIGHT_ACK)
    MSG_MOVE_SET_COLLISION_HGT                   = 0x1519,    // (no client leaf)
    CMSG_COMMENTATOR_SKIRMISH_QUEUE_COMMAND      = 0x0025, // 4.3.4 15595 — NYI in 5.4.8 refs (unverified)
    SMSG_COMMENTATOR_SKIRMISH_QUEUE_RESULT1      = 0x2126,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_COMMENTATOR_SKIRMISH_QUEUE_RESULT2      = 0x6814,    // 4.3.4 15595 (unverified; no client leaf; unframable >0x1FFF)
    SMSG_COMPRESSED_UNKNOWN_1310                 = 0x151F,    // (legacy; no client leaf)
    SMSG_UNKNOWN_1311                            = 0x1520,    // (legacy; no client leaf)
    SMSG_UNKNOWN_1312                            = 0x1521,    // (legacy; no client leaf)
    SMSG_UNKNOWN_1314                            = 0x1523,    // (legacy; no client leaf)
    SMSG_UNKNOWN_1315                            = 0x1524,    // (legacy; no client leaf)
    SMSG_UNKNOWN_1316                            = 0x1525,    // (legacy; no client leaf)
    SMSG_UNKNOWN_1317                            = 0x1526,    // (legacy; no client leaf)
    SMSG_UNKNOWN_1329                            = 0x1532,    // (legacy; no client leaf)
    SMSG_PLAYER_MOVE                             = 0x1A32,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_FLIGHT_BACK_SPEED       = 0x0B28,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_FLIGHT_SPEED            = 0x1DAB,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_PITCH_RATE              = 0x0AB3,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_RUN_BACK_SPEED          = 0x1F9F,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_RUN_SPEED               = 0x02F1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_SWIM_SPEED              = 0x1D8E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_SWIM_BACK_SPEED         = 0x0046,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_TURN_RATE               = 0x0832,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_SPLINE_MOVE_SET_WALK_SPEED              = 0x08B2,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REORDER_CHARACTERS                      = 0x08A7, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SET_CURRENCY_WEEK_LIMIT                 = 0x0E2A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_UPDATE_CURRENCY_WEEK_LIMIT)
    SMSG_SET_CURRENCY                            = 0x1595,    // (legacy; no client leaf)
    SMSG_SEND_CURRENCIES                         = 0x15A5,    // (legacy; no client leaf)
    CMSG_SET_CURRENCY_FLAGS                      = 0x03E4, // 5.4.8 18414 (Wow.exe binary)
    SMSG_WEEKLY_RESET_CURRENCIES                 = 0x023E,    // 5.4.8 18414 (Wow.exe leaf; was 0x3CA1, unframable)
    CMSG_INSPECT_RATED_BG_STATS                  = 0x0882, // 5.4.8 18414 (Wow.exe binary, via CMSG_REQUEST_INSPECT_RATED_BG_STATS)
    CMSG_REQUEST_RATED_BG_STATS                  = 0x0826, // 5.4.8 18414 (Wow.exe binary)
    CMSG_REQUEST_CONQUEST_FORMULA_CONSTANTS      = 0x0365, // 5.4.8 18414 (Wow.exe empty writer; arena initialization call path)
    SMSG_CONQUEST_FORMULA_CONSTANTS              = 0x0EAB, // 5.4.8 18414 (Wow.exe reader sub_6BE42C; conquest-cap formula consumers)
    CMSG_REQUEST_PVP_REWARDS                     = 0x0375, // 5.4.8 18414 (Wow.exe binary)
    SMSG_PVP_REWARDS                             = 0x08AA,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus; fuzzy via SMSG_REQUEST_PVP_REWARDS_RESPONSE)
    CMSG_REQUEST_PVP_OPTIONS_ENABLED             = 0x0A22, // 5.4.8 18414 (Wow.exe binary)
    CMSG_BATTLE_PET_REQUEST_JOURNAL             = 0x0A23, // 5.4.8 18414 (Wow.exe empty writer sub_66218D; PetJournalInfo.cpp workflow)
    SMSG_PVP_OPTIONS_ENABLED                     = 0x080A,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_REQUEST_HOTFIX                          = 0x158D, // 5.4.8 18414 (Wow.exe binary)
    SMSG_DB_REPLY                                = 0x103B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_OBJECT_UPDATE_FAILED                    = 0x1061, // 5.4.8 18414 (Wow.exe binary)
    CMSG_REFORGE_ITEM                            = 0x0C4F, // 5.4.8 18414 (Wow.exe binary)
    SMSG_REFORGE_RESULT                          = 0x141E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_LOAD_SCREEN                             = 0x1DBD, // 5.4.8 18414 (Wow.exe binary)
    CMSG_BATTLE_PAY_GET_PURCHASE_LIST            = 0x18B2, // 5.4.8 18414 (C_PurchaseAPI.GetPurchaseList -> sub_92EADD -> empty writer sub_661E3F)
    CMSG_BATTLE_PAY_GET_PRODUCT_LIST             = 0x0DE0, // 5.4.8 18414 (corpus: 13 captures, always a 0-byte body)
    CMSG_QUERY_COUNTDOWN_TIMER                   = 0x044E, // 5.4.8 18414 (Wow.exe writer sub_690D37; retained usage literal)
    SMSG_START_TIMER                             = 0x0E3F,    // 5.4.8 18414 (Wow.exe reader sub_6E7584; leaf sub_90BF87)
    CMSG_ENABLE_NAGLE                            = 0x12B3, // 5.4.8 18414 (Wow.exe binary)
    CMSG_GUILD_ACHIEVEMENT_MEMBERS               = 0x1470, // 5.4.8 18414 (Wow.exe binary)
    CMSG_BATTLEMASTER_JOIN_RATED                 = 0x0674, // 5.4.8 18414 (Wow.exe binary)
    CMSG_CLEAR_RAID_MARKER                       = 0x1443, // 5.4.8 18414 (Wow.exe binary)
    SMSG_ARENA_UNIT_DESTROYED                    = 0x000F,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_LF_GUILD_POST_REQUEST                   = 0x1450, // 5.4.8 18414 (Wow.exe binary)
    CMSG_QUERY_GUILD_MEMBERS_FOR_RECIPE          = 0x0CFA, // 5.4.8 18414 (Wow.exe binary)
    CMSG_QUERY_GUILD_MEMBER_RECIPES              = 0x04F0, // 5.4.8 18414 (Wow.exe binary)
    CMSG_QUERY_GUILD_RECIPES                     = 0x0478, // 5.4.8 18414 (Wow.exe binary)
    CMSG_SET_PET_SLOT                            = 0x10A7, // 5.4.8 18414 (Wow.exe binary)
    CMSG_UNLEARN_SPECIALIZATION                  = 0x1841, // 5.4.8 18414 (Wow.exe binary)
    SMSG_MULTIPLE_PACKETS                        = 0x1168,    // 5.4.8 18414 (Wow.exe leaf; name unverified)
    SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT          = 0x0332,    // 5.4.8 18414 (Wow.exe leaf; name single-source fork)
    CMSG_BATTLEFIELD_MGR_QUEUE_REQUEST           = 0x1283, // 5.4.8 18414 (Wow.exe binary)
    CMSG_LOG_DISCONNECT                          = 0x10B3, // 5.4.8 18414 (Wow.exe binary)
    SMSG_SET_TIME_ZONE_INFORMATION               = 0x19C1,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    CMSG_GUILD_REQUEST_PARTY_STATE               = 0x10C3, // 5.4.8 18414 (Wow.exe binary; thunk sub_6901B6 pushes 0x10C3)
    CMSG_GUILD_REQUEST_CHALLENGE_UPDATE          = 0x147A, // 5.4.8 18414 (Wow.exe binary; thunk sub_C84E79 pushes 0x147A)

    // ---------------------------------------------------------------------
    // Client dispatch leaves the server did not previously declare.
    //
    // Every VALUE below is confirmed against Wow.exe 5.4.8.18414: each is an
    // implemented world-path dispatch leaf, recovered by emulating the client's
    // receive dispatch rather than taken from any table. Nothing in the server
    // sends these yet -- they are declared so a handler can be written against a
    // correct value.
    //
    // The NAMES are a different matter and are tagged as such. They come from
    // community fork tables, whose accuracy for these rows has not been
    // measured. Where the reference header grades a name low-confidence or
    // single-source, that is carried through. Treat a name as a label to be
    // confirmed, not as evidence.
    // ---------------------------------------------------------------------
    SMSG_MOVE_UPDATE_WALK_SPEED                  = 0x0047,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SOR_START_EXPERIENCE_INCOMPLETE         = 0x0083,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_RAID_MARKERS_CHANGED                    = 0x008A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_VOID_STORAGE_CONTENTS                   = 0x008B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_DISPLAY_PROMOTION                       = 0x00A3,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PET_BATTLE_QUEUE_STATUS                 = 0x00A6,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_ARENA_PREP_OPPONENT_SPECIALIZATIONS     = 0x00BE,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SPELL_EXECUTE_LOG                       = 0x00D8,    // 5.4.8 18414 (Wow.exe leaf; name via 5.4.7 bridge; reader sub_C7F954)
    SMSG_MOVE_UPDATE_FLIGHT_SPEED                = 0x00E1,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MOVE_UPDATE_SWIM_SPEED                  = 0x01E2,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PET_JOURNAL_LOCK_DENINED         = 0x0203,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    // The client reader uses fixed buffers. Keep dormant until both strings are proved and bounded.
    SMSG_GM_TICKET_RESPONSE                      = 0x0207,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PAY_DELIVERY_ENDED               = 0x020B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_PET_BATTLE_REQUEST_FAILED               = 0x022F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_BATTLE_PAY_GET_PURCHASE_LIST_RESPONSE   = 0x023A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_MOVE_UPDATE_SWIM_BACK_SPEED             = 0x025A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_RAID_TARGET_UPDATE_ALL                  = 0x0283,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PETITION_ALREADY_SIGNED                 = 0x0286,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GM_TICKET_UPDATE                        = 0x02A6,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_RESPEC_WIPE_CONFIRM                     = 0x02AB,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MESSAGE_BOX                             = 0x02AE,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_RAID_READY_CHECK_CONFIRM                = 0x02AF,    // 5.4.8 18414 (Wow-64.exe binary receive leaf)
    SMSG_RAID_READY_CHECK_COMPLETED              = 0x15C2,    // 5.4.8 18414 (Wow-64.exe binary receive leaf)
    SMSG_MOVE_REMOVE_MOVEMENT_FORCE              = 0x0341,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_QUEST_NPC_QUERY_RESPONSE                = 0x036D,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PET_PET_UPDATES                  = 0x041A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PAY_GET_DISTRIBUTION_LIST_RESPONSE = 0x043F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_CALENDAR_EVENT_MODERATOR_STATUS         = 0x048F,    // 5.4.8 18414 (Wow.exe reader; name via 5.4.7 bridge)
    SMSG_PET_BATTLE_FINISHED                     = 0x04BB,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MIRROR_IMAGE_CREATURE_DATA              = 0x04D0,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MIRROR_IMAGE_COMPONENTED_DATA           = 0x04D9,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    // 0x0591 is BIDIRECTIONAL -- the same value carries the CMSG and the SMSG. It is absent
    // from the perfect-hash SMSG dispatcher (sub_659694) because this client has a second
    // SMSG delivery subsystem that bypasses it, so that absence is not evidence against the
    // value, and the corpus rows flagged directionConflict are explained by the shared value
    // rather than being catalogue defects.
    SMSG_SET_RAID_DIFFICULTY                     = 0x0591,    // 5.4.8 18414 (Wow.exe; one uint32 RAW client DifficultyID)
    // Dispatcher case 57, and case 57 has exactly ONE member -- brute-forced over all 65536
    // u16 inputs against a hash validated first on the known 0x183B -> case 493 -> SMSG_MOTD
    // anchor. Handler sub_6D8F3E has exactly one xref, inside that case.
    //
    // The reader sub_6D9F28 is NOT an opcode fingerprint: it is the shared constructor for
    // every single-uint32 SMSG and has 50+ xrefs. The identification rests on the case index
    // and the handler, not the reader.
    //
    // Payload is the RAW client DifficultyID. Across 954 build-18414 captures the value set is
    // exactly {1 x512, 2 x424, 8 x18} -- the three DIFFICULTY_DUNGEON_* constants from
    // FrameXML/Constants.lua, with ZERO occurrences of 0. The 18 occurrences of 8 are decisive:
    // no internal 0-based dungeon mode can produce 8.
    SMSG_SET_DUNGEON_DIFFICULTY                  = 0x1283,    // 5.4.8 18414 (Wow.exe case 57, unique; one uint32 RAW client DifficultyID)
    SMSG_WAIT_QUEUE_FINISH                       = 0x060E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PAY_START_PURCHASE_RESPONSE      = 0x0612,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_PET_BATTLE_FIRST_ROUND                  = 0x0613,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_CLEAR_BOSS_EMOTES                       = 0x062B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    CMSG_REALM_NAME_QUERY                        = 0x1A16,    // 5.4.8 18414 (Wow.exe binary; client-confirmed live)
    SMSG_REALM_NAME_QUERY_RESPONSE               = 0x063E,    // 5.4.8 18414 (Wow.exe leaf; RE-verified handler sub_1403073A0)
    SMSG_PVP_SEASON                              = 0x069B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SET_PLAY_HOVER_ANIM                     = 0x069F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_QUEST_PUSH_RESULT                       = 0x074D,    // 5.4.8 18414 (Wow.exe reader; ERR_QUEST_PUSH result path)
    SMSG_BATTLEFIELD_MGR_ENTERED                 = 0x081B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PETITION_RENAME_RESULT                  = 0x082A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PET_BATTLE_FINALIZE_LOCATION            = 0x082E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLEFIELD_MGR_STATE_CHANGE            = 0x083A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_CHARACTER_UPGRADE_COMPLETE              = 0x083B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_MOVE_SET_VEHICLE_REC_ID                 = 0x0861,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MAIL_QUERY_NEXT_TIME_RESULT             = 0x089B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MOVE_UPDATE_RUN_BACK_SPEED              = 0x08A3,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_RESEARCH_SETUP_HISTORY                  = 0x08AB,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_CHAT_SERVER_RECONNECTED                 = 0x0A2E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_TABARD_VENDOR_ACTIVATE                  = 0x0A3E,    // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_RANKS_UPDATE                      = 0x0A60,    // 5.4.8 18414 (Wow.exe reader/handler; guild member rank UI)
    SMSG_GUILD_EVENT_PLAYER_JOINED               = 0x0B69,    // 5.4.8 18414 (Wow.exe reader/handler; GUILD_ROSTER_UPDATE)
    SMSG_GUILD_EVENT_PRESENCE_CHANGE             = 0x0B70,    // 5.4.8 18414 (Wow.exe reader/handler; guild presence UI)
    SMSG_GUILD_EVENT_NEW_LEADER                  = 0x0E69,    // 5.4.8 18414 (Wow.exe reader/handler; guild leader UI)
    SMSG_GUILD_EVENT_DISBANDED                   = 0x1E68,    // 5.4.8 18414 (Wow.exe reader/handler; GUILD_ROSTER_UPDATE)
    SMSG_GUILD_EVENT_BANK_TAB_TEXT_CHANGED       = 0x0A70,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_PARTY_STATE_RESPONSE              = 0x0A78,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_ALL_ACCOUNT_CRITERIA                    = 0x0A9E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_MOVE_UPDATE_APPLY_MOVEMENT_FORCE        = 0x0AB6,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_LF_GUILD_MEMBERSHIP_LIST_UPDATED        = 0x0AE0,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_MOVE_STARTING                     = 0x0AE1,    // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_NEWS_UPDATE                       = 0x0AE8,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GUILD_CHALLENGE_UPDATED                 = 0x0AE9,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_XP                                = 0x0AF0,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SPLINE_MOVE_SET_NORMAL_FALL             = 0x0B08,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GUILD_EVENT_MOTD                        = 0x0B68,    // 5.4.8 18414 (Wow.exe handler fires GUILD_MOTD and GUILD_ROSTER_UPDATE)
    SMSG_LF_GUILD_APPLICANT_LIST_UPDATED         = 0x0B71,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_MOVE_COMPLETE                     = 0x0BE8,    // 5.4.8 18414 (Wow.exe binary)
    SMSG_GUILD_MEMBERS_FOR_RECIPE                = 0x0BF0,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_EVENT_BANK_TAB_MODIFIED           = 0x0BF1,    // 5.4.8 18414 (Wow.exe binary: sub_68EC4C idx 29 -> lookup_table_68F810[29]=22 -> jump_table_68F708[22]=0x68F667 -> sub_6A39C1 -> parser sub_6A224B -> consumer sub_96ED66 -> event 0x1AF. Routing/body/meaning binary-confirmed 2026-08-23; the LABEL is still the fork tables' -- no such string is in the client. Body and reasoning: MopGuildBankPackets::BuildGuildBankTabModified. Opcodes_reference.h's [low-conf] is the clean-room pass's, not ours to flip; superseded by this)
    SMSG_GUILD_EVENT_PLAYER_LEFT                 = 0x0BF8,    // 5.4.8 18414 (Wow.exe reader/handler; guild leave/remove UI)
    SMSG_RESEARCH_COMPLETE                       = 0x0C0E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MONEY_NOTIFY                            = 0x0C0F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_LFG_SLOT_INVALID                        = 0x0C12,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_PET_BATTLE_ROUND_RESULT                 = 0x0C1A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_LFG_UPDATE_STATUS                       = 0x0C2E,    // 5.4.8 18414 (Wow.exe binary)
    SMSG_WAIT_QUEUE_UPDATE                       = 0x0C2F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_AE_LOOT_TARGETS                         = 0x0C32,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MOVE_SET_ACTIVE_MOVER                   = 0x0C6D,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GAME_OBJECT_ACTIVATE_ANIM_KIT           = 0x0C8A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_SET_MOVEMENT_ANIM_KIT                   = 0x0CAA,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_VIGNETTE_UPDATE                         = 0x0CBE,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SPELL_PERIODIC_AURA_LOG                 = 0x0CF2,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MOVE_UPDATE_TURN_RATE                   = 0x0D62,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PET_BATTLE_INITIAL_UPDATE               = 0x0E1E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_LOAD_CUF_PROFILES                       = 0x0E32,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_ITEM_EXPIRE_PURCHASE_REFUND             = 0x0E33,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_LF_GUILD_RECRUIT_LIST_UPDATED           = 0x0E68,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_RENAMED                           = 0x0E70,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_BATTLEFIELD_RATED_INFO                  = 0x0EBA,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GUILD_MEMBER_RECIPES                    = 0x0EE1,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GUILD_ACHIEVEMENT_DATA                  = 0x0EF8,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GUILD_EVENT_BANK_MONEY_CHANGED          = 0x0F68,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_LF_GUILD_BROWSE_UPDATED                 = 0x0F69,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_NEWS_DELETED                      = 0x0F70,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GUILD_XP_GAIN                           = 0x0FE0,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_INVITE_CANCEL                     = 0x0FE1,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_FLAGGED_FOR_RENAME                = 0x0FE9,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_RECIPES                           = 0x0FF1,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GROUP_ROLE_POLL_INFORM                  = 0x1007,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_LOOT_ROLLS_COMPLETE                     = 0x101B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_BLACK_MARKET_OUTBID                     = 0x1040,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_BLACK_MARKET_WON                        = 0x1060,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_MOVE_SET_CAN_TURN_WHILE_FALLING         = 0x1065,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_ARCHAEOLOGY_SURVERY_CAST                = 0x1160,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_ATTACKSWING_ERROR                       = 0x11E1,    // 5.4.8 18414 (Wow.exe leaf; name via 5.4.7 bridge; reader sub_6F568B)
    SMSG_PLAY_SPELL_VISUAL_KIT                   = 0x11E3,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PET_BATTLE_QUEUE_PROPOSE_MATCH          = 0x1202,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_DIFFERENT_INSTANCE_FROM_PARTY           = 0x120B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_CANCEL_SCENE                            = 0x120E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_ENCOUNTER_END                           = 0x120F,    // 5.4.8 18414 (Wow.exe binary)
    SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN       = 0x121E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_PLAYER_DIFFICULTY_CHANGE                = 0x128E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_UPDATE_CURRENCY                         = 0x129E,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_DEBUG_RUNE_REGEN                        = 0x12A7,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_TITLE_LOST                              = 0x12BF,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_RANDOM_ROLL                             = 0x141A,    // 5.4.8 18414 (Wow.exe literal RANDOM_ROLL_RESULT; reader sub_6D18F6)
    SMSG_MOVE_UPDATE_REMOVE_MOVEMENT_FORCE       = 0x1464,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BLACK_MARKET_BID_RESULT                 = 0x148A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_VOID_STORAGE_TRANSFER_CHANGES           = 0x14BA,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PET_QUERY_NAME_RESPONSE          = 0x1540,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PET_JOURNAL                      = 0x1542,    // 5.4.8 18414 (Wow.exe reader sub_7564B1; handler sub_8E4007)
    SMSG_MOVE_UPDATE_RUN_SPEED                   = 0x158E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MOVE_COLLISION_DISABLE                  = 0x15B8,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SHOW_NEURTRAL_PLAYER_FACTION_SELECT_UI  = 0x15E0,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_RAID_TARGET_UPDATE_SINGLE               = 0x160B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PET_STABLE_LIST                         = 0x1613,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_MINIMAP_PING                            = 0x168F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_RANDOMIZE_CHAR_NAME                     = 0x169F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    // The request half of the creation screen's randomise button. VERIFIED IN THE BINARY,
    // not inferred: RequestRandomName (sub_1403B3E80) reads the char-create globals at
    // +0x40 and +0x44 and hands them to sub_1403A3290, whose packet constructor writes the
    // opcode literal 2844 = 0x0B1C (sub_1403CB780). Zero observations in the 18414 corpus,
    // so the binary is the only evidence -- but it is direct evidence, not a fork table.
    CMSG_GENERATE_RANDOM_CHARACTER_NAME          = 0x0B1C,    // 5.4.8 18414 (Wow.exe sender, decompiled)
    SMSG_CALENDAR_EVENT_INITIAL_INVITE           = 0x16AE,    // 5.4.8 18414 (Wow.exe reader; name via 5.4.7 bridge)
    SMSG_BATTLE_PET_SLOT_UPDATE                  = 0x16AF,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MOVE_UPDATE_COLLISION_HEIGHT            = 0x1812,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_DISPLAY_GAME_ERROR                      = 0x181F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SPLINE_MOVE_SET_WATER_WALK              = 0x1823,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_MOVE_COLLISION_ENABLE                   = 0x1826,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_STREAMING_MOVIE                         = 0x1843,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SPELLINTERRUPTLOG                       = 0x1851,    // 5.4.8 18414 (Wow.exe reader/handler -> SPELL_INTERRUPT)
    SMSG_CHARACTER_UPGRADE_STARTED               = 0x188A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_SPLINE_MOVE_SET_FEATHER_FALL            = 0x1893,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_ACCOUNT_CRITERIA_UPDATE                 = 0x189E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_BATTLE_PET_DELETED                      = 0x18AB,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_SPLINE_MOVE_SET_LAND_WALK               = 0x18B6,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLEFIELD_MGR_EJECTED                 = 0x18C2,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PET_JOURNAL_LOCK_ACQUIRED        = 0x1A0F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    CMSG_CORPSE_MAP_POSITION_QUERY               = 0x0A16,    // 5.4.8 18414 (Wow.exe packed-GUID writer)
    SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE      = 0x1A3A,    // 5.4.8 18414 (Wow.exe transport-transform reader)
    SMSG_GUILD_REWARDS_LIST                      = 0x1A69,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_LF_GUILD_APPLICATIONS_LIST_CHANGED      = 0x1A70,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_REPUTATION_WEEKLY_CAP             = 0x1A71,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_LF_GUILD_COMMAND_RESULT                 = 0x1A79,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_SETUP_CURRENCY                          = 0x1A8B,    // 5.4.8 18414 (Wow.exe leaf; name reference-consensus)
    SMSG_BATTLE_PAY_GET_PRODUCT_LIST_RESPONSE    = 0x1ABF,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_CHALLENGE_COMPLETED               = 0x1AF8,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_CRITERIA_DELETED                  = 0x1B60,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_ACHIEVEMENT_MEMBERS               = 0x1B70,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_LF_GUILD_POST_UPDATED                   = 0x1B71,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_MAX_DAILY_XP                      = 0x1BE1,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GUILD_MEMBER_DAILY_RESET                = 0x1BE8,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_EVENT_BANK_TAB_ADDED              = 0x1BE9,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_CRITERIA_DATA                     = 0x1BF0,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_GUILD_ACHIEVEMENT_EARNED                = 0x1BF1,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PET_BATTLE_FINAL_ROUND                  = 0x1C2F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PLAY_SCENE                              = 0x1C3A,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_RAID_READY_CHECK                        = 0x1C8E,    // 5.4.8 18414 (Wow-64.exe binary receive leaf)
    SMSG_CALENDAR_EVENT_INVITE_STATUS            = 0x1C9B,    // 5.4.8 18414 (Wow.exe reader; name via 5.4.7 bridge)
    SMSG_VOID_TRANSFER_RESULT                    = 0x1C9E,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_CUSTOM_LOAD_SCREEN                      = 0x1CAF,    // 5.4.8 18414 (Wow.exe leaf; name fork tables, low confidence)
    SMSG_MOVE_APPLY_MOVEMENT_FORCE               = 0x1DBE,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_PET_BATTLE_PVP_CHALLENGE                = 0x1E0B,    // 5.4.8 18414 (Wow.exe binary)
    SMSG_BATTLE_PAY_DISTRIBUTION_UPDATE          = 0x1E1B,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GROUP_SET_ROLE                          = 0x1E1F,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_BATTLE_PAY_DELIVERY_STARTED             = 0x1E32,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_GUILD_ACHIEVEMENT_DELETED               = 0x1E61,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_ENCOUNTER_START                         = 0x1E8A,    // 5.4.8 18414 (Wow.exe binary)
    SMSG_HOTFIX_NOTIFY_BLOB                      = 0x1EBA,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
    SMSG_VOID_ITEM_SWAP_RESPONSE                 = 0x1EBF,    // 5.4.8 18414 (Wow.exe leaf; name fork tables)
};

#define OPCODE_TABLE_SIZE 0x2000   // 13-bit wire space; greeting 0x4F57 is OUT of range
// Eluna submodule (shared across MaNGOS cores) references NUM_MSG_TYPES as the opcode upper bound.
// Keep the pre-migration value so Phase 1a leaves Eluna's accepted opcode range unchanged (non-closure
// opcodes still carry Four values up to 0xFFFF). Phase 1b tightens this to OPCODE_TABLE_SIZE once every
// opcode is < 0x2000.
#define NUM_MSG_TYPES 0xFFFF

/**
 * Initializes opcode handler metadata tables.
 */
extern void InitializeOpcodes();

/// Player state
enum SessionStatus
{
    STATUS_AUTHED = 0,                     ///< Player authenticated (_player==NULL, m_playerRecentlyLogout = false or will be reset before handler call)
    STATUS_LOGGEDIN,                       ///< Player in game (_player!=NULL, inWorld())
    STATUS_TRANSFER,                       ///< Player transferring to another map (_player!=NULL, !inWorld())
    STATUS_LOGGEDIN_OR_TRANSFER,           ///< Player exists, either in world or transferring (_player!=NULL)
    STATUS_LOGGEDIN_OR_RECENTLY_LOGGEDOUT, ///< _player!= NULL or _player==NULL && m_playerRecentlyLogout)
    STATUS_NEVER,                          ///< Opcode not accepted from client (deprecated or server side only)
    STATUS_UNHANDLED                       ///< We don' handle this opcode yet
};

/**
 * This determines how a \ref WorldPacket is handled by MaNGOS. This can be either in the
 * same function as we received it in, this is unusual, or it can be in:
 * - \ref World::UpdateSessions if it's not thread safe
 * - \ref Map::Update if it is thread safe
 */
enum PacketProcessing
{
    PROCESS_INPLACE = 0,   ///< process packet whenever we receive it - mostly for non-handled or non-implemented packets
    PROCESS_THREADUNSAFE,  ///< packet is not thread-safe - process it in \ref World::UpdateSessions
    PROCESS_THREADSAFE     ///< packet is thread-safe - process it in \ref Map::Update
};

class WorldPacket;
#if COMPILER == COMPILER_MICROSOFT
// WorldSession has no base classes. State the member-pointer model explicitly so
// MSVC does not choose a different representation while the class is incomplete.
class __single_inheritance WorldSession;
#else
class WorldSession;
#endif

/**
 * A structure containing some of the necessary info to handle a \ref WorldPacket when it comes in.
 * The most interesting thing in here is the \ref OpcodeHandler::handler that actually does
 * something with one of the opcodes (see \ref Opcodes) that came in.
 */
struct OpcodeHandler
{
    ///A string representation of the name of this opcode (see \ref Opcodes)
    char const* name;
    ///The status for this handler, it tells whether or not we will handle the packet at all and
    ///when we will handle it.
    SessionStatus status;
    ///This tells where the packet should be processed, ie: is it thread un/safe, which in turn
    ///determines where it will be processed
    PacketProcessing packetProcessing;
    ///The callback called for this opcode which will work some magic
    void (WorldSession::*handler)(WorldPacket& recvPacket);
};

enum PacketDirection { DIR_CLIENT, DIR_SERVER };
extern OpcodeHandler clientOpcodeTable[OPCODE_TABLE_SIZE];
extern OpcodeHandler serverOpcodeTable[OPCODE_TABLE_SIZE];
OpcodeHandler const* LookupClientOpcode(uint16 value);
char const* LookupClientOpcodeName(uint16 value);
char const* LookupServerOpcodeName(uint16 value);
char const* LookupOpcodeName(PacketDirection dir, uint16 value);
#endif

/**
 * \var OpcodesList::SMSG_PERIODICAURALOG
 * This opcode is used to send data for the combat log when you receive either periodic damage or
 * buffs from a \ref Aura in some way, ie  you gain 10 life every second, you increase your regen
 * of power or something along those lines. The data that needs to be sent is a little different
 * depending on the \ref Modifier for the \ref Aura, what should always be included though is:
 * - The victims Pack GUID (see \ref Object::GetPackGUID)
 * - The casting \ref Player s Pack GUID (see \ref Object::GetPackGUID)
 * - The spellid for the \ref Aura (see \ref Aura::GetId) as a \ref uint32
 * - A 1 as a \ref uint32 this is the count of something (what)
 * - The id of the aura see \ref Modifier::m_auraname as a \ref uint32
 *
 * Now comes different parts depending on what value the \ref Modifier::m_auraname has, if it
 * is \ref AuraType::SPELL_AURA_PERIODIC_DAMAGE or
 * \ref AuraType::SPELL_AURA_PERIODIC_DAMAGE_PERCENT then this is sent:
 * - Damage done as a \ref uint32 from \ref SpellPeriodicAuraLogInfo::damage
 * - The \ref SpellSchools of the \ref SpellEntry for the \ref Aura as a \ref uint32 (see
 * \ref SpellEntry::School)
 * - How much that was absorbed as a \ref uint32
 * - How mcuh that was resisted as a \ref uint32
 *
 * If the \ref Modifier::m_auraname has one of the values of:
 * \ref AuraType::SPELL_AURA_PERIODIC_HEAL or \ref AuraType::SPELL_AURA_OBS_MOD_HEALTH then
 * this should be sent:
 * - Damage/healing (in this case) done as a \ref uint32
 *
 * If the \ref Modifier::m_auraname has one of the values of:
 * \ref AuraType::SPELL_AURA_OBS_MOD_MANA or \ref AuraType::SPELL_AURA_PERIODIC_ENERGIZE then
 * this should be sent:
 * - The \ref Modifier::m_miscvalue as a \ref uint32, in this case it's a power type from the
 * \ref Powers
 * - The damage/mana earned (in this case) as a \ref uint32
 *
 * If the \ref Modifier::m_auraname has one of the values of:
 * \ref AuraType::SPELL_AURA_PERIODIC_MANA_LEECH then this should be sent:
 * - The \ref Modifier::m_miscvalue as a \ref uint32, in this case it's a power type from the
 * \ref Powers
 * - The damage/amount of mana drained (in this case) as a \ref uint32
 * - The gain multiplier as a \ref float from the which probably increases how much power was
 * drained
 *
 * To not create this packet and send it all the time you need it you can use
 * \ref Unit::SendPeriodicAuraLog
 *
 * Also, this should be sent with \ref Object::SendMessageToSet so that all nearby (in
 * the same \ref Cell) \ref Player s get the information. To do this with an \ref Aura
 * one could use \ref Aura::GetTarget and then use the \ref Unit::SendMessageToSet
 * \todo Is it actually for the combat log?
 * \todo Is it in the same \ref Cell?
 * \todo What is the count that is sent as a uint32?
 * \todo Document the multiplier in some way?
 */

/**
 * \var OpcodesList::SMSG_SPELLNONMELEEDAMAGELOG
 * This opcode is used to send data for the combat log when you damage someone with a non melee
 * spell, ie frostbolt.
 * The data that needs to be sent is the following in the same order:
 * - The victims Pack GUID (see \ref Object::GetPackGUID)
 * - The \ref Player s Pack GUID (see \ref Object::GetPackGUID)
 * - Id of the spell that was used as a \ref uint32
 * - The amount of damage that was done (not including resisted damage etc) as a \ref uint32
 * - The \ref SpellSchoolMask of the \ref Spell as a \ref uint8, should be from the representation
 * in \ref SpellSchools though, to do this one can use \ref GetFirstSchoolInMask
 * - The amount of absorbed damage as a \ref uint32
 * - The amount of resisted damage as a \ref uint32
 * - A \ref uint8 which if it is 1 shows the spell name for the client, ie: "%s's ranged shot
 * hit %s for %d damage" (taken from source) and if it's 0 no message is shown
 * - A \ref uint8 value that seems to be unused
 * - The amount of blocked damage as a \ref uint32
 * - The \ref HitInfo as a \ref uint32 which tells what happened it would seem
 * - A \ref uint8 that's usually 0 and is used as a flag to use extended data (taken from source)
 *
 * To not create this packet and send it all the time you need it you can use
 * \ref Unit::SendSpellNonMeleeDamageLog
 *
 * Also, this should be sent with \ref Object::SendMessageToSet so that all nearby (in
 * the same \ref Cell) \ref Player s get the information.
 * \todo Is it actually for the combat log?
 * \todo Is it in the same \ref Cell?
 */

/**
 * \var OpcodesList::SMSG_SPELLENERGIZELOG
 * This opcode is used to send data for the combat log when you gain energy in some way.
 * The data that needs to be sent is the following in the same order:
 * - The victims Pack GUID (see \ref Object::GetPackGUID)
 * - The \ref Player s Pack GUID (see \ref Object::GetPackGUID)
 * - the spellid as a \ref uint32
 * - the powertype as a \ref uint32, see \ref Powers for the available power types
 * - the damage or in this case gain as a \ref uint32
 *
 * To not create this packet and send it all the time you need it you can use
 * \ref Unit::SendEnergizeSpellLog
 * Also, this should be sent with \ref Object::SendMessageToSet so that all nearby (in
 * the same \ref Cell) \ref Player s get the information.
 * \todo Is it actually for the combat log?
 * \todo Is it in the same \ref Cell?
 */

/**
 * \var OpcodesList::SMSG_SPELLHEALLOG
 * This opcode is used to send data for the combat log when healing is done. The data
 * that needs to be sent is the following in the same order:
 * - The victims Pack GUID (see \ref Object::GetPackGUID)
 * - The \ref Player s Pack GUID (see \ref Object::GetPackGUID)
 * - The spellid as a \ref uint32
 * - The damage/healing done as a \ref uint32
 * - If it was critical or not as a \ref uint8 (1 meaning critical, 0 meaning normal)
 * - And a \ref uint8 with the value 0 which doesn't seem to be used in the client
 *
 * To not create this packet and send it all the time you need it you can use
 * \ref Unit::SendHealSpellLog
 * Also, this should be sent with \ref Object::SendMessageToSet so that all nearby (in
 * the same \ref Cell) \ref Player s get the information.
 * \todo Is it actually for the combat log?
 * \todo Is it in the same \ref Cell?
 */

/**
 * \var OpcodesList::SMSG_ATTACKERSTATEUPDATE
 * This opcode is used to send information about a recent hit, who it hit, how
 * much damage it did and so forth. See the \ref CalcDamageInfo structure for more
 * info on what will be sent. The data that needs to be sent is the following in
 * the same order:
 * - The \ref CalcDamageInfo::HitInfo as a \ref uint32
 * - The \ref Unit s Pack GUID (see \ref Object::GetPackGUID)
 * - The targets Pack GUID (see \ref Object::GetPackGUID)
 * - The full damage that was done as a \ref uint32
 * - A 1 as a \ref uint8, this acts as the subdamage count (could it be higher?)
 * - A \ref uint32 of \code{.cpp} GetFirstSchoolInMask(damageInfo->damageSchoolMask) \endcode
 * Need to find out what this does
 * - A float representation of the damage (seen as sub damage from comments)
 * - A \ref uint32 representation of the same damage
 * - A \ref uint32 representation of how much was absorbed (see \ref CalcDamageInfo::absorb)
 * - A \ref uint32 representation of how much was resisted (see \ref CalcDamageInfo::resist)
 * - The targets state as a \ref uint32 (see \ref CalcDamageInfo::TargetState)
 * - If the absorbed part is zero add a 0 as an \ref uint32 otherwise add a -1 as an \ref uint32
 * - The spell id as a \ref uint32 if a spell was used, although in
 * \ref Unit::SendAttackStateUpdate it is always 0.
 * - The blocked amount as a \ref uint32 (see \ref CalcDamageInfo::blocked_amount) this is
 * normally \ref HitInfo::HITINFO_NOACTION according to comments in \ref Unit::SendAttackStateUpdate
 *
 * It appears this should also be sent with \ref Object::SendMessageToSet to that all nearby (in
 * the same \ref Cell) \ref Player s can get take part of the info
 * \see VictimState
 * \todo Is this correct? Is it really about a recent hit?
 */


/// @}
