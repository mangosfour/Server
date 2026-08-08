/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Side-effect-free Dungeon Finder state-transition decisions.
 */

#ifndef MANGOS_LFG_STATE_POLICY_H
#define MANGOS_LFG_STATE_POLICY_H

#include "Common.h"

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
}

#endif
