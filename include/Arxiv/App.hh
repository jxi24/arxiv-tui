// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARXIV_APP
#define ARXIV_APP

#include "Arxiv/AppCore.hh"
#include "Arxiv/KeyBindings.hh"
#include "Arxiv/Replay.hh"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using ftxui::Color;
using ftxui::Component;

namespace Arxiv {

/// Identifies which (if any) modal dialog is active. Replaces a previous
/// integer "dialog_depth" with a typed enumeration so the compiler catches
/// stale literals and so the meaning of each branch is self-documenting.
enum class Dialog {
    None = 0,
    NewProject = 1,
    AssignProject = 2,
    Error = 3,
    DateRange = 4,
    Search = 5,
    Rating = 6,
    Notes = 7,
    Export = 8,
    Import = 9,
    KeywordEditor = 10,
    CategoryFilter = 11,
    Settings = 13,
    CitationBibtex = 14,
    ConfirmDelete = 15,
    ConfigWarning = 16,
    FollowAuthors = 17,
    Tags = 18,
    Rename = 19,
    MoveProject = 20,
};

class ArxivApp {
  public:
    explicit ArxivApp(const Config& config,
                      const std::string& config_path = "",
                      ReplayRecorder* recorder = nullptr);
    void Run() { screen.Loop(event_handler); }
    ~ArxivApp() {
        refresh_ui_continue = false;
        if (refresh_ui.joinable())
            refresh_ui.join();
    }

#ifdef TESTING
    // Test-only constructor: accepts injected DB/Fetcher so mocks can be used.
    // Does not launch the refresh thread; use GetEventHandler() to drive events.
    ArxivApp(Config cfg,
             std::unique_ptr<DatabaseManager> db,
             std::unique_ptr<Fetcher> fetcher,
             KeyBindings kb,
             const std::string& config_path = "");
    Component GetEventHandler() const { return event_handler; }
    AppCore& GetCore() { return core; }
#endif

  private:
    // Core application logic
    AppCore core;
    KeyBindings key_bindings;
    std::string m_config_path;
    Config m_config;
    Config m_settings_on_open; // what q in the Settings dialog restores

    // UI handling
    ftxui::ScreenInteractive screen;
    ReplayRecorder* m_recorder = nullptr; // not owned — initialized in ctor body order
    int focused_pane = 0;
    Dialog dialog_depth = Dialog::None;
    // Set while ConfirmDelete asks about a project rather than articles.
    std::string m_pending_project_delete;
    std::string new_project_name;
    std::string parent_for_new_project;
    bool show_detail = false;
    bool show_help = false;
    std::string help_search_query;
    std::set<std::string> selected_projects;
    int selected_project_index = 0;
    std::map<std::string, bool> checkbox_states;
    float title_start_position = 0;
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
    std::atomic<bool> refresh_ui_continue = true;
    std::thread refresh_ui;
    std::string err_msg = "";
    // A short notice in the bottom-right corner that goes away by itself
    // and never takes a key (success messages; errors stay dialogs).
    void ShowToast(std::string message);
    // (key, what it does) pairs for the footer, for where the focus is.
    std::vector<std::pair<std::string, std::string>> FooterHints() const;
    bool ToastVisible() const;
    std::string m_toast;
    std::chrono::steady_clock::time_point m_toast_until{};
    static constexpr std::chrono::seconds kToastDuration{5};
    std::string bibtex_content;
    static constexpr int arrow_size = 2;
    static constexpr int padding = 4;
    static constexpr float scroll_speed = 4.0f; // Scroll speed in characters per second

    // Date range dialog
    enum class DateInputMode { Start, End };
    DateInputMode date_input_mode = DateInputMode::Start;
    std::string start_date;
    std::string end_date;
    Component date_range_dialog;

    // Search dialog
    std::string search_query;
    AppCore::SearchMode search_field = AppCore::SearchMode::title; // Default to searching in title
    bool search_area_selection = false; // false: text entry, true: search-area selection
    int search_area_index = 0;          // cursor in area mode: 0 title, 1 authors, 2 abstract
    Component search_dialog;

    // Rating dialog
    int pending_rating = 0;     // 1-5 chosen by user
    bool m_bulk_rating = false; // true = rate selection, false = rate focused article
    Component rating_dialog;

    // Notes dialog
    std::string note_edit_text;
    std::string note_project_name; // project context when editing
    std::string note_article_link; // article being annotated
    Component note_dialog;

    // Export dialog
    int export_format_index = 0; // 0=Markdown 1=Text 2=JSON
    std::string export_project_name;
    Component export_dialog;

    // Import dialog
    std::string import_path;
    Component import_dialog;

    // Keyword editor dialog
    std::vector<std::string> keyword_edit_list;
    std::string keyword_new_entry;
    int keyword_selected_index = 0;
    Component keyword_dialog;

