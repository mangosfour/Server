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

#include "WorldGateway.h"

#include "DBCStores.h"
#include "Database/DatabaseEnv.h"
#include "Log/Log.h"
#include "MopAuthResponse.h"
#include "Opcodes.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"

#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace
{
    /**
     * @brief The account row, read once during LookupAccount and reused by Attach.
     *
     * This is exactly the set of columns WorldSocket::HandleAuthSession used to pull
     * out of the login database and then carry, as loose locals, through two
     * hundred-odd lines of interleaved policy (WorldSocket.cpp:1257-1577, deleted in
     * this commit). Naming it makes the hand-off explicit and stops the row being
     * fetched twice.
     *
     * NOT carried: an `s` (SRP6 salt) column or an `os` (client OS) column -- M4's
     * account table has neither. WorldSocket.cpp's own query
     * (`id, gmlevel, sessionkey, last_ip, locked, expansion, mutetime, locale`) never
     * selected them, so there is nothing to port; do not add either column here on
     * the strength of a sibling fork's shape.
     */
    struct AccountRow : public proto::AuthContext
    {
        uint32         id        = 0;
        AccountTypes   security  = SEC_PLAYER;
        uint8          expansion = 0;
        time_t         muteTime  = 0;
        LocaleConstant locale    = LOCALE_enUS;

        /// Canonical raw-40 K, converted once via MopAuth::SessionKeyFromHex (spec
        /// 5.1's ladder) and reused verbatim -- never round-tripped through
        /// BigNumber. See Auth/MopAuthKey.h.
        uint8 sessionKey[MopAuth::SESSION_KEY_LEN] = {};
    };

    /**
     * @brief Register the calling thread with the database client library.
     *
     * proto calls LookupAccount()/Attach() on one of the network engine's worker
     * threads, and those threads query LoginDatabase. The MySQL client keeps
     * per-thread state that every such thread has to set up and tear down, or it
     * corrupts and leaks it -- a failure that shows up far from its cause and only
     * under load. Same idiom MapUpdater's worker threads already use
     * (MapUpdater.cpp:143) for exactly the same reason.
     *
     * Lives here rather than in the network engine on purpose: net must not know a
     * database exists. A function-local thread_local runs the guard's constructor
     * the first time a given worker thread reaches this code and its destructor
     * when that thread exits, so the transport stays oblivious while every thread
     * that touches the database is still accounted for.
     */
    void EnsureDbThreadRegistered()
    {
        static thread_local DbThreadGuard guard(&LoginDatabase);
        (void)guard;
    }
}

WorldGateway::WorldGateway()
    : m_nextId(1)
{
}

WorldGateway::~WorldGateway()
{
}

