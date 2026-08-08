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

int main()
{
    TestDifficultyMustResolveBeforeMutation();
    TestBackfillAnswerIsAssignedOnce();
    TestRandomIdentitySurvivesConcreteMerge();
    TestProposalRequiresConcreteDestination();
    return g_fail == 0 ? 0 : 1;
}
