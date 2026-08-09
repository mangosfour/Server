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

#include <sstream>
#include <vector>

#include "DBCEnums.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "GameEventMgr.h"
#include "Item.h"
#include "Mail.h"
#include "Group.h"
#include "LFGMgr.h"
#include "LFGStatePolicy.h"
#include "Object.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"

/**
 * @file LFGMgrProposal.cpp
 * @brief Cohesion split of LFGMgr.cpp -- role check, dungeon proposal and in-dungeon flow: PerformRoleCheck, proposal send/update/decline, dungeon group create, teleport, boss-kill, kick/vote and LFG packet senders. Same LFGMgr class; CMake file(GLOB) picks this file up automatically.
 */

struct LFGMgr::DungeonGroupPlan
{
    LfgDungeonsEntry const* dungeon = NULL;
    Group* group = NULL;
    ObjectGuid leaderGuid;
    LFGStatePolicy::DifficultyPlan difficultyPlan = { false, false, 0 };
    uint32 finalSize = 0;
    uint32 capacity = 0;
    bool createNewGroup = true;
    bool convertToRaid = false;
};

// called each time a player selects their role
void LFGMgr::PerformRoleCheck(Player* pPlayer, Group* pGroup, uint8 roles)
{
    if (!pGroup)
    {
        return;
    }

    ObjectGuid groupGuid = pGroup->GetObjectGuid();
    ObjectGuid plrGuid = pPlayer ? pPlayer->GetObjectGuid() : ObjectGuid();

    roleCheckMap::iterator it = m_roleCheckMap.find(groupGuid);
    if (it == m_roleCheckMap.end())
    {
        return; // no role check map found
    }

    // A REFERENCE, not a copy. This was `LFGRoleCheck roleCheck = it->second;`, so
    // every `roleCheck.currentRoles[plrGuid] = roles` below landed in a temporary that
    // was discarded on return -- no member's answer was ever recorded, and a party of
    // two or more could never complete its role check no matter what anyone clicked.
    LFGRoleCheck& roleCheck = it->second;

    roleMap::iterator member = roleCheck.currentRoles.end();
    if (pPlayer)
    {
        member = roleCheck.currentRoles.find(plrGuid);
        if (!LFGStatePolicy::CanSubmitRole(true, member != roleCheck.currentRoles.end()))
        {
            return;
        }
    }

    bool roleChosen = roleCheck.state != LFG_ROLECHECK_DEFAULT && plrGuid;

    if (!plrGuid)
    {
        roleCheck.state = LFG_ROLECHECK_ABORTED;  // aborted if anyone cancels during role check
    }
    else if (!(roles & (PLAYER_ROLE_TANK | PLAYER_ROLE_HEALER | PLAYER_ROLE_DAMAGE)))
    {
        // The mask must name at least one real role. Testing `roles < PLAYER_ROLE_TANK`
        // only rejected 0 and a bare LEADER bit; it accepted any unknown high bit as a
        // valid answer, which then matched no role anywhere downstream.
        roleCheck.state = LFG_ROLECHECK_NO_ROLE;
    }
    else
    {
        member->second = roles;

        bool allRolesChosen = true;
        for (roleMap::iterator rItr = roleCheck.currentRoles.begin(); rItr != roleCheck.currentRoles.end(); ++rItr)
        {
            if (rItr->second == PLAYER_ROLE_NONE)
            {
                allRolesChosen = false;
                break;
            }
        }

        if (allRolesChosen) // meaning that everyone confirmed their roles
        {
            roleCheck.state = ValidateGroupRoles(roleCheck.currentRoles, roleCheck.dungeonList) ? LFG_ROLECHECK_FINISHED : LFG_ROLECHECK_MISSING_ROLE;
        }
    }

    std::set<uint32> dungeonBuff;
    if (roleCheck.randomDungeonID)
    {
        dungeonBuff.insert(roleCheck.randomDungeonID);
    }
    else
    {
        dungeonBuff = roleCheck.dungeonList;
    }

    partyForbidden nullForbidden;

    for (roleMap::iterator itr = roleCheck.currentRoles.begin(); itr != roleCheck.currentRoles.end(); ++itr)
    {
        ObjectGuid guidBuff = itr->first;
        if (roleChosen)
        {
            SendRoleChosen(guidBuff, plrGuid, roles); // send SMSG_LFG_ROLE_CHOSEN to each player
        }

        // send SMSG_LFG_ROLE_CHECK_UPDATE
        SendRoleCheckUpdate(guidBuff, roleCheck);

        switch (roleCheck.state)
        {
            case LFG_ROLECHECK_INITIALITING:
                continue;
            case LFG_ROLECHECK_FINISHED:
                // set current plr's state to queued. then set their role in that struct
                // then send lfgupdate packet with UPDATETYPE_ADDED_TO_QUEUE
                SetPlayerState(guidBuff, LFG_STATE_QUEUED);
                SetPlayerUpdateType(guidBuff, LFG_UPDATE_ADDED_TO_QUEUE);
                SendLfgUpdate(guidBuff, GetPlayerStatus(guidBuff), true);
                break;
            default:
                if (roleCheck.leaderGuidRaw == guidBuff.GetRawValue())
                {
                    SendLfgJoinResult(guidBuff, ERR_LFG_ROLE_CHECK_FAILED, uint8(roleCheck.state), nullForbidden);
                }
                SetPlayerUpdateType(guidBuff, LFG_UPDATE_ROLECHECK_FAILED);
                SendLfgUpdate(guidBuff, GetPlayerStatus(guidBuff), true);
                break;
        }
    }

    if (roleCheck.state == LFG_ROLECHECK_FINISHED)
    {
        LFGPlayers* queueInfo = GetPlayerOrPartyData(groupGuid);
        if (!queueInfo)
        {
            m_roleCheckMap.erase(groupGuid);
            return;
        }

        queueInfo->currentState = LFG_STATE_QUEUED;
        queueInfo->currentRoles = roleCheck.currentRoles;
        queueInfo->joinedTime   = time(NULL);

        m_playerData[groupGuid] = *queueInfo;

        // Retail sends the successful join result to the leader after the role check
        // completes. BeginTicket ran before the opening status, so this quotes the same
        // retained group requester, id and time as every other body for the queue.
        SendLfgJoinResult(ObjectGuid(roleCheck.leaderGuidRaw), ERR_LFG_OK,
                          LFG_JOIN_DETAIL_NONE, nullForbidden);

        AddToQueue(groupGuid);

        // The check is resolved; leaving it in the map makes RemoveOldRoleChecks expire
        // an already-queued party and tear its queue entry back down.
        m_roleCheckMap.erase(groupGuid);
    }
    else if (roleCheck.state != LFG_ROLECHECK_INITIALITING)
    {
        // todo: add players back to individual queues if applicable
        roleCheck.state = LFG_ROLECHECK_NO_ROLE;

        for (roleMap::iterator roleMapItr = roleCheck.currentRoles.begin(); roleMapItr != roleCheck.currentRoles.end(); ++roleMapItr)
        {
            ObjectGuid plrGuid = roleMapItr->first;

            SetPlayerState(plrGuid, LFG_STATE_NONE);

            SendRoleCheckUpdate(plrGuid, roleCheck);                 // role check failed
            SendLfgUpdate(plrGuid, GetPlayerStatus(plrGuid), true);  // not in lfg system anymore
        }
        m_roleCheckMap.erase(groupGuid);
    }
}

bool LFGMgr::ValidateGroupRoles(roleMap groupMap, std::set<uint32> const& dungeonList)
{
    if (groupMap.empty()) // sanity check
    {
        return false;
    }

    // This used to assert only that every member had picked exactly one of tank/healer/
    // damage, which failed two ways at once: a member who ticked tank AND damage matched
    // no case and sank the whole party's role check, while a party of five tanks passed
    // it and then jammed the queue because no dungeon has five tank slots.
    //
    // Asking whether the party can be assigned to the dungeon's actual role counts covers
    // both, and covers scenarios and raid finder, whose compositions are not 1/1/3.
    return RolesAreValidForDungeons(groupMap, dungeonList);
}

/**
 * @brief The dungeon a proposal should actually put the group into.
 *
 * A normal queue names a real dungeon and this returns it unchanged. A RANDOM queue names a
 * category, and a category is not a place: all 12 TypeID 6 rows in LfgDungeons.dbc carry MapID
 * 0 or 0xFFFFFFFF. Proposing one sent the group to a plain teleport failure, or -- for the four
 * carrying 0 -- silently to Eastern Kingdoms.
 *
 * The category row is excluded from its own expansion. Group_ID 33, behind Random Hour of
 * Twilight Heroic, has exactly ONE member and that member is the category row itself, so
 * without the exclusion that random would still propose an unrunnable row.
 *
 * Untranslatable tiers are excluded for the same reason JoinLFG refuses them at admission: a
 * row whose DifficultyID has no internal mode cannot be entered at the tier it claims.
 *
 * @return a concrete dungeon id, or 0 when nothing behind the selection is runnable.
 */
static uint32 PickConcreteDungeon(uint32 queuedDungeonId, std::set<uint32> const& candidates)
{
    LfgDungeonsEntry const* queued = sLfgDungeonsStore.LookupEntry(queuedDungeonId);
    if (!queued)
    {
        return 0;
    }

    if (queued->TypeID != LFG_TYPE_RANDOM_DUNGEON)
    {
        return queuedDungeonId;                             // already a real dungeon
    }

    // Collect every runnable member, then pick one at random.
    //
    // This used to return the first match. candidates is a std::set<uint32>, which is
    // ordered ascending, so "random dungeon" deterministically produced the LOWEST
    // dungeon id in the category every single time -- the same instance on every queue.
    std::vector<uint32> runnable;
    for (std::set<uint32>::const_iterator it = candidates.begin(); it != candidates.end(); ++it)
    {
        if (*it == queuedDungeonId)
        {
            continue;                                       // the category cannot host itself
        }

        LfgDungeonsEntry const* candidate = sLfgDungeonsStore.LookupEntry(*it);
        if (!candidate || candidate->TypeID == LFG_TYPE_RANDOM_DUNGEON)
        {
            continue;
        }

        if (ToInternalDifficulty(candidate->DifficultyID) < 0)
        {
            continue;
        }

        runnable.push_back(candidate->ID);
    }

    if (runnable.empty())
    {
        return 0;
    }

    return runnable[urand(0, uint32(runnable.size()) - 1)];
}