proto::AuthLookup WorldGateway::LookupAccount(const proto::AuthRequest& request)
{
    EnsureDbThreadRegistered();

    proto::AuthLookup result;

    // ---- Client build --------------------------------------------------------
    // WorldSocket.cpp:1303.
    //
    // Wire build, not data-file build. IsAcceptableClientBuild also accepts 18273 because the
    // 18414 client tags its own DBCs and maps that way; admitting an 18273 CLIENT would hand it
    // an 18414 opcode table it cannot parse. A real 18414 client reports 18414 here.
    if (!IsAcceptableClientWireBuild(request.fields.builtNumberClient))
    {
        result.status = proto::AuthStatus::VersionMismatch;
        return result;
    }

    // ---- Account row -----------------------------------------------------------
    // WorldSocket.cpp:1325-1337.
    std::string safeAccount = request.fields.account;
    LoginDatabase.escape_string(safeAccount);

    QueryResult* queryResult =
        LoginDatabase.PQuery("SELECT "
                             "`id`, "          // 0
                             "`gmlevel`, "     // 1
                             "`sessionkey`, "  // 2
                             "`last_ip`, "     // 3
                             "`locked`, "      // 4
                             "`expansion`, "   // 5
                             "`mutetime`, "    // 6
                             "`locale` "       // 7
                             "FROM `account` WHERE `username` = '%s'",
                             safeAccount.c_str());

    if (!queryResult)
    {
        // DEBUG_LOG, not sLog.outError: attacker-driven volume on an unauthenticated
        // path (WorldSocket.cpp:1342-1345's own rationale).
        DEBUG_LOG("WorldGateway::LookupAccount: rejecting auth (unknown account).");
        result.status = proto::AuthStatus::UnknownAccount;
        return result;
    }

    const Field* fields = queryResult->Fetch();

    std::shared_ptr<AccountRow> row = std::make_shared<AccountRow>();
    row->id = fields[0].GetUInt32();

    // Clamp rather than trust: a bad gmlevel in the database must not hand out more
    // authority than the server has levels for. WorldSocket.cpp:1365-1369.
    uint32 security = fields[1].GetUInt16();
    if (security > SEC_ADMINISTRATOR)
    {
        security = SEC_ADMINISTRATOR;
    }
    row->security = AccountTypes(security);

    // ---- Ladder step 1 (spec 5.1): the sessionkey guard. WorldSocket.cpp:1371-1402.
    // VALIDATE STRUCTURALLY, never branch on a specific length -- see MopAuthKey.h
    // for why (realmd's AsHexStr() drops leading zero bytes; 78/76/74 hex chars are
    // all NORMAL). SessionKeyFromHex owns the whole hex -> raw-40 chain.
    const char* const sessionKeyHex = fields[2].GetString();
    if (!MopAuth::SessionKeyFromHex(sessionKeyHex, row->sessionKey))
    {
        delete queryResult;
        // Payload-free: the account id only, never the key text.
        BASIC_LOG("WorldGateway::LookupAccount: rejecting auth (account %u has a "
                  "NULL, empty or malformed sessionkey; realmd may not have "
                  "authenticated this account yet).", row->id);
        result.status = proto::AuthStatus::SessionExpired;
        return result;
    }

    const std::string lastIp = fields[3].GetString();
    const bool        locked = fields[4].GetUInt8() == 1;

    row->expansion = uint8(std::min<uint32>(sWorld.getConfig(CONFIG_UINT32_EXPANSION),
                                            fields[5].GetUInt8()));
    row->muteTime = time_t(fields[6].GetUInt64());

    const uint8 rawLocale = fields[7].GetUInt8();
    row->locale = rawLocale >= MAX_LOCALE ? LOCALE_enUS : LocaleConstant(rawLocale);

    delete queryResult;

    // ---- IP lock ---------------------------------------------------------------
    // WorldSocket.cpp:1353-1362 ("re-check ip locking, same check as in realmd").
    if (locked && lastIp != request.peerAddress)
    {
        BASIC_LOG("WorldGateway::LookupAccount: rejecting auth (account IP differs).");
        result.status = proto::AuthStatus::Failed;
        return result;
    }

    // ---- Bans --------------------------------------------------------------
    // WorldSocket.cpp:1415-1429 ("re-check account ban, same check as in realmd").
    QueryResult* banResult =
        LoginDatabase.PQuery("SELECT 1 FROM `account_banned` WHERE `id` = %u AND `active` = 1 "
                             "AND (`unbandate` > UNIX_TIMESTAMP() OR `unbandate` = `bandate`) "
                             "UNION "
                             "SELECT 1 FROM `ip_banned` WHERE (`unbandate` = `bandate` OR "
                             "`unbandate` > UNIX_TIMESTAMP()) AND `ip` = '%s'",
                             row->id, request.peerAddress.c_str());

    if (banResult)
    {
        delete banResult;
        BASIC_LOG("WorldGateway::LookupAccount: rejecting auth (account banned).");
        result.status = proto::AuthStatus::Banned;
        return result;
    }

    // ---- Security floor (server closed to ordinary players) --------------------
    // WorldSocket.cpp:1431-1438.
    const AccountTypes allowed = sWorld.GetPlayerSecurityLimit();
    if (allowed > SEC_PLAYER && row->security < allowed)
    {
        BASIC_LOG("WorldGateway::LookupAccount: user tries to login but security "
                  "level is not enough");
        result.status = proto::AuthStatus::Unavailable;
        return result;
    }

    // Warden is intentionally absent during this schema-first transition. A
    // later implementation must consume the canonical raw-40 K without a
    // BigNumber round trip, never log key material, and deliberately order its
    // first server packet after authentication. This path deliberately does not
    // select or gate on `account.os`; removal must not change that auth policy.

    result.status = proto::AuthStatus::Ok;
    std::memcpy(result.sessionKey, row->sessionKey, sizeof(result.sessionKey));
    result.context = row;
    return result;
}

