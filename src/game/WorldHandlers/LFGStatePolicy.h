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
 *
 * Side-effect-free Dungeon Finder state-transition decisions.
 */

#ifndef MANGOS_LFG_STATE_POLICY_H
#define MANGOS_LFG_STATE_POLICY_H

#include "Common.h"

#include <set>

namespace LFGStatePolicy
{
struct TicketIdentity
{
    uint64 requesterGuid = 0;
    uint32 id = 0;
    uint32 time = 0;

    // FIRST WINS for one queue lifetime. The client keys status records on this
    // identity, so a merge or regroup must not replace any field independently.
    void Retain(uint64 requester, uint32 ticketId, uint32 ticketTime)
    {
        if (!ticketId || id)
        {
            return;
        }

        Begin(requester, ticketId, ticketTime);
    }

    // A genuinely new queue replaces the complete identity atomically.
    void Begin(uint64 requester, uint32 ticketId, uint32 ticketTime)
    {
        requesterGuid = requester;
        id = ticketId;
        time = ticketTime;
    }
};

inline TicketIdentity ResolveTicketIdentity(TicketIdentity const* retained,
                                              uint64 fallbackRequester,
                                              uint32 fallbackId,
                                              uint32 fallbackTime)
{
    if (retained && retained->id)
    {
        return *retained;
    }

    TicketIdentity fallback;
    fallback.Begin(fallbackRequester, fallbackId, fallbackTime);
    return fallback;
}

struct DifficultyPlan
{
    bool valid;
    bool isRaid;
    uint8 mode;
};

inline DifficultyPlan ResolveDifficulty(int32 translatedMode, bool isRaid,
                                        uint8 maxDungeonModes,
                                        uint8 maxRaidModes)
{
    uint8 const upperBound = isRaid ? maxRaidModes : maxDungeonModes;
    if (translatedMode < 0 || translatedMode >= upperBound)
    {
        return { false, isRaid, 0 };
    }

    return { true, isRaid, uint8(translatedMode) };
}

enum class ProposalAnswerDecision : uint8
{
    Pending,
    Agree
};

inline ProposalAnswerDecision InitialProposalAnswer(bool continuingMember)
{
    return continuingMember ? ProposalAnswerDecision::Agree
                            : ProposalAnswerDecision::Pending;
}

struct QueueSelectionPlan
{
    bool valid;
    uint32 randomDungeonId;
    std::set<uint32> requestedDungeons;
    std::set<uint32> candidateDungeons;
};

inline bool CanMergeQueueSelections(uint32 mainRandomDungeonId,
                                    uint32 bufferRandomDungeonId)
{
    return !mainRandomDungeonId || !bufferRandomDungeonId ||
           mainRandomDungeonId == bufferRandomDungeonId;
}

inline QueueSelectionPlan MergeQueueSelection(
    uint32 mainRandomDungeonId, uint32 bufferRandomDungeonId,
    std::set<uint32> const& compatibleDungeons)
{
    if (!CanMergeQueueSelections(mainRandomDungeonId, bufferRandomDungeonId))
    {
        return { false, 0, {}, {} };
    }

    uint32 const randomDungeonId = mainRandomDungeonId
        ? mainRandomDungeonId : bufferRandomDungeonId;

    std::set<uint32> requested = randomDungeonId
        ? std::set<uint32>{ randomDungeonId }
        : compatibleDungeons;

    // A non-random multi-selection is narrowed to the same concrete overlap.
    // For a random queue this remains separate from the category identity.
    return { true, randomDungeonId, requested, compatibleDungeons };
}

inline bool CanStartProposal(uint32 concreteDungeonId)
{
    return concreteDungeonId != 0;
}

enum class EntranceSource : uint8
{
    None,
    InMapMember,
    LfgOnly,
    Physical
};

inline EntranceSource ChooseEntranceSource(bool hasInMapMember,
                                           bool hasLfgEntrance,
                                           bool hasPhysicalEntrance)
{
    if (hasInMapMember)
    {
        return EntranceSource::InMapMember;
    }

    if (hasLfgEntrance)
    {
        return EntranceSource::LfgOnly;
    }

    if (hasPhysicalEntrance)
    {
        return EntranceSource::Physical;
    }

    return EntranceSource::None;
}

inline bool CanCompleteProposal(bool allMembersOnline, bool hasDungeon,
                                bool validDifficulty, bool hasGroupOrLeader,
                                uint32 finalSize, uint32 capacity)
{
    return allMembersOnline && hasDungeon && validDifficulty &&
           hasGroupOrLeader && finalSize != 0 && capacity != 0 &&
           finalSize <= capacity;
}

inline bool CanMutateGroupQueue(bool isGrouped, bool isLeader)
{
    return !isGrouped || isLeader;
}

inline bool CanSubmitRole(bool hasPlayer, bool isRosterMember)
{
    return hasPlayer && isRosterMember;
}

enum class BootPlayerState
{
    None,
    InDungeon
};

struct BootTerminalPlan
{
    bool restoreGroup;
    BootPlayerState survivor;
    BootPlayerState victim;
};

inline BootTerminalPlan ResolveBootTerminal(bool removeVictim, bool groupExists)
{
    if (!groupExists)
    {
        return { false, BootPlayerState::None, BootPlayerState::None };
    }

    return { true, BootPlayerState::InDungeon,
             removeVictim ? BootPlayerState::None : BootPlayerState::InDungeon };
}
}

#endif