//todo: remove from queue, update queue average settings
bool LFGMgr::SendDungeonProposal(ObjectGuid queueGuid, LFGPlayers* lfgGroup)
{
    if (!lfgGroup || lfgGroup->dungeonList.empty())
    {
        return false;
    }

    uint32 const queuedDungeonId = lfgGroup->randomDungeonID
        ? lfgGroup->randomDungeonID : *lfgGroup->dungeonList.begin();

    // note: group create function's parameters are leader guid & leader name
    LFGProposal newProposal;
    newProposal.state = LFG_PROPOSAL_INITIATING;
    newProposal.encounters = 0; // todo: check if group has already started a dungeon and are looking for another plr
    newProposal.currentRoles = lfgGroup->currentRoles;
    newProposal.dungeonID = queuedDungeonId;

    // The dungeon the group is actually put into.
    //
    // For a normal queue that is the queued row. For a RANDOM one it cannot be: every TypeID 6
    // row in LfgDungeons.dbc carries MapID 0 or 0xFFFFFFFF, so proposing the category itself
    // teleports the group nowhere -- 4 of the 12 silently to Eastern Kingdoms and the other 8 to
    // a plain failure. A concrete member of the expansion is chosen instead, while dungeonID
    // keeps naming the random entry for the proposal packet and the reward lookup.
    newProposal.concreteDungeonID = PickConcreteDungeon(queuedDungeonId, lfgGroup->candidateDungeons);
    if (!LFGStatePolicy::CanStartProposal(newProposal.concreteDungeonID))
    {
        // Nothing runnable behind the category. Do not build a proposal that cannot complete:
        // the group would be formed, torn out of its previous groups and then left standing.
        DEBUG_LOG("LFG SendDungeonProposal: random dungeon %u expanded to no runnable "
                  "member; refusing to propose.", queuedDungeonId);
        return false;
    }

    newProposal.isNew = true;
    newProposal.joinedQueue = lfgGroup->joinedTime;
    newProposal.createdTime = time(NULL);

    // Which queue entry this came from, so a failure can put the survivors back. Passed
    // in rather than recovered by scanning m_playerData for a matching address: the
    // caller already knows the key, and identifying a map entry by the address of its
    // value is the kind of thing that quietly stops working the first time anyone copies
    // the struct.
    newProposal.queueGuid = queueGuid;

    {
        std::ostringstream avail;
        for (std::set<uint32>::const_iterator it = lfgGroup->dungeonList.begin();
             it != lfgGroup->dungeonList.end(); ++it)
        {
            avail << (it == lfgGroup->dungeonList.begin() ? "" : ",") << *it;
        }
        DEBUG_LOG("LFG SendDungeonProposal: entry dungeons={%s} -> chose %u (entry 0x%08X)",
                  avail.str().c_str(), newProposal.dungeonID, GetDungeonEntry(newProposal.dungeonID));
    }

    // Is this proposal CONTINUING an existing run, or forming a new group?
    //
    // A backfill is not a distinct protocol -- the client answers the offer with an
    // ordinary CMSG_LFG_JOIN carrying the same dungeon slot -- so "this is a backfill" is
    // server-side state only: one of the members belongs to a finder group whose run is
    // still live.
    uint32 liveRuns = 0;
    ObjectGuid const continueGuid = ResolveContinuingGroup(lfgGroup->currentRoles, liveRuns);
    if (liveRuns > 1)
    {
        // Two live runs in one proposal has no sane resolution -- whichever group were
        // reused, the other's players would be torn out of a dungeon they are standing in.
        // Every fork treats this as hard-incompatible; RoleMapsAreCompatible now refuses
        // the merge, so reaching here means something upstream slipped.
        sLog.outError("LFG SendDungeonProposal: %u live LFG runs in one proposal; refusing. "
                      "The queue entry is left intact.", liveRuns);
        return false;
    }

    bool const continuing = !continueGuid.IsEmpty();
    Group* continueGroup = continuing ? sObjectMgr.GetGroupById(continueGuid.GetCounter()) : NULL;
    if (continuing && !continueGroup)
    {
        sLog.outError("LFG SendDungeonProposal: continuing group %s vanished; refusing.",
                      continueGuid.GetString().c_str());
        return false;
    }

    if (continuing)
    {
        // PIN the dungeon to the one the group is standing in. Without this a random
        // category re-rolls PickConcreteDungeon and proposes a DIFFERENT dungeon from the
        // one the run is in -- the fork-unanimous isContinue pin.
        if (LFGGroupStatus const* runStatus = GetGroupStatus(continueGuid))
        {
            newProposal.concreteDungeonID = runStatus->dungeonID;
        }

        // Set ONCE, from the resolved group -- not from whichever member happens to carry
        // the leader role bit.
        newProposal.groupRawGuid = continueGuid.GetRawValue();
        newProposal.groupLeaderGuid = continueGroup->GetLeaderGuid().GetRawValue();
    }

    // isNew drives SendLfgProposalUpdate's `silent` and `inProposedGroup` flags
    // (LFGHandler.cpp:684-685, :708-709), which were dead while this was hardcoded true.
    newProposal.isNew = !continuing;

    // All refusal checks are above this point. Allocate the id only for a
    // proposal that will be stored and announced.
    newProposal.id = ++m_proposalId;

    // iterate through role map just so get everyone's guid
    for (roleMap::iterator it = lfgGroup->currentRoles.begin(); it != lfgGroup->currentRoles.end(); ++it)
    {
        ObjectGuid plrGuid = it->first;
        SetPlayerState(plrGuid, LFG_STATE_PROPOSAL);

        Player* pPlayer = sObjectAccessor.FindPlayer(plrGuid);
        if (!pPlayer)
        {
            continue;
        }

        bool continuingMember = false;
        if (Group* pGroup = pPlayer->GetGroup())
        {
            ObjectGuid grpGuid = pGroup->GetObjectGuid();

            SetPlayerUpdateType(plrGuid, LFG_UPDATE_PROPOSAL_BEGIN);

            // groupRawGuid / groupLeaderGuid are set ONCE above, from the resolved
            // continuing group. They used to be filled in here from whichever member
            // happened to carry the leader bit, which for a mixed proposal is arbitrary.
            newProposal.groups[plrGuid] = grpGuid;

            continuingMember = continuing && grpGuid == continueGuid;

            SendLfgUpdate(plrGuid, GetPlayerStatus(plrGuid), true);
        }
        else
        {
            newProposal.groups[plrGuid] = ObjectGuid();

            //SetPlayerUpdateType(plrGuid, LFG_UPDATE_GROUP_FOUND);
            //SendLfgUpdate(plrGuid, GetPlayerStatus(plrGuid), false);

            SetPlayerUpdateType(plrGuid, LFG_UPDATE_PROPOSAL_BEGIN);
            SendLfgUpdate(plrGuid, GetPlayerStatus(plrGuid), false);
        }

        // Assign exactly once. Continuing members receive a silent proposal and
        // therefore must already agree; newly matched players get the popup and
        // remain pending until they answer it.
        newProposal.answers[plrGuid] =
            LFGStatePolicy::InitialProposalAnswer(continuingMember) ==
                LFGStatePolicy::ProposalAnswerDecision::Agree
                    ? LFG_ANSWER_AGREE
                    : LFG_ANSWER_PENDING;
    }

    // Sent only once the proposal is COMPLETE.
    //
    // This used to sit inside the loop above, which is still filling `groups` and
    // `answers`. Since the packet serialises those maps, every recipient except the last
    // one received an opening proposal that omitted the members added after them -- so
    // the ready popup showed an incomplete group until somebody answered.
    for (roleMap::const_iterator it = lfgGroup->currentRoles.begin();
         it != lfgGroup->currentRoles.end(); ++it)
    {
        if (Player* pMember = sObjectAccessor.FindPlayer(it->first))
        {
            pMember->GetSession()->SendLfgProposalUpdate(newProposal);
        }
    }

    // No SetAsLfgGroup here.
    //
    // It existed to mark a PREMADE that was about to become a finder group. A continuing
    // run is already flagged -- GROUPTYPE_LFD is set when the finder first formed it and
    // is never cleared -- and a brand new group is flagged by CreateDungeonGroup when it
    // builds one. Marking a plain world premade here was the behaviour that made an
    // ordinary party start reporting itself as a dungeon-finder group.

    // also save the proposal
    m_proposalMap[newProposal.id] = newProposal;
    return true;
}

bool LFGMgr::IsLiveLfgRun(Group* pGroup)
{
    if (!pGroup || !pGroup->isLFGGroup())
    {
        return false;
    }

    LFGGroupStatus const* status = GetGroupStatus(pGroup->GetObjectGuid());
    return status && status->state != LFG_STATE_FINISHED_DUNGEON;
}

ObjectGuid LFGMgr::ResolveContinuingGroup(roleMap const& members, uint32& outLiveRuns)
{
    std::set<ObjectGuid> runs;

    for (roleMap::const_iterator it = members.begin(); it != members.end(); ++it)
    {
        Player* pPlayer = sObjectAccessor.FindPlayer(it->first);
        if (!pPlayer)
        {
            continue;       // logged out or mid-teleport; the caller skips them too
        }

        if (Group* pGroup = pPlayer->GetGroup())
        {
            if (IsLiveLfgRun(pGroup))
            {
                runs.insert(pGroup->GetObjectGuid());
            }
        }
    }

    outLiveRuns = uint32(runs.size());
    return runs.size() == 1 ? *runs.begin() : ObjectGuid();
}


// From a CMSG_LFG_PROPOSAL_RESPONSE call
/// A decline cancels the proposal, but it does NOT eject everyone.
///
/// The client states all three outcomes plainly:
///   ERR_LFG_PROPOSAL_FAILED         "Someone has declined the invite. You have been
///                                    returned to the front of the queue."
///   ERR_LFG_PROPOSAL_DECLINED_SELF  "You have been removed from the queue because you
///                                    did not accept the invitation."
///   ERR_LFG_PROPOSAL_DECLINED_PARTY "...because someone in your party did not accept."
///
/// So the decliner leaves, their premade leaves with them, and everyone else is
/// requeued. An earlier version of this removed everyone, which is why the queue entry
/// is now kept alive for the lifetime of the proposal -- there has to be something left
/// to put people back into.
void LFGMgr::DeclineProposal(ObjectGuid plrGuid, LFGProposal* proposal)
{
    std::set<ObjectGuid> culprits;
    culprits.insert(plrGuid);

    // A premade is removed alongside the member who declined for it.
    playerGroupMap::const_iterator declinerGroup = proposal->groups.find(plrGuid);
    if (declinerGroup != proposal->groups.end() && declinerGroup->second)
    {
        for (playerGroupMap::const_iterator it = proposal->groups.begin();
             it != proposal->groups.end(); ++it)
        {
            if (it->second == declinerGroup->second)
            {
                culprits.insert(it->first);
            }
        }
    }

    CancelProposal(proposal->id, culprits);
}

