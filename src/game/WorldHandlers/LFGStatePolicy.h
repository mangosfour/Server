/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Side-effect-free Dungeon Finder state-transition decisions.
 */

#ifndef MANGOS_LFG_STATE_POLICY_H
#define MANGOS_LFG_STATE_POLICY_H

#include "Common.h"

#include <set>

namespace LFGStatePolicy
{
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

inline QueueSelectionPlan MergeQueueSelection(
    uint32 mainRandomDungeonId, uint32 bufferRandomDungeonId,
    std::set<uint32> const& compatibleDungeons)
{
    if (mainRandomDungeonId && bufferRandomDungeonId &&
        mainRandomDungeonId != bufferRandomDungeonId)
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
