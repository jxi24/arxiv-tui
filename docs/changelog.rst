.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Changelog
=========

v0.9.1
------

- **Read-on-scroll** — articles are now marked read whenever the user
  navigates (``j``/``k``) while the detail pane is open, not only when the
  pane is first opened.
- **Bulk rating** — pressing ``n`` with a selection active opens a
  *Rate Selection* dialog and applies the score to all selected articles in
  one operation.

v0.9
----

- **Clipboard integration** — ``c`` copies BibTeX directly to the system
  clipboard via ``xclip``, ``xsel``, or ``wl-clipboard``; ``ARXIV_TUI_CLIPBOARD``
  env var selects the backend.
- **FTS5 full-text search** — SQLite FTS5 replaces ``LIKE '%query%'`` with
  ranked, stemmed full-text search over titles, authors, and abstracts.
- **Tag system** — user-defined labels outside the project hierarchy;
  articles can carry multiple tags; tags appear as filters and are included
  in BibTeX exports.
- **Auto-update project .bib** — adding an article to a project that has a
  previously exported ``.bib`` automatically appends the new entry without a
  manual re-export.
- **Bulk rating** — ``n`` with a selection active opens a *Rate Selection*
  dialog and applies the chosen score to all selected articles, triggering a
  single model retrain check.

v0.8
----

- **Read/unread tracking** — ``read_at`` timestamp recorded when the detail
  pane opens, when navigating with the detail pane open, or when a PDF
  downloads; **Unread** filter shows unseen articles; read articles render
  dimmer.
- **``--fetch`` headless mode** — ``arxiv-tui --fetch`` updates the
  database and exits without opening the TUI, enabling cron-based refresh.
- **Database pruning** — ``max_article_age_days`` config key automatically
  removes old unprotected articles on startup.

v0.7
----

- **Multi-article selection and bulk actions** — ``Space`` to select;
  bulk bookmark, project-assign, and delete.
- **Article deletion** — delete individual articles or entire selections;
  all associated ratings, notes, and project memberships are removed.
- **Link deduplication** — arXiv links are normalised to canonical form on
  ingestion; a one-time startup migration merges existing duplicates.

v0.6
----

- **Auto-refresh** — configurable background feed refresh interval.
- **Scroll margin** — configurable context lines kept visible around the
  selected article.
- **Fuzzy search** — in-process fuzzy matching in the search filter.
- **Author subscriptions** — follow specific authors alongside category feeds.

v0.5
----

- **BibTeX export** — ``.bib`` generation for individual articles, selections,
  or entire projects; InspireHEP API lookup with metadata fallback.
- **Replay system and crash handler** — JSONL action recording, ``--replay``
  headless mode, signal-based crash reports with backtrace.

v0.4
----

- **Hierarchical sub-projects** — nest projects arbitrarily deep.
- **Per-article notes** — notes scoped to a project.
- **Export/import** — Markdown, plain text, JSON export; JSON import.

v0.3
----

- **Personalised ranking** — TF-IDF + MLP model learns from 1–5 star
  ratings; *Recommended* filter surfaces relevant papers; warm-start
  retraining with persistence to ``ranker.bin``.