    // Followed-authors dialog
    struct FollowRow {
        std::string name;
        bool from_article; // an author of the focused article (else: a followed entry)
        bool followed;     // this exact name is in the followed list
    };
    std::vector<FollowRow> FollowRows() const;
    void RequestFollow(const std::string& name);
    int follow_selected_index = 0;
    bool follow_adding = false;      // typing a name to add
    std::string follow_new_name;     // the name being typed
    std::string follow_pending_name; // awaiting confirmation (no first initial)
    Component follow_dialog;
    int tag_selected_index = 0;
    bool tag_adding = false;        // typing a new tag
    std::string tag_new_name;       // the tag being typed
    std::string tag_pending_delete; // awaiting confirmation
    Component tags_dialog;

    // Category filter dialog
    int category_selected_index = 0;
    Component category_dialog;

    // Settings dialog
    int settings_section = 0;         // 0=General 1=Topics 2=Ranker 3=Export 4=Keys
    int settings_field_index = 0;     // selected field within section
    bool settings_editing = false;    // true while text field is being edited
    std::string settings_edit_buffer; // scratch buffer for the current field
    Component settings_dialog;

    // Article pane scrolling
    int visible_rows = 0; // Number of rows visible in the article pane
    // Where the article pane and the whole screen were drawn last frame
    // (FTXUI reflect): layout follows the real drawing area, which is not
    // Terminal::Size() when rendering to a smaller or test screen.
    ftxui::Box m_article_box;
    ftxui::Box m_filter_box;
    ftxui::Box m_screen_box;
    // Size of the drawing area last frame (the terminal before the first).
    int ScreenWidth() const;
    int ScreenHeight() const;
    int top_article_index = 0; // Index of the article at the top of the visible area

    // Components
    Component filter_menu;
    Component filter_pane;
    Component article_list;
    Component article_pane;
    Component detail_view;
    Component main_container;
    Component main_renderer;
    Component event_handler;
    Component help_dialog;

    // Project dialog components
    Component project_checkbox_container;
    Component project_dialog;

    // Helper functions
    void SetupUI();
    void RefreshUI();
    int FilterPaneWidth();
    void UpdateTitleScrollPositions();
    void UpdateVisibleRange();
    void ToggleHelp();

    // View setup (called from SetupUI)
    void SetupFilterPane();
    void SetupArticlePane();
    // Every change of view or article goes through these (keys, menus,
    // mouse), so AppCore's setters, the replay log and read-marking always
    // run. The menus edit UI-owned copies of the indices and call them.
    void SelectFilter(int index);
    void SelectArticle(int index);
    int filter_menu_index = 0;
    int article_menu_index = 0;
    void SetupDetailView();
    void SetupHelpDialog();
    void SetupAssignProjectDialog();
    void SetupDateRangeDialog();
    void SetupSearchDialog();
    void SetupRatingDialog();
    void SetupNotesDialog();
    void SetupExportDialog();
    void SetupImportDialog();
    void SetupKeywordEditorDialog();
    void SetupFollowAuthorsDialog();
    void SetupTagsDialog();
    void SetupCategoryFilterDialog();
    void SetupSettingsDialog();
    void SetupMainRenderer();
    // Shows why a background task did not start (busy, nothing selected);
    // returns `started`.
    bool StartTaskOrExplain(bool started, bool needs_selection);
    // What to show instead of an empty article list, per view.
    std::string EmptyViewMessage() const;
    void SetupEventHandler();

    // Event handlers (called from SetupEventHandler dispatch or HandleGlobalEvent)
    bool HandleHelpEvent(ftxui::Event event);
    ftxui::Element RenderNewProjectDialog();
    bool HandleNewProjectEvent(ftxui::Event event);
    // Rename the selected project/tag (E), move a project (M): RenameDialog.cc
    void OpenRenameDialog();
    ftxui::Element RenderRenameDialog();
    bool HandleRenameEvent(ftxui::Event event);
    void OpenMoveProjectDialog();
    ftxui::Element RenderMoveProjectDialog();
    bool HandleMoveProjectEvent(ftxui::Event event);
    std::string rename_target;
    std::string rename_buffer;
    bool rename_is_tag = false;
    std::string move_project;
    std::vector<std::string> move_candidates;
    int move_index = 0;
    bool HandleAssignProjectEvent(ftxui::Event event);
    bool HandleDateRangeEvent(ftxui::Event event);
    bool HandleSearchEvent(ftxui::Event event);
    bool HandleRatingEvent(ftxui::Event event);
    bool HandleNotesEvent(ftxui::Event event);
    bool HandleExportEvent(ftxui::Event event);
    bool HandleImportEvent(ftxui::Event event);
    bool HandleKeywordEditorEvent(ftxui::Event event);
    bool HandleFollowAuthorsEvent(ftxui::Event event);
    bool HandleTagsEvent(ftxui::Event event);
    bool HandleCategoryFilterEvent(ftxui::Event event);
    bool HandleSettingsEvent(ftxui::Event event);
    bool HandleGlobalEvent(ftxui::Event event);
    // The open dialog's keys; it is modal, so this consumes every key.
    bool HandleDialogEvent(ftxui::Event event);
    // Clicks and the wheel over the filter pane and the article list.
    bool HandleMouseEvent(ftxui::Event event);
};

} // namespace Arxiv

#endif
