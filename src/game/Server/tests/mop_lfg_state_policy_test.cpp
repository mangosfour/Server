/*
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
 * Focused state-transition policy coverage for the MoP dungeon finder.
 */

#include "LFGStatePolicy.h"

#include <cstdio>
#include <set>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void TestDifficultyMustResolveBeforeMutation()
{
    LFGStatePolicy::DifficultyPlan plan =
        LFGStatePolicy::ResolveDifficulty(0, false, 3, 4);
    CHECK(plan.valid);
    CHECK(!plan.isRaid);
    CHECK(plan.mode == 0);

    plan = LFGStatePolicy::ResolveDifficulty(1, false, 3, 4);
    CHECK(plan.valid);
    CHECK(!plan.isRaid);
    CHECK(plan.mode == 1);

    plan = LFGStatePolicy::ResolveDifficulty(3, true, 3, 4);
    CHECK(plan.valid);
    CHECK(plan.isRaid);
    CHECK(plan.mode == 3);

    plan = LFGStatePolicy::ResolveDifficulty(-1, false, 3, 4);
    CHECK(!plan.valid);

    plan = LFGStatePolicy::ResolveDifficulty(3, false, 3, 4);
    CHECK(!plan.valid);

    plan = LFGStatePolicy::ResolveDifficulty(4, true, 3, 4);
    CHECK(!plan.valid);
}

static void TestBackfillAnswerIsAssignedOnce()
{
    CHECK(LFGStatePolicy::InitialProposalAnswer(true) ==
          LFGStatePolicy::ProposalAnswerDecision::Agree);
    CHECK(LFGStatePolicy::InitialProposalAnswer(false) ==
          LFGStatePolicy::ProposalAnswerDecision::Pending);
}

static void TestRandomIdentitySurvivesConcreteMerge()
{
    std::set<uint32> const concrete = { 6u };

    CHECK(LFGStatePolicy::CanMergeQueueSelections(0, 258));
    CHECK(LFGStatePolicy::CanMergeQueueSelections(258, 0));
    CHECK(LFGStatePolicy::CanMergeQueueSelections(258, 258));
    CHECK(!LFGStatePolicy::CanMergeQueueSelections(258, 999));

    LFGStatePolicy::QueueSelectionPlan plan =
        LFGStatePolicy::MergeQueueSelection(0, 258, concrete);
    CHECK(plan.valid);
    CHECK(plan.randomDungeonId == 258u);
    CHECK(plan.requestedDungeons == std::set<uint32>({ 258u }));
    CHECK(plan.candidateDungeons == concrete);

    plan = LFGStatePolicy::MergeQueueSelection(258, 0, concrete);
    CHECK(plan.valid);
    CHECK(plan.randomDungeonId == 258u);
    CHECK(plan.requestedDungeons == std::set<uint32>({ 258u }));
    CHECK(plan.candidateDungeons == concrete);

    plan = LFGStatePolicy::MergeQueueSelection(258, 999, concrete);
    CHECK(!plan.valid);

    plan = LFGStatePolicy::MergeQueueSelection(0, 0, concrete);
    CHECK(plan.valid);
    CHECK(plan.randomDungeonId == 0u);
    CHECK(plan.requestedDungeons == concrete);
    CHECK(plan.candidateDungeons == concrete);
}

static void TestProposalRequiresConcreteDestination()
{
    CHECK(!LFGStatePolicy::CanStartProposal(0));
    CHECK(LFGStatePolicy::CanStartProposal(6));
}

static void TestDungeonEntrancePriority()
{
    using LFGStatePolicy::EntranceSource;

    CHECK(LFGStatePolicy::ChooseEntranceSource(true, true, true) ==
          EntranceSource::InMapMember);
    CHECK(LFGStatePolicy::ChooseEntranceSource(false, true, true) ==
          EntranceSource::LfgOnly);
    CHECK(LFGStatePolicy::ChooseEntranceSource(false, false, true) ==
          EntranceSource::Physical);
    CHECK(LFGStatePolicy::ChooseEntranceSource(false, false, false) ==
          EntranceSource::None);
}

