/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Database content-version comparison tests.
 */

#include "Database/DatabaseVersion.h"

#include <cstdio>

namespace
{
int failures = 0;
int casesRun = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

using DatabaseVersion::CompareContent;
using DatabaseVersion::ContentComparison;

// Break caught: comparing decimal content versions lexicographically at 9 -> 10.
void TestNumericOrdering()
{
    ++casesRun;
    CHECK(CompareContent("9", "10") == ContentComparison::Older);
    CHECK(CompareContent("10", "10") == ContentComparison::Equal);
    CHECK(CompareContent("11", "10") == ContentComparison::Newer);
    CHECK(CompareContent("99", "100") == ContentComparison::Older);
    CHECK(CompareContent("100", "99") == ContentComparison::Newer);
}

// Break caught: treating formatting zeroes as significant version digits.
void TestLeadingZeroes()
{
    ++casesRun;
    CHECK(CompareContent("009", "10") == ContentComparison::Older);
    CHECK(CompareContent("010", "10") == ContentComparison::Equal);
    CHECK(CompareContent("000", "0") == ContentComparison::Equal);
}

// Break caught: silently ordering malformed generated or database content values.
void TestMalformedValues()
{
    ++casesRun;
    CHECK(CompareContent("", "10") == ContentComparison::Invalid);
    CHECK(CompareContent("9", "") == ContentComparison::Invalid);
    CHECK(CompareContent("9x", "10") == ContentComparison::Invalid);
    CHECK(CompareContent("+9", "10") == ContentComparison::Invalid);
    CHECK(CompareContent("-1", "10") == ContentComparison::Invalid);
}
}

int main()
{
    TestNumericOrdering();
    TestLeadingZeroes();
    TestMalformedValues();

    if (failures == 0)
    {
        std::printf("PASS: %d database content-version cases\n", casesRun);
    }
    return failures == 0 ? 0 : 1;
}