proto::SessionId WorldGateway::Attach(const proto::AuthRequest& request,
                                      const std::shared_ptr<proto::IClientLink>& link,
                                      const std::shared_ptr<proto::AuthContext>& context,
                                      proto::IWorldGateway::AuthCommit commit,
                                      void* commitContext)
{
    EnsureDbThreadRegistered();

    AccountRow* row = static_cast<AccountRow*>(context.get());
    if (row == nullptr || commit == nullptr)
    {
        return proto::INVALID_SESSION_ID;
    }

    // Phase 3 performs NO writes to the `account` row on the auth path (spec 2,
    // 6.7; WorldSocket.cpp:1493-1497). Unlike MangosThree's WorldGateway, this is
    // NOT a last_ip UPDATE that was simply never ported -- M4 removed it
    // DELIBERATELY, because realmd already rewrites last_ip on every
    // authentication. Do not reintroduce it.

    WorldSession* session = new WorldSession(row->id, link, row->security,
                                             row->expansion, row->muteTime,
                                             row->locale, row->sessionKey);

    // Every fallible step here runs BEFORE @p commit is ever reachable -- DB loads,
    // addon inflate -- exactly mirroring WorldSocket.cpp:1504-1544's ordering. A
    // throw here must never leave @p commit callable, so each catch tears the
    // session down via AbandonUnpublishedLink() (which does NOT close the link;
    // the connection must survive to deliver the resulting rejection) and returns
    // INVALID_SESSION_ID without touching the crypt.
    try
    {
        session->LoadGlobalAccountData();
        session->LoadTutorialsData();

        ByteBuffer addonBuffer;
        if (!request.fields.addonData.empty())
        {
            addonBuffer.append(request.fields.addonData.data(),
                               request.fields.addonData.size());
        }
        session->ReadAddonsInfo(addonBuffer);
    }
    catch (ByteBufferException const&)
    {
        sLog.outError("WorldGateway::Attach: rejecting auth (malformed addon data "
                      "for account %u).", row->id);
        session->AbandonUnpublishedLink();
        delete session;
        return proto::INVALID_SESSION_ID;
    }
    catch (std::exception const& e)
    {
        sLog.outError("WorldGateway::Attach: rejecting auth (session load failed "
                      "for account %u: %s).", row->id, e.what());
        session->AbandonUnpublishedLink();
        delete session;
        return proto::INVALID_SESSION_ID;
    }
    catch (...)
    {
        sLog.outError("WorldGateway::Attach: rejecting auth (unknown session-load "
                      "failure for account %u).", row->id);
        session->AbandonUnpublishedLink();
        delete session;
        return proto::INVALID_SESSION_ID;
    }

    // ================= AUTH PUBLICATION TRANSACTION =================
    // Everything fallible has already run: crypt Prepare() (proto side, before this
    // function), the session allocation, the DB loads, the addon inflate.
    // World::AddSession now performs the ONLY remaining fallible operation --
    // queue insertion -- while its lock is held, and invokes @p commit (which
    // proto::ClientConnection defined to Activate() the crypt and set CONN_AUTHED)
    // only once that succeeds, STILL under the lock -- so the world thread cannot
    // observe the queued session until crypt and connection state are committed.
    // Unlock is the publication point. See WorldSocket.cpp:1551-1576 (deleted) for
    // the ordering this preserves, and IWorldGateway::Attach()'s doc comment for
    // why this call must forward @p commit/@p commitContext verbatim rather than
    // interpret them.
    proto::SessionId id;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        id = m_nextId;

        if (!sWorld.AddSession(session, commit, commitContext))
        {
            session->AbandonUnpublishedLink();
            delete session;
            return proto::INVALID_SESSION_ID;
        }

        m_sessions[id] = session;
        ++m_nextId;
    }

    return id;
}