bool LFGMgr::PrepareDungeonGroup(LFGProposal* proposal, DungeonGroupPlan& plan,
                                 std::set<ObjectGuid>& culprits)
{
    if (!proposal)
    {
        return false;
    }

    bool validRoster = !proposal->currentRoles.empty() &&
        proposal->currentRoles.size() == proposal->groups.size() &&
        proposal->currentRoles.size() == proposal->answers.size();

    for (roleMap::const_iterator it = proposal->currentRoles.begin();
         it != proposal->currentRoles.end(); ++it)
    {
        if (proposal->groups.find(it->first) == proposal->groups.end())
        {
            validRoster = false;
        }

        proposalAnswerMap::const_iterator answer = proposal->answers.find(it->first);
        if (answer == proposal->answers.end() || answer->second != LFG_ANSWER_AGREE)
        {
            validRoster = false;
        }

        if (!sObjectAccessor.FindPlayer(it->first))
        {
            culprits.insert(it->first);
        }
    }

    uint32 const runDungeonId = proposal->concreteDungeonID
        ? proposal->concreteDungeonID : proposal->dungeonID;
    plan.dungeon = sLfgDungeonsStore.LookupEntry(runDungeonId);
    if (plan.dungeon)
    {
        bool const isRaid = plan.dungeon->TypeID == LFG_TYPE_RAID;
        plan.difficultyPlan = LFGStatePolicy::ResolveDifficulty(
            ToInternalDifficulty(plan.dungeon->DifficultyID), isRaid,
            uint8(MAX_DUNGEON_DIFFICULTY), uint8(MAX_RAID_DIFFICULTY));

        uint32 const requestedSize = plan.dungeon->Count_tank +
            plan.dungeon->Count_healer + plan.dungeon->Count_damage;
        plan.convertToRaid = requestedSize > MAX_GROUP_SIZE;
        plan.capacity = plan.convertToRaid ? MAX_RAID_SIZE : MAX_GROUP_SIZE;
    }

    if (proposal->groupRawGuid)
    {
        plan.group = sObjectMgr.GetGroupById(
            ObjectGuid(proposal->groupRawGuid).GetCounter());
        if (!IsLiveLfgRun(plan.group))
        {
            plan.group = NULL;
        }
    }

    plan.createNewGroup = plan.group == NULL;
    if (plan.createNewGroup)
    {
        // `.debug dungeon` keeps the game master in control of the generated group.
        if (m_debugMode != LFG_DEBUG_OFF)
        {
            for (roleMap::const_iterator it = proposal->currentRoles.begin();
                 it != proposal->currentRoles.end(); ++it)
            {
                Player* player = sObjectAccessor.FindPlayer(it->first);
                if (player && player->GetSession() &&
                    player->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
                {
                    plan.leaderGuid = it->first;
                    break;
                }
            }
        }

        for (roleMap::const_iterator it = proposal->currentRoles.begin();
             !plan.leaderGuid && it != proposal->currentRoles.end(); ++it)
        {
            if ((it->second & PLAYER_ROLE_LEADER) &&
                sObjectAccessor.FindPlayer(it->first))
            {
                plan.leaderGuid = it->first;
            }
        }

        for (roleMap::const_iterator it = proposal->currentRoles.begin();
             !plan.leaderGuid && it != proposal->currentRoles.end(); ++it)
        {
            if (sObjectAccessor.FindPlayer(it->first))
            {
                plan.leaderGuid = it->first;
            }
        }
    }

    plan.finalSize = plan.group ? plan.group->GetMembersCount() : 0;
    for (roleMap::const_iterator it = proposal->currentRoles.begin();
         it != proposal->currentRoles.end(); ++it)
    {
        if (!plan.group || !plan.group->IsMember(it->first))
        {
            ++plan.finalSize;
        }
    }

    bool const allMembersOnline = validRoster && culprits.empty();
    bool const hasGroupOrLeader = plan.group != NULL || bool(plan.leaderGuid);
    if (!LFGStatePolicy::CanCompleteProposal(
            allMembersOnline, plan.dungeon != NULL, plan.difficultyPlan.valid,
            hasGroupOrLeader, plan.finalSize, plan.capacity))
    {
        if (!validRoster)
        {
            sLog.outError("LFG proposal %u has inconsistent role, answer or group rosters; "
                          "cancelling before success.", proposal->id);
        }
        else if (!culprits.empty())
        {
            sLog.outError("LFG proposal %u lost %u accepted member(s) before success.",
                          proposal->id, uint32(culprits.size()));
        }
        else if (!plan.dungeon)
        {
            sLog.outError("LFG proposal %u names unknown concrete dungeon %u.",
                          proposal->id, runDungeonId);
        }
        else if (!plan.difficultyPlan.valid)
        {
            sLog.outError("LFG proposal %u dungeon %u has unsupported client "
                          "DifficultyID %u.", proposal->id, plan.dungeon->ID,
                          plan.dungeon->DifficultyID);
        }
        else if (!hasGroupOrLeader)
        {
            sLog.outError("LFG proposal %u has no live continuation group or online leader.",
                          proposal->id);
        }
        else
        {
            sLog.outError("LFG proposal %u would create %u members over capacity %u.",
                          proposal->id, plan.finalSize, plan.capacity);
        }
        return false;
    }

    return true;
}

void LFGMgr::ProposalUpdate(uint32 proposalID, ObjectGuid plrGuid, bool accepted)
{
    //note: create a group here if it doesn't exist and everyone accepted proposal
    LFGProposal* proposal = GetProposalData(proposalID);

    if (!proposal)
    {
        return;
    }

    // Only a participant may answer.
    //
    // m_proposalId is a plain incrementing counter, so an id is trivially guessable.
    // Without this check, writing to proposal->answers INSERTED the caller, and a
    // `false` answer from any logged-in player cancelled a group they had nothing to do
    // with -- clearing the real members out of the queue.
    if (proposal->answers.find(plrGuid) == proposal->answers.end())
    {
        sLog.outError("LFG: %s answered proposal %u they are not part of.",
                      plrGuid.GetString().c_str(), proposalID);
        return;
    }

    bool allOkay = true; // true if everyone answered LFG_ANSWER_AGREE

    // Update answer map to given value
    LFGProposalAnswer plrAnswer = (LFGProposalAnswer)accepted;
    proposal->answers[plrGuid] = plrAnswer;

    if (plrAnswer == LFG_ANSWER_DENY)
    {
        DeclineProposal(plrGuid, proposal);
        return;
    }

    for (proposalAnswerMap::iterator it = proposal->answers.begin(); it != proposal->answers.end(); ++it)
    {
        if (it->second != LFG_ANSWER_AGREE)
        {
            allOkay = false;
        }
    }

    // if !allOkay, send proposal updates to all
    if (!allOkay)
    {
        for (proposalAnswerMap::iterator itr = proposal->answers.begin(); itr != proposal->answers.end(); ++itr)
        {
            ObjectGuid proposalPlrGuid  = itr->first;
            Player* pProposalPlayer = sObjectAccessor.FindPlayer(proposalPlrGuid);
            if (pProposalPlayer)
            {
                pProposalPlayer->GetSession()->SendLfgProposalUpdate(*proposal);
            }
        }

        return;
    }

    // Prove the whole completion before telling any client it succeeded. This is
    // intentionally before proposal->state changes and before the first SUCCESS,
    // GROUP_FOUND or LEAVE body: a later FAILED cannot reliably undo that UI flow.
    DungeonGroupPlan groupPlan;
    std::set<ObjectGuid> culprits;
    if (!PrepareDungeonGroup(proposal, groupPlan, culprits))
    {
        CancelProposal(proposal->id, culprits);
        return;
    }

    // At this point everyone's online and the group can be built.

    time_t joinedTime = time(NULL);
    bool sendProposalUpdate = proposal->state != LFG_PROPOSAL_SUCCESS;

    // now update the proposal's state to successful and inform the players
    proposal->state = LFG_PROPOSAL_SUCCESS;
    for (roleMap::iterator rItr = proposal->currentRoles.begin(); rItr != proposal->currentRoles.end(); ++rItr)
    {
        // get the player's role
        uint8 proposalPlrRole   = rItr->second;
        proposalPlrRole &= ~PLAYER_ROLE_LEADER;

        ObjectGuid proposalPlrGuid  = rItr->first;
        Player* pProposalPlayer = sObjectAccessor.FindPlayer(proposalPlrGuid);
        MANGOS_ASSERT(pProposalPlayer);

        if (sendProposalUpdate)
        {
            pProposalPlayer->GetSession()->SendLfgProposalUpdate(*proposal);
        }

        // amount of time spent in queue
        int32 timeWaited = joinedTime - proposal->joinedQueue;

        // tell the lfg system to update the average wait times on the next tick
        UpdateWaitMap(LFGRoles(proposalPlrRole), proposal->dungeonID, timeWaited);

        // send some updates to the player, depending on group status
        LFGPlayerStatus proposalPlrStatus = GetPlayerStatus(proposalPlrGuid);
        proposalPlrStatus.updateType = LFG_UPDATE_GROUP_FOUND;

        // The GROUP_FOUND body must advertise the dungeon the PROPOSAL chose, not the slot
        // the player queued for. This is what closes the client's ready popup.
        //
        // The client decides a proposal's outcome in the SMSG_LFG_UPDATE_STATUS handler, not
        // in the proposal packet. It keeps the active proposal's dungeon entry in a global,
        // and on each status body it walks that body's dungeon list looking for it:
        //
        //     mov  esi, dword_1209400   ; active proposal's dungeon, 0 when none
        //     mov  eax, [ebx+11Ch]      ; dungeon count in THIS body
        //     jbe  skip                 ; empty list -> ignore the body entirely
        //     cmp  [ecx], esi           ; the list must CONTAIN it
        //     ...
        //     mov  al, [ebx+15Ah]       ; reason: 1, 11 or 17 -> LFG_PROPOSAL_SUCCEEDED
        //
        // Miss that search and it takes no action at all -- no reset, no event -- so
        // LFGDungeonReadyPopup is never hidden and sits on screen until relog.
        //
        // Observed live 2026-08-06 18:02:07. Two GROUP_FOUND bodies for one proposal on
        // dungeon 0x01000006: the player who queued for that dungeon carried 0x01000006 and
        // was fine, while the player who queued RANDOM carried 0x06000102 -- the category
        // (type 6, id 258) -- and his popup stuck. dungeonList holds what the player queued
        // for, which for a random is the category, so every random queuer hit this.
        //
        // Scoped to this one body deliberately. The LEAVE that follows closes the QUEUE the
        // player actually joined, so it keeps advertising the slots they queued for.
        std::set<uint32> const queuedDungeons = proposalPlrStatus.dungeonList;
        proposalPlrStatus.dungeonList.clear();
        proposalPlrStatus.dungeonList.insert(proposal->dungeonID);

        // ONE retained key, used for both packets. Current grouping is only the
        // compatibility fallback; SendLfgUpdate keeps the requester originally announced
        // for this queue even if the player's group changed during the proposal.
        bool const fallbackIsGroupOwned = pProposalPlayer->GetGroup() != nullptr;

        SendLfgUpdate(proposalPlrGuid, proposalPlrStatus, fallbackIsGroupOwned);
        RemoveFromQueue(fallbackIsGroupOwned ? pProposalPlayer->GetGroup()->GetObjectGuid()
                                             : proposalPlrGuid);

        proposalPlrStatus.updateType = LFG_UPDATE_LEAVE;
        proposalPlrStatus.dungeonList = queuedDungeons;
        SendLfgUpdate(proposalPlrGuid, proposalPlrStatus, fallbackIsGroupOwned);
    }

    CreateDungeonGroup(proposal, groupPlan);

    // Tear the queue entry down. TryFormGroup deliberately KEEPS it alive for the
    // lifetime of the proposal so a decline or timeout can put the survivors back --
    // but on success nobody put it back, so it sat in m_playerData forever with
    // currentState LFG_STATE_PROPOSAL, and every member's stored status stayed at
    // LFG_STATE_PROPOSAL too. JoinLFG refuses that state, so a player who successfully
    // entered a dungeon could never queue again until relog. Observed live: five
    // rejected CMSG_LFG_JOIN attempts after one successful proposal.
    ObjectGuid const queueGuid = proposal->queueGuid;
    for (roleMap::const_iterator it = proposal->currentRoles.begin();
         it != proposal->currentRoles.end(); ++it)
    {
        // They are in the dungeon now, not queued. TeleportToDungeon sets this too for
        // the players it actually moves, but a member whose teleport was denied must not
        // be left reading LFG_STATE_PROPOSAL either.
        SetPlayerState(it->first, LFG_STATE_IN_DUNGEON);
    }

    m_queueSet.erase(queueGuid);
    m_playerData.erase(queueGuid);

    m_proposalMap.erase(proposal->id);
}

bool LFGMgr::HasLeaderFlag(roleMap const& roles)
{
    for (roleMap::const_iterator it = roles.begin(); it != roles.end(); ++it)
    {
        if (it->second & PLAYER_ROLE_LEADER)
        {
            return true;
        }
    }
    return false;
}

void LFGMgr::CreateDungeonGroup(LFGProposal* proposal, DungeonGroupPlan const& plan)
{
    MANGOS_ASSERT(proposal);
    MANGOS_ASSERT(plan.dungeon);

    // Rewritten. The previous version had four independent defects on this one path:
    //
    //  - The leader search looped over every role-flagged member calling Group::Create
    //    with no break, so two merged premades carrying two LEADER bits ran Create
    //    twice on one object. Each call does its own GenerateGroupLowGuid plus an
    //    INSERT INTO groups in its own transaction, orphaning the first group id and
    //    stranding that id's group_member rows.
    //  - If a leader bit was set but every leader-flagged player was offline, Create
    //    never ran while AddMember still did -- building a group with id 0 and an empty
    //    leader guid, which was then inserted into m_groupSet.
    //  - The existing-group branch called no AddMember at all, so the commonest LFD
    //    composition (one premade plus solo queuers) dequeued the solos, told them a
    //    group was found, and never put them in one.
    //  - Nothing registered the group with ObjectMgr, so GetGroupById could not find
    //    it, it leaked at shutdown, and the boot path called RemoveGroup on a group
    //    that had never been added.
    //
    // Everything which can be decided from the proposal, DBC and live group state was
    // resolved before the first success packet. This function only commits that plan.
    LfgDungeonsEntry const* dungeon = plan.dungeon;
    LFGStatePolicy::DifficultyPlan const& difficultyPlan = plan.difficultyPlan;
    Group* pGroup = plan.group;
    bool const createdNewGroup = plan.createNewGroup;

    if (createdNewGroup)
    {
        // A stale continuation snapshot was deliberately downgraded during preflight.
        // Clear its identity so the status merge below creates a fresh run record.
        proposal->groupRawGuid = 0;
        proposal->groupLeaderGuid = 0;

        Player* pLeader = sObjectAccessor.FindPlayer(plan.leaderGuid);
        MANGOS_ASSERT(pLeader);
        if (!pLeader)
        {
            return; // defensive release-build guard; preflight proved this player live
        }

        // Detach from any prior group BEFORE creating, so Create does not run against a
        // player their old group still lists.
        //
        // Player::RemoveFromGroup, not Group::RemoveMember directly: pulling a member
        // out of a two-man group makes RemoveMember Disband it, and Disband does not
        // delete the object or unregister it. The helper is the codebase's own
        // convention for exactly this and handles RemoveGroup plus delete.
        for (roleMap::const_iterator it = proposal->currentRoles.begin();
             it != proposal->currentRoles.end(); ++it)
        {
            Player* pMember = sObjectAccessor.FindPlayer(it->first);
            MANGOS_ASSERT(pMember);
            if (pMember->GetGroup())
            {
                Player::RemoveFromGroup(pMember->GetGroup(), it->first);
            }
        }

        pGroup = new Group();
        bool const groupCreated = pGroup->Create(pLeader->GetObjectGuid(), pLeader->GetName());
        MANGOS_ASSERT(groupCreated);
        if (!groupCreated)
        {
            sLog.outError("LFG: preflighted proposal %u failed to persist its dungeon group.",
                          proposal->id);
            delete pGroup;
            return;
        }

        // Group::Create inherits the leader's manual setting. Replace it before
        // the group is marked LFG, registered, or given another member, so nobody
        // observes or persists the wrong Heroic/Normal state.
        if (difficultyPlan.isRaid)
        {
            pGroup->SetRaidDifficulty(Difficulty(difficultyPlan.mode));
        }
        else
        {
            pGroup->SetDungeonDifficulty(Difficulty(difficultyPlan.mode));
        }
        pGroup->SetAsLfgGroup();

        if (plan.convertToRaid)
        {
            pGroup->ConvertToRaid();
        }

        sObjectMgr.AddGroup(pGroup);
    }

    if (!createdNewGroup)
    {
        // Continuing run: synchronize the run's selected mode before any matched
        // replacement is added and inherits the group's current difficulty.
        if (difficultyPlan.isRaid)
        {
            pGroup->SetRaidDifficulty(Difficulty(difficultyPlan.mode));
        }
        else
        {
            pGroup->SetDungeonDifficulty(Difficulty(difficultyPlan.mode));
        }

        if (plan.convertToRaid && !pGroup->isRaidGroup())
        {
            pGroup->ConvertToRaid();
        }
    }

    // Everyone in the proposal who is not already in this group joins it. That covers
    // both paths: a freshly created group needs every non-leader added, and a reused
    // premade needs the solo queuers that were matched into it.
    ObjectGuid const groupGuid = pGroup->GetObjectGuid();
    for (roleMap::const_iterator it = proposal->currentRoles.begin();
         it != proposal->currentRoles.end(); ++it)
    {
        Player* pMember = sObjectAccessor.FindPlayer(it->first);
        MANGOS_ASSERT(pMember);
        if (pGroup->IsMember(it->first))
        {
            continue;
        }

        if (Group* existing = pMember->GetGroup())
        {
            Player::RemoveFromGroup(existing, it->first);
        }

        bool const memberAdded = pGroup->AddMember(it->first, pMember->GetName());
        MANGOS_ASSERT(memberAdded);
        if (!memberAdded)
        {
            sLog.outError("LFG: preflighted proposal %u could not add %s to group %u "
                          "(currently %u members).", proposal->id,
                          it->first.GetString().c_str(), pGroup->GetId(),
                          pGroup->GetMembersCount());
        }
    }

    MANGOS_ASSERT(pGroup->GetMembersCount() == plan.finalSize);

    // Add group to our group set and group map, then teleport to the dungeon.
    // groupGuid is the one taken above; do not shadow it.
    LFGGroupStatus groupStatus(LFG_STATE_IN_DUNGEON, dungeon->ID, proposal->currentRoles, pGroup->GetLeaderGuid());
    // Only when the two differ was this a random queue; proposal->dungeonID is the row the
    // player actually picked, which for a random IS the category.
    if (proposal->concreteDungeonID && proposal->dungeonID != proposal->concreteDungeonID)
    {
        groupStatus.randomDungeonID = proposal->dungeonID;
    }

    // Carry forward what the run has already achieved.
    //
    // This assignment replaces every field, so re-forming a proposal around a group that
    // is ALREADY running -- which is what a backfill does -- reset madeProgress to false.
    // The group would then be back inside its protected opening, and anyone leaving after
    // that would take Dungeon Deserter for a run whose first boss was long dead.
    if (LFGGroupStatus const* existing = GetGroupStatus(groupGuid))
    {
        groupStatus.madeProgress = groupStatus.madeProgress || existing->madeProgress;
        if (!groupStatus.randomDungeonID)
        {
            groupStatus.randomDungeonID = existing->randomDungeonID;
        }
    }

    if (proposal->groupRawGuid && GetGroupStatus(groupGuid))
    {
        // CONTINUING a live run: merge the roster in, leave the run itself alone.
        //
        // Replacing wholesale would reset state, dungeonID and madeProgress on a group
        // that is mid-dungeon -- putting it back inside its protected opening, so anyone
        // leaving would take Deserter for a run whose first boss was long dead, and
        // repointing dungeonID at whatever the proposal happened to carry.
        LFGGroupStatus* live = GetGroupStatus(groupGuid);
        for (roleMap::const_iterator it = proposal->currentRoles.begin();
             it != proposal->currentRoles.end(); ++it)
        {
            live->playerRoles[it->first] = it->second;
        }
        live->leaderGuid = pGroup->GetLeaderGuid();
    }
    else
    {
        m_groupSet.insert(groupGuid);
        m_groupStatusMap[groupGuid] = groupStatus;
    }

    // Announce the group BEFORE the teleport as well as after.
    //
    // SMSG_GROUP_LIST's LFG block is the ONLY thing the 18414 client reads for an
    // in-progress run -- IsPartyLFG, GetPartyLFGID, HasLFGRestrictions and IsInLFGDungeon
    // all hang off fields the group-list apply path is the sole writer of. Miss it and the
    // player has no eye, no teleport options and no Vote Kick gate.
    //
    // The status is stored just above, so an update sent here already carries the block.
    // The one AFTER the teleport does not reliably reach everyone: TeleportToDungeon far-
    // teleports each member, which removes them from their map, and a member already in
    // flight is not reached by the member walk. Observed live 2026-08-06 20:35:04 -- of two
    // players entering Wailing Caverns together, only the second received an isLfg=1 body
    // (81 bytes); the leader's last group list was 64 bytes with the flag clear, and he had
    // no eye. It only showed up when queueing as a PARTY, because queueing separately adds
    // and teleports members in a different order.
    //
    // Both calls are kept. This one guarantees every member holds the LFG block while they
    // are all still in world; the one after covers state that only settles post-teleport.
    pGroup->SendUpdate();

    TeleportToDungeon(dungeon->ID, pGroup);

    pGroup->SendUpdate();
}

void LFGMgr::TeleportToDungeon(uint32 dungeonID, Group* pGroup, Player* onlyPlayer /*= NULL*/)
{
    // if the group's leader is already in the dungeon, teleport anyone not in dungeon to them
    // if nobody is in the dungeon, teleport all to beginning of dungeon (sObjectMgr.GetMapEntranceTrigger(mapid [not dungeonid]))
    LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(dungeonID);
    if (!dungeon || !pGroup)
    {
        return;
    }

    uint32 mapID = (uint32)dungeon->MapID;
    float x, y, z, o;
    LFGTeleportError err = LFG_TELEPORTERROR_OK;

    // Whether this run came from a random category decides who takes the 15-minute
    // cooldown below.
    LFGGroupStatus const* runStatus = GetGroupStatus(pGroup->GetObjectGuid());

    // Prefer a group member already INSIDE the dungeon; entrance trigger otherwise.
    //
    // A previous commit here replaced this with an unconditional entrance trigger, arguing
    // that every instanced SMSG_NEW_WORLD arrival in the corpus lands on one fixed
    // coordinate per map. That argument does not hold and the change is withdrawn:
    //
    //   - The rule being tested is "entrance UNLESS a member is already on the map". For
    //     every ordinary run nobody is inside yet, so it REDUCES to the entrance trigger.
    //     A fixed per-map coordinate is therefore what BOTH readings predict, and the
    //     observation cannot separate them.
    //   - The check was said to be made in the ten captures carrying a backfill offer. A
    //     capture containing SMSG_LFG_OFFER_CONTINUE is a REMAINING member's session --
    //     that client is already inside and receives no SMSG_NEW_WORLD for the newcomer's
    //     arrival at all. Those captures cannot hold the observation claimed from them.
    //
    // Against it: Warcraft Wiki states replacements appear at the location of the players
    // already inside, and PandariaCore (LFGMgr.cpp:2117-2150, leader then any member) and
    // Legends-of-Azeroth implement exactly that. SkyFire's copy is dead code -- its loop
    // condition `itr != NULL && !mapid` never fires because mapid is pre-set.
    //
    // Leader first, then any member on the map, entrance last. This is a no-op for a
    // normal run and does not affect WHICH instance is used -- the group bind decides that.
    Player* teleportTo = sObjectAccessor.FindPlayer(pGroup->GetLeaderGuid());
    if (!teleportTo || teleportTo->GetMapId() != mapID)
    {
        teleportTo = NULL;
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player* pMember = itr->getSource();
            if (pMember && pMember->GetMapId() == mapID)
            {
                teleportTo = pMember;
                break;
            }
        }
    }

    DungeonFinderEntrance const* lfgEntrance =
        sObjectMgr.GetDungeonFinderEntrance(dungeonID);
    AreaTrigger const* physicalEntrance = sObjectMgr.GetMapEntranceTrigger(mapID);

    switch (LFGStatePolicy::ChooseEntranceSource(
        teleportTo != NULL, lfgEntrance != NULL, physicalEntrance != NULL))
    {
        case LFGStatePolicy::EntranceSource::InMapMember:
            x = teleportTo->GetPositionX();
            y = teleportTo->GetPositionY();
            z = teleportTo->GetPositionZ();
            o = teleportTo->GetOrientation();
            break;
        case LFGStatePolicy::EntranceSource::LfgOnly:
            x = lfgEntrance->x;
            y = lfgEntrance->y;
            z = lfgEntrance->z;
            o = lfgEntrance->orientation;
            break;
        case LFGStatePolicy::EntranceSource::Physical:
            x = physicalEntrance->target_X;
            y = physicalEntrance->target_Y;
            z = physicalEntrance->target_Z;
            o = physicalEntrance->target_Orientation;
            break;
        case LFGStatePolicy::EntranceSource::None:
            sLog.outError("LFG TeleportToDungeon: no LFG-only entrance for dungeon %u "
                          "and no physical entrance targeting map %u",
                          dungeonID, mapID);
            err = LFG_TELEPORTERROR_INVALID_LOCATION;
            break;
    }

    dungeonForbidden lockedDungeons;
    for (GroupReference* itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        if (Player* pGroupPlr = itr->getSource())
        {
            // A voluntary "Teleport to dungeon" moves the player who asked for it, nobody
            // else. The group is still what decides WHERE -- the leader-or-any-member
            // position resolved above is what puts a returning player back with the party
            // rather than at the entrance -- but only the caller is moved.
            //
            // This used to rely on the map check further down to do the filtering, on the
            // reasoning that everyone else is already inside so only the one who left would
            // qualify. That assumption fails the moment the rest of the group is outside
            // too: with the whole party in the world, every member qualifies and one
            // player's eyeball drags all five in. Reported live -- ".tele group northshire"
            // then any single member clicking Teleport to dungeon moved the entire group,
            // from any member, not just the leader.
            //
            // The mandatory path (CreateDungeonGroup, on proposal accept) passes no
            // onlyPlayer and still moves everyone, which is correct: that IS a group entry.
            if (onlyPlayer && pGroupPlr != onlyPlayer)
            {
                continue;
            }

            // further checks: player is dead, in vehicle, in battleground, on taxi, etc
            LFGTeleportError plrErr = LFG_TELEPORTERROR_OK;

            if (pGroupPlr->IsDead())
            {
                plrErr = LFG_TELEPORTERROR_PLAYER_DEAD;
            }
            if (pGroupPlr->IsFalling())
            {
                plrErr = LFG_TELEPORTERROR_FALLING;
            }
            if (pGroupPlr->GetVehicleInfo())
            {
                plrErr = LFG_TELEPORTERROR_IN_VEHICLE;
            }
            // NO combat check here, deliberately.
            //
            // TeleportToDungeon also runs from CreateDungeonGroup, where a proposal has
            // just been accepted and the group formed. Refusing one member there teleports
            // everyone else in and strands that player -- still in the group, still set to
            // LFG_STATE_IN_DUNGEON. When this was written they also got no feedback at
            // all, because SMSG_LFG_TELEPORT_DENIED was not admitted; it is now, but
            // being stranded silently is not much improved by being told why. A proposal accept is a mandatory
            // group form, and the client's own message for this
            // (ERR_PARTY_LFG_TELEPORT_IN_COMBAT) is about teleporting OUT of a dungeon,
            // not about being placed into one.
            //
            // The voluntary paths are still covered: CMSG_LFG_TELEPORT and the leave
            // teleport both go through TeleportPlayer, which keeps its own combat guard.

            lockedDungeons = FindRandomDungeonsNotForPlayer(pGroupPlr);
            if (lockedDungeons.find(dungeon->Entry()) != lockedDungeons.end())
            {
                plrErr = LFG_TELEPORTERROR_INVALID_LOCATION;
            }

            // A far teleport already in flight must not be started again. TeleportTo only
            // records m_teleport_dest and raises the far semaphore; m_mapId keeps the OLD
            // value until MSG_MOVE_WORLDPORT_ACK arrives, so `GetMapId() != mapID` is still
            // true for a player who is halfway through this very teleport. Clicking "Enter
            // Dungeon" right after a proposal auto-entry reaches here a second time and
            // would re-issue the teleport against a player who is no longer on any map.
            if (err == LFG_TELEPORTERROR_OK && plrErr == LFG_TELEPORTERROR_OK &&
                pGroupPlr->GetMapId() != mapID && !pGroupPlr->IsBeingTeleported())
            {
                // NO SetBattleGroundEntryPoint() here.
                //
                // It used to be taken on every entry, which meant walking out of the portal
                // and teleporting back in overwrote the saved return point with the dungeon's
                // own doorstep -- permanently losing the place the player queued from. The
                // point is now captured once, at queue time, by LFGMgr::RecordEntryPoint.

                // The 15-minute requeue cooldown is cast HERE, before the teleport, and not
                // in the success branch below.
                //
                // A far TeleportTo removes the player from the old map immediately --
                // Map::Remove calls ResetMap(), leaving m_currMap NULL until the client
                // answers with MSG_MOVE_WORLDPORT_ACK. Spell::CheckCast then reads
                // m_caster->GetZoneAndAreaId(), which goes through WorldObject::GetTerrain()
                // and dereferences that NULL map. Confirmed from the crash dump of
                // 2026-08-06 13:28:31: read of Map+0x8258 (m_TerrainData) off a null this,
                // m_spellInfo->Id == 0x116A0 (71328), caster m_currMap == 0 while still
                // holding its pre-teleport position on map 0.
                //
                // It only ever survived because ApplyDungeonCooldown no-ops when the aura is
                // already present -- a character who had just run a random still had it, so
                // the cast was skipped. The first fresh character to enter crashed the world.
                //
                // Casting first is also the correct semantics: the cooldown starts when the
                // player ENTERS, and this block is exactly the "is about to enter" path. A
                // member already standing on the dungeon's map is not entering and no longer
                // takes a fresh cooldown.
                //
                // ONLY for a queue that was made through Random Dungeon.
                //
                // 71328 is the RANDOM cooldown. A player who queued for a specific
                // dungeon did not take it on retail, and applying it to them is not a
                // harmless over-approximation: it is the aura that blocks the next
                // random queue, so a specific-dungeon run would lock the player out of
                // random for 15 minutes they never owed.
                //
                // It also matters that the two systems stay independent. Deserter must
                // never be conditional on this aura -- that was JadeCore's bug, and it
                // exempts early leavers from specific-dungeon groups entirely.
                //
                // randomDungeonID is non-zero exactly when the queue was a random
                // category, recorded at group creation from the proposal.
                if (runStatus && runStatus->randomDungeonID)
                {
                    ApplyDungeonCooldown(pGroupPlr);
                }

                if (!pGroupPlr->TeleportTo(mapID, x, y, z, o))
                {
                    plrErr = LFG_TELEPORTERROR_INVALID_LOCATION;
                }
            }

            if (err != LFG_TELEPORTERROR_OK)
            {
                sLog.outError("LFG TeleportToDungeon: %s DENIED, dungeon %u map %u, group error %u",
                              pGroupPlr->GetName(), dungeonID, mapID, uint32(err));
                pGroupPlr->GetSession()->SendLfgTeleportError(err);
            }
            else if (plrErr != LFG_TELEPORTERROR_OK)
            {
                sLog.outError("LFG TeleportToDungeon: %s DENIED, dungeon %u map %u, player error %u",
                              pGroupPlr->GetName(), dungeonID, mapID, uint32(plrErr));
                pGroupPlr->GetSession()->SendLfgTeleportError(plrErr);
            }
            else
            {
                // The cooldown is NOT applied here -- see the teleport block above. Casting
                // anything on a player whose far teleport has already started dereferences a
                // null Map.
                SetPlayerState(pGroupPlr->GetObjectGuid(), LFG_STATE_IN_DUNGEON);
            }
        }
    }
}