static void TestProposalCompletionRequiresFullPreflight()
{
    CHECK(LFGStatePolicy::CanCompleteProposal(true, true, true, true, 5, 5));
    CHECK(!LFGStatePolicy::CanCompleteProposal(false, true, true, true, 5, 5));
    CHECK(!LFGStatePolicy::CanCompleteProposal(true, false, true, true, 5, 5));
    CHECK(!LFGStatePolicy::CanCompleteProposal(true, true, false, true, 5, 5));
    CHECK(!LFGStatePolicy::CanCompleteProposal(true, true, true, false, 5, 5));
    CHECK(!LFGStatePolicy::CanCompleteProposal(true, true, true, true, 6, 5));
    CHECK(!LFGStatePolicy::CanCompleteProposal(true, true, true, true, 0, 5));
    CHECK(LFGStatePolicy::CanCompleteProposal(true, true, true, true, 25, 40));
}

static void TestOnlyLeaderMutatesAGroupQueue()
{
    CHECK(LFGStatePolicy::CanMutateGroupQueue(false, false));
    CHECK(LFGStatePolicy::CanMutateGroupQueue(true, true));
    CHECK(!LFGStatePolicy::CanMutateGroupQueue(true, false));
}

static void TestOnlyRosterMembersSubmitRoles()
{
    CHECK(LFGStatePolicy::CanSubmitRole(true, true));
    CHECK(!LFGStatePolicy::CanSubmitRole(true, false));
    CHECK(!LFGStatePolicy::CanSubmitRole(false, true));
}

static void TestBootTerminalStates()
{
    LFGStatePolicy::BootTerminalPlan plan =
        LFGStatePolicy::ResolveBootTerminal(false, true);
    CHECK(plan.restoreGroup);
    CHECK(plan.survivor == LFGStatePolicy::BootPlayerState::InDungeon);
    CHECK(plan.victim == LFGStatePolicy::BootPlayerState::InDungeon);

    plan = LFGStatePolicy::ResolveBootTerminal(true, true);
    CHECK(plan.restoreGroup);
    CHECK(plan.survivor == LFGStatePolicy::BootPlayerState::InDungeon);
    CHECK(plan.victim == LFGStatePolicy::BootPlayerState::None);

    plan = LFGStatePolicy::ResolveBootTerminal(false, false);
    CHECK(!plan.restoreGroup);
    CHECK(plan.survivor == LFGStatePolicy::BootPlayerState::None);
    CHECK(plan.victim == LFGStatePolicy::BootPlayerState::None);
}

static void TestTicketIdentityRetainsRequesterUntilNewQueue()
{
    LFGStatePolicy::TicketIdentity ticket;

    ticket.Retain(0x11, 101, 1001);
    CHECK(ticket.requesterGuid == 0x11);
    CHECK(ticket.id == 101);
    CHECK(ticket.time == 1001);

    // A merge or regroup must not replace the identity already sent to the client.
    ticket.Retain(0x22, 202, 2002);
    CHECK(ticket.requesterGuid == 0x11);
    CHECK(ticket.id == 101);
    CHECK(ticket.time == 1001);

    // A genuinely new queue owns a new complete ticket identity.
    ticket.Begin(0x22, 202, 2002);
    CHECK(ticket.requesterGuid == 0x22);
    CHECK(ticket.id == 202);
    CHECK(ticket.time == 2002);
}

int main()
{
    TestDifficultyMustResolveBeforeMutation();
    TestBackfillAnswerIsAssignedOnce();
    TestRandomIdentitySurvivesConcreteMerge();
    TestProposalRequiresConcreteDestination();
    TestDungeonEntrancePriority();
    TestProposalCompletionRequiresFullPreflight();
    TestOnlyLeaderMutatesAGroupQueue();
    TestOnlyRosterMembersSubmitRoles();
    TestBootTerminalStates();
    TestTicketIdentityRetainsRequesterUntilNewQueue();
    return g_fail == 0 ? 0 : 1;
}
