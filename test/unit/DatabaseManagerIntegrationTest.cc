#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "Arxiv/DatabaseManager.hh"
#include "Arxiv/Article.hh"

using namespace Catch::Matchers;
using Arxiv::DatabaseManager;
using Arxiv::Article;

// ---------------------------------------------------------------------------
// Helper — build a minimal Article
// ---------------------------------------------------------------------------

static Article make_article(
    const std::string& link,
    const std::string& title,
    const std::string& authors,
    const std::string& abstract_text)
{
    Article a;
    a.link     = link;
    a.title    = title;
    a.authors  = authors;
    a.abstract = abstract_text;
    a.date     = std::chrono::system_clock::now();
    a.bookmarked = false;
    return a;
}

// ---------------------------------------------------------------------------
// AddArticle — special characters round-trip
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::AddArticle round-trips single quotes", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/2403.00001",
        "O'Brien's theorem: a proof",
        "O'Brien, Patrick and D'Alembert, Jean",
        "We prove O'Brien's conjecture using D'Alembert's method.");

    db.AddArticle(art);

    auto articles = db.GetRecent(-1);
    REQUIRE(articles.size() == 1);
    REQUIRE(articles[0].title   == "O'Brien's theorem: a proof");
    REQUIRE(articles[0].authors == "O'Brien, Patrick and D'Alembert, Jean");
    REQUIRE(articles[0].abstract == "We prove O'Brien's conjecture using D'Alembert's method.");
}

TEST_CASE("DatabaseManager::AddArticle round-trips curly braces", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/2403.00002",
        "Transverse momentum $\\hat{p}_T$ distributions",
        "Smith, Alice",
        "We study {$\\hat{p}_T$} in {heavy-ion} collisions at the LHC.");

    db.AddArticle(art);

    auto articles = db.GetRecent(-1);
    REQUIRE(articles.size() == 1);
    REQUIRE(articles[0].title    == "Transverse momentum $\\hat{p}_T$ distributions");
    REQUIRE(articles[0].abstract == "We study {$\\hat{p}_T$} in {heavy-ion} collisions at the LHC.");
}

TEST_CASE("DatabaseManager::AddArticle round-trips multi-byte UTF-8", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/2403.00003",
        "Étude sur les méthodes numériques",
        "Hébert, Rémi and Björk, Åsa",
        "Nous présentons une méthode innovante basée sur l'analyse de Fourier.");

    db.AddArticle(art);

    auto articles = db.GetRecent(-1);
    REQUIRE(articles.size() == 1);
    REQUIRE(articles[0].title   == "Étude sur les méthodes numériques");
    REQUIRE(articles[0].authors == "Hébert, Rémi and Björk, Åsa");
    REQUIRE(articles[0].abstract == "Nous présentons une méthode innovante basée sur l'analyse de Fourier.");
}

// ---------------------------------------------------------------------------
// ToggleBookmark — link with special chars
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::ToggleBookmark round-trips link with special chars", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/it's-a-2403.00004",
        "Test Article",
        "Tester",
        "Abstract");
    db.AddArticle(art);
    db.ToggleBookmark(art.link, true);

    auto bookmarked = db.ListBookmarked();
    REQUIRE(bookmarked.size() == 1);
    REQUIRE(bookmarked[0].link == art.link);
    REQUIRE(bookmarked[0].bookmarked == true);
}

// ---------------------------------------------------------------------------
// AddProject / RemoveProject — names with special chars
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::AddProject round-trips name with single quote", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    db.AddProject("Alice's Research");
    auto projects = db.GetProjects();
    REQUIRE(projects.size() == 1);
    REQUIRE(projects[0] == "Alice's Research");
}

TEST_CASE("DatabaseManager::AddProject round-trips name with curly braces", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    db.AddProject("{Quantum} Computing");
    auto projects = db.GetProjects();
    REQUIRE(projects.size() == 1);
    REQUIRE(projects[0] == "{Quantum} Computing");
}

TEST_CASE("DatabaseManager::RemoveProject with single-quote name removes cleanly", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    db.AddProject("Bob's Project");
    db.RemoveProject("Bob's Project");
    auto projects = db.GetProjects();
    REQUIRE(projects.empty());
}