void LFGMgr::TeleportPlayer(Player* pPlayer, bool out)
{
    // Fetch necessary data first
    // Every refusal below now REACHES the player: SMSG_LFG_TELEPORT_DENIED carries a
    // derived four-bit reason and is admitted through the send gate. It was not, when this
    // logging was added -- a refused teleport then produced no packet at all, and the log
    // line was the only trace. Keep logging anyway: the reason codes are coarse, and a
    // player reporting "teleport out did nothing" is still far easier to diagnose with the
    // branch recorded server-side. Observed live
    // 2026-08-06 21:04: two CMSG_LFG_TELEPORT from a player alone in Shadowfang Keep, no
    // reply, no log line, and no way to tell which branch refused.
    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        sLog.outError("LFG TeleportPlayer: %s refused (%s) -- not in a group",
                      pPlayer->GetName(), out ? "out" : "in");
        pPlayer->GetSession()->SendLfgTeleportError((uint8)LFG_TELEPORTERROR_INVALID_LOCATION);
        return;
    }

    LFGGroupStatus* status = GetGroupStatus(pGroup->GetObjectGuid());
    if (!status)
    {
        sLog.outError("LFG TeleportPlayer: %s refused (%s) -- group %s has no LFG status",
                      pPlayer->GetName(), out ? "out" : "in",
                      pGroup->GetObjectGuid().GetString().c_str());
        pPlayer->GetSession()->SendLfgTeleportError((uint8)LFG_TELEPORTERROR_INVALID_LOCATION);
        return;
    }

    // Never move a player who is fighting, in EITHER direction.
    //
    // Without this the dropdown was an instant combat escape -- pull a pack, teleport
    // out, and the fight is simply over -- and Leave Instance Group yanked the player
    // out mid-pull, leaving the rest of the group in a fight they did not choose to
    // take alone. The client agrees this is refusable: it ships the message for it
    // (ERR_PARTY_LFG_TELEPORT_IN_COMBAT, "You cannot teleport out of the dungeon while
    // in combat.").
    //
    // Deliberately covers `in` as well. Teleporting INTO a dungeon while fighting
    // something outside it strands the mob and drops the player into an instance still
    // flagged in combat.
    //
    // This guard sits in TeleportPlayer rather than at the call sites so that the
    // dropdown (CMSG_LFG_TELEPORT) and the leave path (CMSG_GROUP_DISBAND) are both
    // covered by one check that cannot be forgotten by a third caller.
    // Per-player, deliberately -- NOT group-wide.
    //
    // Retail gates this on the caller alone: Wowpedia's Dungeon Finder page says players
    // may teleport in and out at any time if THEY are not in combat, and WoWWiki gives the
    // condition as "you won't be teleported if you are in combat, jumping or falling" --
    // the same three per-player conditions TeleportToDungeon already checks below.
    //
    // A group-wide gate was written and then withdrawn. It closes a real hole (combat is
    // per-unit, so a ranged dps who never took threat is not flagged and can leave mid-pull)
    // but retail apparently does not close it, and wire fidelity wins here. If that hole
    // ever needs closing, gate the LEAVE path rather than this function, and only on members
    // standing on the dungeon's own map -- a member who already ported out and is fighting
    // something in the world must not lock the players still inside.
    //
    // The check covers `in` as well as `out`: teleporting INTO a dungeon while fighting
    // something outside it strands the mob and drops the player in still flagged.
    //
    // It sits in TeleportPlayer rather than at the call sites so the dropdown
    // (CMSG_LFG_TELEPORT) and the leave path (CMSG_GROUP_DISBAND) are both covered by one
    // check a third caller cannot forget.
    if (pPlayer->IsInCombat())
    {
        DEBUG_LOG("LFG TeleportPlayer: %s refused (%s) -- in combat",
                  pPlayer->GetName(), out ? "out" : "in");
        pPlayer->GetSession()->SendLfgTeleportError((uint8)LFG_TELEPORTERROR_IN_COMBAT);
        return;
    }

    // Get dungeon info and then teleport the player out if applicable
    if (out)
    {
        LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(status->dungeonID);
        if (dungeon && pPlayer->GetMapId() == dungeon->MapID)
        {
            pPlayer->TeleportToBGEntryPoint();
        }
        else
        {
            // The silent branch. It fires when the run's recorded dungeon does not resolve
            // to the map the player is standing on -- which is a REAL defect whenever it
            // happens, not a legitimate refusal, because a player asking to leave a dungeon
            // they are demonstrably inside should always be able to.
            //
            // The likeliest cause is a run whose dungeonID is a random CATEGORY row rather
            // than the concrete dungeon that was entered: SendDungeonProposal has been seen
            // to log "entry dungeons={258} -> chose 258 (entry 0x06000102)", i.e. TypeID 6,
            // for a group that then zoned into Shadowfang Keep.
            sLog.outError("LFG TeleportPlayer: %s refused (out) -- dungeon %u resolves to "
                          "map %d but the player is on map %u; leaving them stranded",
                          pPlayer->GetName(), status->dungeonID,
                          dungeon ? int32(dungeon->MapID) : -1, pPlayer->GetMapId());
        }
        return;
    }

    // Teleport back IN.
    //
    // This branch did not exist: TeleportPlayer only ever handled `out`, so the dropdown's
    // "Teleport to dungeon" resolved the group and the status and then fell off the end of
    // the function doing nothing. Observed live -- a player who ported out could not get
    // back, which is worse than not offering the option at all.
    //
    // TeleportToDungeon is the same routine the proposal uses on group creation, so it
    // carries the dead / falling / in-vehicle checks and the SMSG_LFG_TELEPORT_DENIED
    // replies with it, and it prefers a member already inside as the destination, which is
    // what puts a returning player back with the group rather than at the entrance.
    //
    // Restricted to the caller. This is a voluntary, per-player action: the map check alone
    // is NOT sufficient filtering, because when the rest of the group is also outside every
    // member passes it and one player's eyeball teleports the whole party in.
    TeleportToDungeon(status->dungeonID, pGroup, pPlayer);
}

