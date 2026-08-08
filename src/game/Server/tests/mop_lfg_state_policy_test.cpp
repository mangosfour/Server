/*
 * SPDX-License-Identifier: GPL-3.0-or-later
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
    return g_fail == 0 ? 0 : 1;
}