// ---------------------------------------------------------------------------
// LinkArticleToProject / UnlinkArticleFromProject
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::LinkArticleToProject round-trips with special chars", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/2403.00005",
        "Article with 'quotes' and {braces}",
        "O'Neill, Ryan",
        "Abstract text.");
    db.AddArticle(art);
    db.AddProject("O'Neill's Project");
    db.LinkArticleToProject(art.link, "O'Neill's Project");

    auto arts = db.GetArticlesForProject("O'Neill's Project");
    REQUIRE(arts.size() == 1);
    REQUIRE(arts[0].title == "Article with 'quotes' and {braces}");
}

TEST_CASE("DatabaseManager::UnlinkArticleFromProject with special chars", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/2403.00006",
        "Title",
        "Author",
        "Abstract");
    db.AddArticle(art);
    db.AddProject("O'Brien's Group");
    db.LinkArticleToProject(art.link, "O'Brien's Group");
    db.UnlinkArticleFromProject(art.link, "O'Brien's Group");

    auto arts = db.GetArticlesForProject("O'Brien's Group");
    REQUIRE(arts.empty());
}

// ---------------------------------------------------------------------------
// SetProjectParent / GetProjectParent
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::SetProjectParent round-trips parent with single quote", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    db.AddProject("Parent's Lab");
    db.AddProject("Child's Group");
    db.SetProjectParent("Child's Group", "Parent's Lab");

    std::string parent = db.GetProjectParent("Child's Group");
    REQUIRE(parent == "Parent's Lab");
}

// ---------------------------------------------------------------------------
// SetRating / GetRating
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::SetRating round-trips link with single quote", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/O'Brien-2403.00007",
        "Title",
        "Author",
        "Abstract");
    db.AddArticle(art);
    db.SetRating(art.link, 4);

    int rating = db.GetRating(art.link);
    REQUIRE(rating == 4);
}

TEST_CASE("DatabaseManager::SetRating round-trips link with curly braces", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/{special}-2403.00008",
        "Title",
        "Author",
        "Abstract");
    db.AddArticle(art);
    db.SetRating(art.link, 5);

    int rating = db.GetRating(art.link);
    REQUIRE(rating == 5);
}

// ---------------------------------------------------------------------------
// SetProjectNote / GetProjectNote
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::SetProjectNote round-trips note with single quote", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/2403.00009",
        "Title",
        "Author",
        "Abstract");
    db.AddArticle(art);
    db.AddProject("MyProject");
    db.LinkArticleToProject(art.link, "MyProject");

    db.SetProjectNote("MyProject", art.link, "It's an important paper about O'Brien's method.");
    std::string note = db.GetProjectNote("MyProject", art.link);
    REQUIRE(note == "It's an important paper about O'Brien's method.");
}

TEST_CASE("DatabaseManager::SetProjectNote round-trips note with curly braces and UTF-8", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/2403.00010",
        "Title",
        "Author",
        "Abstract");
    db.AddArticle(art);
    db.AddProject("PhysicsProject");
    db.LinkArticleToProject(art.link, "PhysicsProject");

    const std::string note = "Uses {$\\hat{p}_T$} formalism. See Hébert et al.";
    db.SetProjectNote("PhysicsProject", art.link, note);
    std::string retrieved = db.GetProjectNote("PhysicsProject", art.link);
    REQUIRE(retrieved == note);
}

// ---------------------------------------------------------------------------
// SearchArticles — query with single quote
// ---------------------------------------------------------------------------

TEST_CASE("DatabaseManager::SearchArticles with single-quote query", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art1 = make_article(
        "https://arxiv.org/abs/2403.00011",
        "O'Brien's theorem revisited",
        "O'Brien, Patrick",
        "A study of O'Brien's original conjecture.");
    Article art2 = make_article(
        "https://arxiv.org/abs/2403.00012",
        "Unrelated paper",
        "Smith, Alice",
        "Nothing relevant here.");
    db.AddArticle(art1);
    db.AddArticle(art2);

    auto results = db.SearchArticles("O'Brien", true, true, true);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].title == "O'Brien's theorem revisited");
}

TEST_CASE("DatabaseManager::GetProjectsForArticle with special-char link", "[db][integration][sql]") {
    DatabaseManager db(":memory:");

    Article art = make_article(
        "https://arxiv.org/abs/it's-2403.00013",
        "Title",
        "Author",
        "Abstract");
    db.AddArticle(art);
    db.AddProject("TestProject");
    db.LinkArticleToProject(art.link, "TestProject");

    auto projs = db.GetProjectsForArticle(art.link);
    REQUIRE(projs.size() == 1);
    REQUIRE(projs[0] == "TestProject");
}