LFGGroupStatus* LFGMgr::GetGroupStatus(ObjectGuid guid)
{
    groupStatusMap::iterator it = m_groupStatusMap.find(guid);
    if (it != m_groupStatusMap.end())
    {
        return &(it->second);
    }
    else
    {
        return NULL;
    }
}

/// Legacy per-player decline teardown.
///
/// No longer on the decline path: ProposalUpdate routes declines through CancelProposal,
/// which implements the three outcomes the client actually describes (decliner out,
/// their premade out, everyone else requeued). Kept because the boot/kick flow still
/// references this shape, but it must not be called for a proposal response.
void LFGMgr::ProposalDeclined(ObjectGuid guid, LFGProposal* proposal)
{
    Player* pPlayer = sObjectAccessor.FindPlayer(guid);

    if (!pPlayer)
    {
        return;
    }

    bool leaveGroupLFG = false;

    for (roleMap::iterator it = proposal->currentRoles.begin(); it != proposal->currentRoles.end(); ++it)
    {
        ObjectGuid groupPlrGuid = it->first;

        // update each player with a LFG_UPDATE_PROPOSAL_DECLINED
        SetPlayerUpdateType(groupPlrGuid, LFG_UPDATE_PROPOSAL_DECLINED);

        Player* pGroupPlayer = sObjectAccessor.FindPlayer(groupPlrGuid);
        if (!pGroupPlayer)
        {
            continue;
        }
        Group* pGroup = pGroupPlayer->GetGroup();

        // if player was in a premade group and declined, remove the group.
        if (groupPlrGuid == guid)
        {
            //LeaveLFG(pGroupPlayer, true);
            if (pGroup && (pGroup->GetObjectGuid().GetRawValue() == proposal->groupRawGuid))
            {
                leaveGroupLFG = true;
            }

            SendLfgUpdate(groupPlrGuid, GetPlayerStatus(groupPlrGuid), false);
        }
        else
        {
            if (proposal->groupRawGuid)
            {
                SendLfgUpdate(groupPlrGuid, GetPlayerStatus(groupPlrGuid), true);
            }
            else
            {
                SendLfgUpdate(groupPlrGuid, GetPlayerStatus(groupPlrGuid), false);
            }
        }
    }

    // The proposal is erased by ProposalUpdate, which owns it -- erasing here destroyed
    // the object our caller still holds a pointer to. Nor is there any point pruning the
    // decliner out of currentRoles/answers/groups any more: the whole proposal is torn
    // down either way, and pruning was exactly what let the survivors read as unanimous.
    LeaveLFG(pPlayer, leaveGroupLFG);
}

