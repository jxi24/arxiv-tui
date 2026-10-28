// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AuthorMatch.hh"

#include <catch2/catch_test_macros.hpp>

using Arxiv::AuthorMatches;

// A followed name matches an author when the whole last names agree, the
// first initials agree (and the first names, when both are spelled out), and
// any middle initials that both names give agree. This tells apart the many
// authors who share a common last name.

TEST_CASE("Author matching: whole last names only", "[author][match]") {
    REQUIRE(AuthorMatches("Wei Li", "Wei Li"));
    REQUIRE_FALSE(AuthorMatches("Wei Li", "Wei Lin"));
    REQUIRE_FALSE(AuthorMatches("Wei Li", "Wei Liang"));
}

TEST_CASE("Author matching: first initial", "[author][match]") {
    SECTION("initial matches a spelled-out first name") {
        REQUIRE(AuthorMatches("W. Li", "Wei Li"));
        REQUIRE(AuthorMatches("Wei Li", "W. Li"));
        REQUIRE(AuthorMatches("W Li", "Wei Li"));
    }
    SECTION("a different initial is a different person") {
        REQUIRE_FALSE(AuthorMatches("W. Li", "X. Li"));
    }
    SECTION("two spelled-out first names must agree") {
        REQUIRE_FALSE(AuthorMatches("Wei Li", "Wen Li"));
        REQUIRE_FALSE(AuthorMatches("John Smith", "Jane Smith"));
    }
}

TEST_CASE("Author matching: middle initial is optional", "[author][match]") {
    SECTION("matches when either side omits it") {
        REQUIRE(AuthorMatches("J. A. Smith", "John Smith"));
        REQUIRE(AuthorMatches("John Smith", "J. A. Smith"));
        REQUIRE(AuthorMatches("J. A. Smith", "John Andrew Smith"));
    }
    SECTION("written together") { REQUIRE(AuthorMatches("J.A. Smith", "John A. Smith")); }
    SECTION("must agree when both give one") {
        REQUIRE_FALSE(AuthorMatches("J. A. Smith", "J. B. Smith"));
        REQUIRE_FALSE(AuthorMatches("John A. Smith", "John Brian Smith"));
    }
}

TEST_CASE("Author matching: details", "[author][match]") {
    SECTION("case-insensitive") { REQUIRE(AuthorMatches("wei li", "Wei LI")); }
    SECTION("hyphenated first name") { REQUIRE(AuthorMatches("J.-L. Picard", "Jean-Luc Picard")); }
    SECTION("name particles belong to the last name") {
        REQUIRE(AuthorMatches("J. van der Berg", "Jan van der Berg"));
        REQUIRE_FALSE(AuthorMatches("J. van der Berg", "J. Berg"));
    }
    SECTION("a last name alone matches that last name") {
        REQUIRE(AuthorMatches("Li", "Wei Li"));
        REQUIRE_FALSE(AuthorMatches("Li", "Wei Lin"));
    }
    SECTION("reversed order is not the same name") {
        REQUIRE_FALSE(AuthorMatches("Wei Li", "Li Wei"));
    }
    SECTION("empty never matches") { REQUIRE_FALSE(AuthorMatches("", "Wei Li")); }
}

TEST_CASE("Author names: whether a first initial is given", "[author][match]") {
    using Arxiv::HasFirstInitial;
    SECTION("initial") { REQUIRE(HasFirstInitial("W. Li")); }
    SECTION("spelled out") { REQUIRE(HasFirstInitial("Wei Li")); }
    SECTION("last name alone") { REQUIRE_FALSE(HasFirstInitial("Li")); }
    SECTION("particle and last name alone") { REQUIRE_FALSE(HasFirstInitial("van der Berg")); }
    SECTION("empty") { REQUIRE_FALSE(HasFirstInitial("  ")); }
}

TEST_CASE("Author names: capitalised particles are given names", "[author][match]") {
    // Only lower-case "van", "le", ... belong to the last name.
    REQUIRE(Arxiv::HasFirstInitial("Le Wang"));
    REQUIRE(AuthorMatches("L. Wang", "Le Wang"));
}
