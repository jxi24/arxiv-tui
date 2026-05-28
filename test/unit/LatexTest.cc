// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/LatexUtils.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>
#include <vector>

using namespace Catch::Matchers;

// ---------------------------------------------------------------------------
// StripLatex — unit tests
// ---------------------------------------------------------------------------

TEST_CASE("StripLatex: plain text is unchanged", "[latex]") {
    REQUIRE(Arxiv::StripLatex("hello world") == "hello world");
}

TEST_CASE("StripLatex: removes inline math $...$", "[latex]") {
    std::string result = Arxiv::StripLatex("energy $E = mc^2$ is conserved");
    REQUIRE_THAT(result, !ContainsSubstring("$"));
    REQUIRE_THAT(result, !ContainsSubstring("mc^2"));
    REQUIRE_THAT(result, ContainsSubstring("energy"));
    REQUIRE_THAT(result, ContainsSubstring("conserved"));
}

TEST_CASE("StripLatex: removes display math $$...$$", "[latex]") {
    std::string result = Arxiv::StripLatex("consider $$\\int_0^\\infty f(x) dx$$ below");
    REQUIRE_THAT(result, !ContainsSubstring("$"));
    REQUIRE_THAT(result, !ContainsSubstring("int_0"));
    REQUIRE_THAT(result, ContainsSubstring("consider"));
    REQUIRE_THAT(result, ContainsSubstring("below"));
}

TEST_CASE("StripLatex: keeps content of \\cmd{content}", "[latex]") {
    SECTION("\\textbf") {
        std::string result = Arxiv::StripLatex("\\textbf{important}");
        REQUIRE_THAT(result, ContainsSubstring("important"));
        REQUIRE_THAT(result, !ContainsSubstring("textbf"));
    }

    SECTION("\\emph") {
        std::string result = Arxiv::StripLatex("this is \\emph{emphasized} text");
        REQUIRE_THAT(result, ContainsSubstring("emphasized"));
        REQUIRE_THAT(result, ContainsSubstring("text"));
        REQUIRE_THAT(result, !ContainsSubstring("\\emph"));
    }

    SECTION("nested braces keep inner content") {
        std::string result = Arxiv::StripLatex("\\title{{Fast Methods}}");
        REQUIRE_THAT(result, ContainsSubstring("Fast"));
        REQUIRE_THAT(result, ContainsSubstring("Methods"));
    }
}

TEST_CASE("StripLatex: removes bare \\cmd tokens", "[latex]") {
    std::string result = Arxiv::StripLatex("see \\cite and \\ref for details");
    REQUIRE_THAT(result, !ContainsSubstring("\\cite"));
    REQUIRE_THAT(result, !ContainsSubstring("\\ref"));
    REQUIRE_THAT(result, ContainsSubstring("see"));
    REQUIRE_THAT(result, ContainsSubstring("details"));
}

TEST_CASE("StripLatex: removes leading backslash from known patterns", "[latex]") {
    // \hat{p} → p (backslash command consumed, content kept)
    std::string result = Arxiv::StripLatex("\\hat{p}");
    REQUIRE_THAT(result, ContainsSubstring("p"));
    REQUIRE_THAT(result, !ContainsSubstring("hat"));
}

TEST_CASE("StripLatex: mixed LaTeX and prose", "[latex]") {
    const std::string input =
        "We present $\\hat{p}_T$ distributions for \\textbf{heavy-ion} collisions "
        "at $\\sqrt{s} = 13$ TeV using the method of \\emph{jet substructure}.";
    std::string result = Arxiv::StripLatex(input);

    REQUIRE_THAT(result, ContainsSubstring("distributions"));
    REQUIRE_THAT(result, ContainsSubstring("heavy-ion"));
    REQUIRE_THAT(result, ContainsSubstring("collisions"));
    REQUIRE_THAT(result, ContainsSubstring("TeV"));
    REQUIRE_THAT(result, ContainsSubstring("jet substructure"));
    // Math regions stripped
    REQUIRE_THAT(result, !ContainsSubstring("hat"));
    REQUIRE_THAT(result, !ContainsSubstring("sqrt"));
    REQUIRE_THAT(result, !ContainsSubstring("$"));
}

TEST_CASE("StripLatex: empty string returns empty", "[latex]") {
    REQUIRE(Arxiv::StripLatex("").empty());
}

TEST_CASE("StripLatex: unmatched $ does not crash", "[latex]") {
    // Should not throw; exact output is implementation-defined
    std::string result;
    REQUIRE_NOTHROW(result = Arxiv::StripLatex("dangling $ sign"));
}