void LFGMgr::UpdateWaitMap(LFGRoles role, uint32 dungeonID, time_t waitTime)
{
    if (!role || !dungeonID || !waitTime)
    {
        return;
    }

    switch (role)
    {
        case PLAYER_ROLE_TANK:
        {
            waitTimeMap::iterator it = m_tankWaitTime.find(dungeonID);
            if (it != m_tankWaitTime.end())
            {
                LFGWait wait = it->second;

                wait.previousTime = wait.time;
                wait.time = waitTime;
                wait.doAverage = true;

                m_tankWaitTime[dungeonID] = wait;
            }
        }
            break;
        case PLAYER_ROLE_HEALER:
        {
            waitTimeMap::iterator hIt = m_healerWaitTime.find(dungeonID);
            if (hIt != m_healerWaitTime.end())
            {
                LFGWait wait = hIt->second;

                wait.previousTime = wait.time;
                wait.time = waitTime;
                wait.doAverage = true;

                m_healerWaitTime[dungeonID] = wait;
            }
        }
            break;
        case PLAYER_ROLE_DAMAGE:
        {
            waitTimeMap::iterator dIt = m_dpsWaitTime.find(dungeonID);
            if (dIt != m_dpsWaitTime.end())
            {
                LFGWait wait = dIt->second;

                wait.previousTime = wait.time;
                wait.time = waitTime;
                wait.doAverage = true;

                m_dpsWaitTime[dungeonID] = wait;
            }
        }
            break;
        default:
        {
            waitTimeMap::iterator aIt = m_avgWaitTime.find(dungeonID);
            if (aIt != m_avgWaitTime.end())
            {
                LFGWait wait = aIt->second;

                wait.previousTime = wait.time;
                wait.time = waitTime;
                wait.doAverage = true;

                m_avgWaitTime[dungeonID] = wait;
            }
        }
            break;
    }

}