void WorldGateway::Deliver(proto::SessionId session, WorldPacket&& packet)
{
    // WorldSocket.cpp's ProcessIncoming never reached the default (queue-to-session)
    // branch for an opcode >= NUM_MSG_TYPES; Phase 1a's bounded LookupClientOpcode()
    // already drops unregistered opcodes gracefully downstream of Deliver(), so no
    // additional gate is added here (contrast MangosThree's WorldGateway, whose
    // WorldSocket.cpp DID hard-reject >= NUM_MSG_TYPES at the transport and had to
    // re-home that check here -- M4 never had it to begin with; see Opcodes.cpp's
    // own bounded-lookup rationale).
    std::lock_guard<std::mutex> lock(m_lock);

    if (WorldSession* target = Find(session))
    {
        // Packet dumps stay in game code so the transport remains opcode-agnostic.
        // The gateway session id replaces the old ACE socket handle as the
        // connection-scoped identifier for incoming traffic.
        if (sLog.IsPacketLoggingEnabled())
        {
            sLog.outWorldPacketDump(session, packet.GetOpcode(),
                                    LookupOpcodeName(DIR_CLIENT, packet.GetOpcode()), &packet, true);
        }

        // QueuePacket takes ownership; the world thread drains and frees it.
        target->QueuePacket(new WorldPacket(std::move(packet)));
    }
}

bool WorldGateway::OnPing(proto::SessionId session, uint32 latency, uint32 fastPingRun)
{
    WorldSession* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        target = Find(session);
    }

    // A ping before authenticating is not something a real client does.
    // WorldSocket.cpp:1638-1645.
    if (target == nullptr)
    {
        sLog.outError("WorldGateway::OnPing: peer sent CMSG_PING before authenticating.");
        return false;
    }

    target->SetLatency(latency);
    target->ResetClientTimeDelay();

    // Overspeed policy: a configured maximum of zero disables the check entirely
    // (WorldSocket.cpp:1609-1613's historical meaning of the option).
    const uint32 maxCount = sWorld.getConfig(CONFIG_UINT32_MAX_OVERSPEED_PINGS);
    if (maxCount == 0 || fastPingRun <= maxCount)
    {
        return true;
    }

    // Staff accounts are exempt (WorldSocket.cpp:1615): the check exists to catch a
    // client hammering the server, and a GM tool legitimately does.
    if (target->GetSecurity() != SEC_PLAYER)
    {
        return true;
    }

    sLog.outError("WorldGateway::OnPing: kicking account %u for overspeed pings "
                  "(%u in a row).", target->GetAccountId(), fastPingRun);
    return false;
}

void WorldGateway::Detach(proto::SessionId session)
{
    WorldSession* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_lock);

        auto it = m_sessions.find(session);
        if (it == m_sessions.end())
        {
            return;
        }
        target = it->second;
        m_sessions.erase(it);
    }

    // The session is not deleted here. It still holds a player that has to be
    // saved and taken off the map, which only the world thread may do; the world
    // reaps it on a later Update() tick (WorldSession::Update() drops the world's
    // last reference once its link IsClosed()). The link it holds is already
    // disarmed, so any packet it tries to send on the way out is discarded rather
    // than crashing.
    if (target != nullptr)
    {
        target->KickPlayer();
    }
}

void WorldGateway::BuildAuthErrorResponse(proto::AuthStatus status, WorldPacket& out)
{
    // Re-derive the game-side AUTH_* code from proto's wire-value enum -- both are
    // defined as the literal SharedDefines.h values (IWorldGateway.h documents the
    // source line for each), so this is a value cast, not a translation table.
    MopAuth::BuildAuthResponseError(out, uint8(status));
}

void WorldGateway::OnPacketReceived(WorldPacket& packet, proto::SessionId session)
{
    // WorldSocket.cpp:1119-1137: fire-and-forget Eluna notification for
    // CMSG_KEEP_ALIVE and CMSG_LOG_DISCONNECT, no veto. The null WorldSession* is
    // reachable (a KEEP_ALIVE technically cannot arrive pre-auth per the handshake
    // legality table, but Eluna::OnPacketReceive already null-checks its session
    // argument, matching what the deleted code passed).
#ifdef ENABLE_ELUNA
    if (Eluna* e = sWorld.GetEluna())
    {
        WorldSession* target = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            target = Find(session);
        }
        e->OnPacketReceive(target, packet);
    }
#else
    (void)packet;
    (void)session;
#endif
}

WorldSession* WorldGateway::Find(proto::SessionId session) const
{
    auto it = m_sessions.find(session);
    return it == m_sessions.end() ? nullptr : it->second;
}
