// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AppCore.hh"
#include "Arxiv/Config.hh"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "fixtures/test_data.hh"
#include "mocks/DatabaseManagerMock.hh"
#include "mocks/FetcherMock.hh"

using namespace Arxiv;
using namespace arxiv_tui::test;
using namespace arxiv_tui::test::fixtures;

// ---------------------------------------------------------------------------
// Helper — does NOT set up GetRecent; each test must do that in its own scope
// so the expectation survives the function return.
// ---------------------------------------------------------------------------

static std::unique_ptr<AppCore> make_undo_core(DatabaseManagerMock*& db_out,
                                               std::size_t undo_capacity = 10) {
    auto db = std::make_unique<DatabaseManagerMock>();
    auto fetcher = std::make_unique<FetcherMock>();
    db_out = db.get();

    Config cfg;
    cfg.set_topics({"cs.AI"});
    cfg.set_download_dir("/tmp");
    cfg.set_undo_buffer_size(undo_capacity);
    return std::make_unique<AppCore>(cfg, std::move(db), std::move(fetcher));
}

// ---------------------------------------------------------------------------
// Config::undo_buffer_size
// ---------------------------------------------------------------------------

TEST_CASE("Config::undo_buffer_size: defaults to 10", "[undo][config]") {
    Config cfg;
    REQUIRE(cfg.get_undo_buffer_size() == 10);
}

TEST_CASE("Config::set_undo_buffer_size: getter returns set value", "[undo][config]") {
    Config cfg;
    cfg.set_undo_buffer_size(5);
    REQUIRE(cfg.get_undo_buffer_size() == 5);
}

// ---------------------------------------------------------------------------
// CanUndo / UndoLastDelete: basic behaviour
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::CanUndo: false before any delete", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    REQUIRE_FALSE(core->CanUndo());
}

TEST_CASE("AppCore::CanUndo: true after DeleteCurrentOrSelected", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));

    core->FetchArticles();
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected();

    REQUIRE(core->CanUndo());
}

TEST_CASE("AppCore::UndoLastDelete: restores the deleted article", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));

    bool article_restored = false;
    ALLOW_CALL(*db, AddArticle(trompeloeil::_)).LR_SIDE_EFFECT(article_restored = true);

    core->FetchArticles();
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected();
    core->UndoLastDelete();

    REQUIRE(article_restored);
}

TEST_CASE("AppCore::UndoLastDelete: CanUndo is false after restoring last entry",
          "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));
    ALLOW_CALL(*db, AddArticle(trompeloeil::_));

    core->FetchArticles();
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected();
    core->UndoLastDelete();

    REQUIRE_FALSE(core->CanUndo());
}

TEST_CASE("AppCore::UndoLastDelete: safe to call when buffer is empty", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    REQUIRE_NOTHROW(core->UndoLastDelete());
}

// ---------------------------------------------------------------------------
// LIFO ordering
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::UndoLastDelete: restores in LIFO order", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    // GetRecent must stay alive for the full test so FetchArticles always
    // repopulates m_current_articles between deletes.
    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));

    core->FetchArticles();

    // Delete article at index 0 twice to create two distinct undo entries.
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected(); // push entry A
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected(); // push entry B

    std::vector<std::string> restored_order;
    ALLOW_CALL(*db, AddArticle(trompeloeil::_)).LR_SIDE_EFFECT(restored_order.push_back(_1.link));

    core->UndoLastDelete(); // pops entry B
    core->UndoLastDelete(); // pops entry A

    REQUIRE(restored_order.size() == 2);
}

// ---------------------------------------------------------------------------
// Ring buffer wrapping
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::UndoLastDelete: ring wraps and evicts oldest entry", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    // Capacity of 2: only the last 2 deletes are undoable.
    auto core = make_undo_core(db, 2);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));
    ALLOW_CALL(*db, AddArticle(trompeloeil::_));

    core->FetchArticles();

    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected(); // entry 1 — will be evicted
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected(); // entry 2
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected(); // entry 3

    // Only 2 undos should be available.
    REQUIRE(core->CanUndo());
    core->UndoLastDelete();
    REQUIRE(core->CanUndo());
    core->UndoLastDelete();
    REQUIRE_FALSE(core->CanUndo());
}

// ---------------------------------------------------------------------------
// GetUndoCapacity / SetUndoCapacity
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::GetUndoCapacity: returns value from config", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db, 7);
    REQUIRE(core->GetUndoCapacity() == 7);
}