/// Hand a completion reward item to a player, falling back to mail when the bags are full.
///
/// A dungeon reward must not be silently dropped because the player finished the run with no
/// free slot -- that is precisely when it is most likely, since they have just looted a boss.
/// Mailing it is what retail does and what the rest of this server already does for
/// achievement rewards (AchievementMgr.cpp:2219).
void LFGMgr::GiveDungeonRewardItem(Player* pPlayer, uint32 itemId, uint32 amount)
{
    ItemPrototype const* proto = ObjectMgr::GetItemPrototype(itemId);
    if (!proto)
    {
        sLog.outError("LFG GiveDungeonRewardItem: reward item %u does not exist; %s paid nothing",
                      itemId, pPlayer->GetGuidStr().c_str());
        return;
    }

    ItemPosCountVec dest;
    uint32 noSpaceCount = 0;
    pPlayer->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, amount, &noSpaceCount);

    // Driven off `dest` and `noSpaceCount`, NOT off the return code.
    //
    // On a PARTIAL fit _CanStoreItem fills `dest` with the portion that does fit,
    // reports the remainder through noSpaceCount, and still returns an error.
    // Gating the store on EQUIP_ERR_OK therefore threw the storable portion away
    // and mailed only the overflow, so the player silently lost part of the
    // reward. That is the ordinary case for a two-item reward with one free slot,
    // not an exotic one.
    uint32 const stored = amount > noSpaceCount ? amount - noSpaceCount : 0;
    if (stored && !dest.empty())
    {
        if (Item* item = pPlayer->StoreNewItem(dest, itemId, true))
        {
            pPlayer->SendNewItem(item, stored, true, false);
        }
    }

    // Whatever did not fit goes in the post, in as many stacks as it takes.
    //
    // Item::CreateItem clamps count to the item's maximum stack size and creates
    // exactly ONE stack, so a single call silently discarded anything above it.
    // Today's rewards are 1-2 and fit any stack, but a larger one would have lost
    // the excess without a trace.
    uint32 remaining = noSpaceCount;
    uint32 const maxStack = proto->GetMaxStackSize() ? proto->GetMaxStackSize() : 1;
    while (remaining)
    {
        uint32 const thisStack = remaining < maxStack ? remaining : maxStack;

        Item* mailItem = Item::CreateItem(itemId, thisStack, pPlayer);
        if (!mailItem)
        {
            sLog.outError("LFG GiveDungeonRewardItem: could not create %u x%u for mail to %s; %u lost",
                          itemId, thisStack, pPlayer->GetGuidStr().c_str(), remaining);
            return;
        }

        mailItem->SaveToDB();                               // persist before send, or a failed send loses it

        std::string const subject = proto->Name1 ? proto->Name1 : "";
        MailDraft draft(subject, "");
        draft.AddItem(mailItem);
        draft.SendMailTo(MailReceiver(pPlayer), MailSender(MAIL_CREATURE, uint32(0)));

        remaining -= thisStack;
    }
}

void LFGMgr::HandleBossKilled(Player* pPlayer)
{
    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        return;
    }

    ObjectGuid groupGuid = pGroup->GetObjectGuid();
    LFGGroupStatus* status = GetGroupStatus(groupGuid);
    if (!status)
    {
        return;
    }

    // Second guard on top of the encounter-bit check in UpdateEncounterState. Rewards
    // are paid from here, so a run must complete exactly once however it is reached --
    // a re-credited encounter, a script firing the same completion twice, or a future
    // caller that does not know about the first guard.
    if (status->state == LFG_STATE_FINISHED_DUNGEON)
    {
        DEBUG_LOG("LFG HandleBossKilled: group %s already finished; ignoring",
                  groupGuid.GetString().c_str());
        return;
    }

    // set each player's lfgstate to LFG_STATE_FINISHED_DUNGEON
    // fetch reward info, and if it's the first dungeon of the day (per player),
    //    give them 2x the xp (or 1x if it's not the first), and the reward item
    //    (special case for 2nd wotlk heroic and +). If no room in inventory, send
    //    via ingame mail.
    status->state = LFG_STATE_FINISHED_DUNGEON;

    DungeonTypes type = GetDungeonType(status->dungeonID);
    for (GroupReference* itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next()) //todo: check if we will need to use mail or not
    {
        if (Player* pGroupPlr = itr->getSource())
        {
            SetPlayerState(pGroupPlr->GetObjectGuid(), LFG_STATE_FINISHED_DUNGEON);

            // Completing the run resolves the random cooldown. Retail treats a finished
            // dungeon as clearing it -- the restriction exists to pace entry into NEW
            // random runs, and the player has now actually finished the one they took it
            // for. Leaving it on would make a clean completion punish the player exactly
            // as much as walking out at the door.
            //
            // Removed rather than left to expire so the group can immediately queue again,
            // which is the whole point of finishing.
            pGroupPlr->RemoveAurasDueToSpell(LFG_COOLDOWN_SPELL);

            // check if player did a random dungeon
            uint32 randomDungeonId = 0;
            LfgDungeonsEntry const* dungeon = sLfgDungeonsStore.LookupEntry(status->dungeonID);
            // A stored dungeon id that no longer resolves -- stale group state,
            // or a DBC that changed under a saved group -- would crash the
            // reward path rather than simply pay nothing.
            if (dungeon && (dungeon->TypeID == LFG_TYPE_RANDOM_DUNGEON || IsSeasonal(dungeon->Flags)))
            {
                randomDungeonId = dungeon->ID;
            }

            // get rewards
            uint32 groupPlrLevel = pGroupPlr->getLevel();
            const DungeonFinderRewards* rewards = sObjectMgr.GetDungeonFinderRewards(groupPlrLevel); // Fetch base xp/money reward
            if (!rewards)
            {
                // Unconditionally dereferenced below. dungeonfinder_rewards ships 66
                // rows covering levels 15-80, so every level 81-90 character -- i.e.
                // every MoP-relevant one -- crashed the world server on a tracked boss
                // kill. No row means no base reward, not a crash.
                continue;
            }

            ItemRewards itemRewards = GetDungeonItemRewards(status->dungeonID, type);                // fetch item reward

            int32 multiplier;                                                                        // base reward modifier
            bool hasDoneDaily = HasPlayerDoneDaily(pGroupPlr->GetGUIDLow(), type);                                 // first dungeon of the day?
            (hasDoneDaily) ? multiplier = 1 : multiplier = 2;

            uint32 xpReward = multiplier*rewards->baseXPReward;                                      // player's xp reward
            uint32 moneyReward = uint32(multiplier*rewards->baseMonetaryReward);                              // player's money reward

            uint32 itemReward = 0;                                                                   // reward item
            uint32 itemAmount = 0;                                                                   // amount of item
            if (hasDoneDaily && (type == DUNGEON_WOTLK_HEROIC))
            {
                itemReward = WOTLK_SPECIAL_HEROIC_ITEM;
                itemAmount = WOTLK_SPECIAL_HEROIC_AMNT;
            }
            else if (!hasDoneDaily)
            {
                itemReward = itemRewards.itemId;
                itemAmount = itemRewards.itemAmount;
            }

            // Actually pay the player.
            //
            // Everything above computed a reward and then only ever announced it. Nothing
            // in the LFG code granted money, experience or the satchel -- a grep for
            // ModifyMoney, GiveXP or StoreNewItem across the LFG sources returned nothing
            // -- so finishing a dungeon paid exactly zero, and the announcement was
            // dropped by the enter-world send gate on top of that.
            if (moneyReward)
            {
                pGroupPlr->ModifyMoney(int64(moneyReward));
            }

            if (xpReward)
            {
                // GiveXP is a no-op at max level, which is the correct behaviour here:
                // the money component above is what a level-capped character keeps.
                pGroupPlr->GiveXP(xpReward, NULL);
            }

            if (itemReward && itemAmount)
            {
                GiveDungeonRewardItem(pGroupPlr, itemReward, itemAmount);
            }

            // Record the run against the daily allowance AFTER the reward is decided.
            //
            // RegisterPlayerDaily had no callers at all, so HasPlayerDoneDaily was
            // permanently false: every run took the first-of-the-day branch and paid the
            // doubled reward plus the satchel, for ever. It has to be set here, once the
            // multiplier and the item have already been chosen from the pre-run value.
            RegisterPlayerDaily(pGroupPlr->GetGUIDLow(), type);

            // and then fill a structure corresponding to SMSG_LFG_PLAYER_REWARD and
            // send one of these to each player
            LFGRewards reward(randomDungeonId, status->dungeonID, hasDoneDaily, moneyReward, xpReward, itemReward, itemAmount);
            pGroupPlr->GetSession()->SendLfgRewards(reward);
        }
    }

    // The status deliberately SURVIVES the final boss.
    //
    // It used to be erased here, while the Group object kept GROUPTYPE_LFD. From that
    // moment every Group::SendUpdate emitted an LFG block whose dungeon slot resolved
    // through GetGroupDungeonEntry -> GetGroupStatus -> null -> 0, i.e. isLfg = 1 with a
    // ZERO slot A. That is the case Group.cpp warns is worse than sending no block at
    // all: the client copies it, party+232 becomes 0, and IsPartyLFG() goes false --
    // so the Leave Dungeon button and the minimap dungeon state vanished from the first
    // tracked boss kill onward, mid-run.
    //
    // The state was already moved to LFG_STATE_FINISHED_DUNGEON above, which is what
    // makes the block's state byte report 2 (IsLFGComplete). Release happens in
    // ReleaseGroupLfgStatus, called when the group is actually disbanded.
}

void LFGMgr::AttemptToKickPlayer(Group* pGroup, ObjectGuid guid, ObjectGuid kicker, std::string reason)
{
    if (!pGroup)
    {
        return;
    }

    ObjectGuid groupGuid = pGroup->GetObjectGuid();
    LFGGroupStatus* status = GetGroupStatus(groupGuid);
    if (!status)
    {
        return;
    }

    Player* pKicker = sObjectAccessor.FindPlayer(kicker);

    // Refusals below are reported with SMSG_PARTY_COMMAND_RESULT, whose flat 18414
    // body is already verified and admitted. Without them the initiator gets no
    // response at all and the client leaves the Remove entry looking broken.
    if (status->state == LFG_STATE_BOOT)
    {
        if (pKicker)
        {
            pKicker->GetSession()->SendPartyResult(PARTY_OP_LEAVE, "", ERR_PARTY_LFG_BOOT_IN_PROGRESS);
        }
        return;
    }

    if (status->state == LFG_STATE_FINISHED_DUNGEON)
    {
        // Nothing left to protect the group from once the run is done, and the
        // client ships a dedicated message for exactly this.
        if (pKicker)
        {
            pKicker->GetSession()->SendPartyResult(PARTY_OP_LEAVE, "", ERR_PARTY_LFG_BOOT_DUNGEON_COMPLETE);
        }
        return;
    }

    // A vote that cannot possibly reach the threshold must not be started: it would
    // freeze the group in LFG_STATE_BOOT until the timer expired, blocking any
    // further attempt for the whole window. Everyone except the target may vote
    // yes, so the group needs REQUIRED_VOTES_FOR_BOOT + 1 members to succeed at all.
    // Note the asymmetry this leaves, and why it is tolerable ONLY because expiry
    // exists. The victim does not vote, so a group of N has N-1 voters and the
    // initiator's is already AGREE.
    //
    // At N = 4 at most 2 denies are possible, so a non-unanimous vote can never reach
    // the deny threshold at all. At N = 5 a 1-agree/2-deny split among the three
    // remaining voters leaves BOTH counts at 2 and stalls the same way. In each case
    // the vote can only resolve by running out the LFG_TIME_BOOT window. That is a
    // correct outcome, and
    // RemoveOldBoots delivers it -- but if the reaper is ever removed, a four-man
    // group is wedged in LFG_STATE_BOOT permanently. Retail avoids the corner by
    // scaling votesNeeded with group size (the corpus shows 13 for a 25-man LFR);
    // we do not, so the reaper is load-bearing rather than belt-and-braces.
    if (int32(pGroup->GetMembersCount()) <= REQUIRED_VOTES_FOR_BOOT)
    {
        if (pKicker)
        {
            pKicker->GetSession()->SendPartyResult(PARTY_OP_LEAVE, "", ERR_PARTY_LFG_BOOT_TOO_FEW_PLAYERS);
        }
        return;
    }

    status->state = LFG_STATE_BOOT;
    m_groupStatusMap[groupGuid] = *status;

    // This function is only called when a group is set/in a dungeon so we can go straight to the boot packets
    time_t now = time(NULL);
    proposalAnswerMap votes;

    // The initiator's own vote counts as agree. The VICTIM is deliberately not polled.
    //
    // They used to be seeded with LFG_ANSWER_DENY, which the tally counted toward `nay`.
    // With REQUIRED_VOTES_FOR_BOOT = 3 that let a five-man kick fail on only TWO genuine
    // denies, because the victim supplied the third for free, and it inflated the
    // voteCount the client displays by a vote nobody cast. Retail does not poll the
    // player being voted on.
    //
    // Leaving them out of the map entirely is also what CastVote's membership check
    // keys off, so the victim cannot vote on their own removal by any route, and
    // SendLfgBootUpdate tolerates the missing entry.
    votes[kicker] = LFG_ANSWER_AGREE;

    // set group state to boot vote, same for player states until it's over
    for (GroupReference* itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next()) //todo: check if we will need to use mail or not
    {
        if (Player* pGroupPlr = itr->getSource())
        {
            ObjectGuid pGroupPlrGuid = pGroupPlr->GetObjectGuid();

            SetPlayerState(pGroupPlrGuid, LFG_STATE_BOOT);

            if ( (pGroupPlrGuid != guid) && (pGroupPlrGuid != kicker) )
            {
                votes[pGroupPlrGuid] = LFG_ANSWER_PENDING;
            }
        }
    }

    LFGBoot boot(true, guid, reason, votes, now);
    m_bootStatusMap[groupGuid] = boot;

    for (GroupReference* it = pGroup->GetFirstMember(); it != NULL; it = it->next())
    {
        if (Player* groupPlr = it->getSource())
        {
            groupPlr->GetSession()->SendLfgBootUpdate(boot);
        }
    }
}

void LFGMgr::CastVote(Player* pPlayer, bool vote)
{
    if (!pPlayer)
    {
        return;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        // The vote body carries no group and no target, so a client that votes with
        // no group at all reaches here. Dereferencing was an unchecked crash on a
        // packet any client can send.
        return;
    }

    ObjectGuid groupGuid = pGroup->GetObjectGuid();

    LFGGroupStatus* status = GetGroupStatus(groupGuid);

    if (!status || status->state != LFG_STATE_BOOT)
    {
        return;
    }

    bootStatusMap::iterator it = m_bootStatusMap.find(groupGuid);
    if (it == m_bootStatusMap.end())
    {
        return;
    }

    LFGBoot boot = it->second;

    // The player being voted on does not get a say in their own removal.
    if (pPlayer->GetObjectGuid() == boot.playerVotedOn)
    {
        return;
    }

    // Nor does anyone who was not part of the vote when it started -- otherwise a
    // member who joined mid-vote could tip a tally they were never counted in.
    if (boot.answers.find(pPlayer->GetObjectGuid()) == boot.answers.end())
    {
        return;
    }

    boot.answers[pPlayer->GetObjectGuid()] = LFGProposalAnswer(vote);

    int32 yay = 0, nay = 0; // keep a count of votes
    for (proposalAnswerMap::iterator pIt = boot.answers.begin(); pIt != boot.answers.end(); ++pIt)
    {
        LFGProposalAnswer answer = pIt->second;
        if (answer == LFG_ANSWER_AGREE)
        {
            ++yay;
        }
        else if (answer == LFG_ANSWER_DENY)
        {
            ++nay;
        }
    }

    if (yay < REQUIRED_VOTES_FOR_BOOT && nay < REQUIRED_VOTES_FOR_BOOT)
    {
        m_bootStatusMap[groupGuid] = boot;
        return;
    }

    bool const passed = yay >= REQUIRED_VOTES_FOR_BOOT;
    FinishBootVote(groupGuid, pGroup, boot, passed, true);

    if (passed)
    {
        // Put them back where they queued from BEFORE removing them from the group.
        // Once the group is gone so is the LFG status this reads, and a booted
        // player left standing inside the instance can simply walk back to the
        // group -- the removal is meaningless without the teleport.
        if (Player* pVictim = sObjectAccessor.FindPlayer(boot.playerVotedOn))
        {
            if (pVictim->IsInWorld())
            {
                pVictim->TeleportToBGEntryPoint();
            }
        }

        // kick player from group
        // Test for ZERO, not <= 1. Group::RemoveMember returns the surviving member
        // count, and an LFG group is now allowed to live on with a single member, so 1
        // no longer means "disbanded". Deleting there would free a group that is still
        // in play. Unreachable today -- REQUIRED_VOTES_FOR_BOOT = 3 means a vote cannot
        // start below five members, leaving at least three after a kick -- but it is a
        // use-after-free the moment that constant is lowered, which the LFR work will
        // want to do.
        if (pGroup->RemoveMember(boot.playerVotedOn, 1) == 0)
        {
            // group->Disband(); already disbanded in RemoveMember
            sObjectMgr.RemoveGroup(pGroup);
            delete pGroup;
            // removemember sets the player's group pointer to NULL
        }
    }
}

void LFGMgr::SendRoleChosen(ObjectGuid plrGuid, ObjectGuid confirmedGuid, uint8 roles)
{
    Player* pPlayer = sObjectAccessor.FindPlayer(plrGuid);

    if (pPlayer)
    {
        pPlayer->GetSession()->SendLfgRoleChosen(confirmedGuid.GetRawValue(), roles);
    }
}

void LFGMgr::SendRoleCheckUpdate(ObjectGuid plrGuid, LFGRoleCheck const& roleCheck)
{
    Player* pPlayer = sObjectAccessor.FindPlayer(plrGuid);

    if (pPlayer)
    {
        pPlayer->GetSession()->SendLfgRoleCheckUpdate(roleCheck);
    }
}

void LFGMgr::SendLfgUpdate(ObjectGuid plrGuid, LFGPlayerStatus status, bool fallbackIsGroup)
{
    Player* pPlayer = sObjectAccessor.FindPlayer(plrGuid);

    if (pPlayer)
    {
        pPlayer->GetSession()->SendLfgUpdate(fallbackIsGroup, status);
    }
}

void LFGMgr::SendLfgJoinResult(ObjectGuid plrGuid, LfgJoinResult result, uint8 detail, partyForbidden const& lockedDungeons)
{
    Player* pPlayer = sObjectAccessor.FindPlayer(plrGuid);

    if (pPlayer)
    {
        pPlayer->GetSession()->SendLfgJoinResult(result, detail, lockedDungeons);
    }
}

void LFGMgr::RemoveOldRoleChecks()
{
    // Erase-safe iteration. m_roleCheckMap is an unordered_map, so erasing by
    // key destroys the node roleItr points at and the following ++roleItr walks
    // freed memory. This is the FIRST thing LFGMgr::Update calls, so it would
    // crash or spin the world thread on the first tick that finds an expired
    // check.
    for (roleCheckMap::iterator roleItr = m_roleCheckMap.begin(); roleItr != m_roleCheckMap.end(); )
    {
        ObjectGuid groupGuid = roleItr->first;

        LFGRoleCheck roleCheck = roleItr->second;
        if ((roleCheck.waitForRoleTime - time(NULL)) <= 0) // no time left
        {
            roleCheck.state = LFG_ROLECHECK_NO_ROLE;

            for (roleMap::iterator roleMapItr = roleCheck.currentRoles.begin(); roleMapItr != roleCheck.currentRoles.end(); ++roleMapItr)
            {
                ObjectGuid plrGuid = roleMapItr->first;

                SetPlayerState(plrGuid, LFG_STATE_NONE);

                SendRoleCheckUpdate(plrGuid, roleCheck);                 // role check failed
                SendLfgUpdate(plrGuid, GetPlayerStatus(plrGuid), true);  // not in lfg system anymore
            }

            // Advance BEFORE erasing, and drop the queue data this check owned:
            // the entries JoinLFG wrote for the group would otherwise survive
            // with nothing left to resolve them.
            m_playerData.erase(groupGuid);
            m_queueSet.erase(groupGuid);
            roleItr = m_roleCheckMap.erase(roleItr);
        }
        else
        {
            ++roleItr;
        }
    }
}