TEST_CASE("AppCore::SetUndoCapacity: changes capacity and clears history", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db, 10);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));

    core->FetchArticles();
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected();
    REQUIRE(core->CanUndo());

    core->SetUndoCapacity(5);

    REQUIRE(core->GetUndoCapacity() == 5);
    REQUIRE_FALSE(core->CanUndo()); // history cleared on capacity change
}

// ---------------------------------------------------------------------------
// Bulk delete is one undo step
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::UndoLastDelete: bulk delete is one undo step", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));

    core->FetchArticles();
    core->ToggleSelection(sample_articles[0].link);
    core->ToggleSelection(sample_articles[1].link);
    core->DeleteCurrentOrSelected();

    REQUIRE(core->CanUndo());

    int restored_count = 0;
    ALLOW_CALL(*db, AddArticle(trompeloeil::_)).LR_SIDE_EFFECT(++restored_count);

    core->UndoLastDelete();

    REQUIRE(restored_count == 2);
    REQUIRE_FALSE(core->CanUndo());
}

// ---------------------------------------------------------------------------
// Snapshot captures rating
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::UndoLastDelete: restores non-zero rating", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, GetRating(sample_articles[0].link)).RETURN(4);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));
    ALLOW_CALL(*db, AddArticle(trompeloeil::_));

    int restored_rating = 0;
    ALLOW_CALL(*db, SetRating(sample_articles[0].link, trompeloeil::_))
        .LR_SIDE_EFFECT(restored_rating = _2);

    core->FetchArticles();
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected();
    core->UndoLastDelete();

    REQUIRE(restored_rating == 4);
}

TEST_CASE("AppCore::UndoLastDelete: does not call SetRating for unrated article",
          "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, GetRating(sample_articles[0].link)).RETURN(0);
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));
    ALLOW_CALL(*db, AddArticle(trompeloeil::_));

    bool rating_called = false;
    ALLOW_CALL(*db, SetRating(trompeloeil::_, trompeloeil::_)).LR_SIDE_EFFECT(rating_called = true);

    core->FetchArticles();
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected();
    core->UndoLastDelete();

    REQUIRE_FALSE(rating_called);
}

// ---------------------------------------------------------------------------
// Snapshot captures project memberships and notes
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::UndoLastDelete: restores project membership", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, GetProjectsForArticle(sample_articles[0].link))
        .RETURN(std::vector<std::string>{"proj-alpha"});
    ALLOW_CALL(*db, GetProjectNote("proj-alpha", sample_articles[0].link)).RETURN("my note");
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));
    ALLOW_CALL(*db, AddArticle(trompeloeil::_));

    std::string relinked_project;
    ALLOW_CALL(*db, LinkArticleToProject(sample_articles[0].link, trompeloeil::_))
        .LR_SIDE_EFFECT(relinked_project = _2);

    std::string restored_note;
    ALLOW_CALL(*db, SetProjectNote("proj-alpha", sample_articles[0].link, trompeloeil::_))
        .LR_SIDE_EFFECT(restored_note = _3);

    core->FetchArticles();
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected();
    core->UndoLastDelete();

    REQUIRE(relinked_project == "proj-alpha");
    REQUIRE(restored_note == "my note");
}

// ---------------------------------------------------------------------------
// Snapshot captures tags
// ---------------------------------------------------------------------------

TEST_CASE("AppCore::UndoLastDelete: restores tags", "[undo][appcore]") {
    DatabaseManagerMock* db = nullptr;
    auto core = make_undo_core(db);

    ALLOW_CALL(*db, GetRecent(trompeloeil::_)).RETURN(sample_articles);
    ALLOW_CALL(*db, GetTagsForArticle(sample_articles[0].link))
        .RETURN(std::vector<std::string>{"hep-ph", "interesting"});
    ALLOW_CALL(*db, DeleteArticle(trompeloeil::_));
    ALLOW_CALL(*db, AddArticle(trompeloeil::_));

    std::vector<std::string> relinked_tags;
    ALLOW_CALL(*db, LinkArticleToTag(sample_articles[0].link, trompeloeil::_))
        .LR_SIDE_EFFECT(relinked_tags.push_back(_2));

    core->FetchArticles();
    core->SetArticleIndex(0);
    core->DeleteCurrentOrSelected();
    core->UndoLastDelete();

    REQUIRE(relinked_tags.size() == 2);
    REQUIRE(std::find(relinked_tags.begin(), relinked_tags.end(), "hep-ph") != relinked_tags.end());
    REQUIRE(std::find(relinked_tags.begin(), relinked_tags.end(), "interesting") !=
            relinked_tags.end());
}
