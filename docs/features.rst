.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Features
========

Feed fetching and persistence
------------------------------

arxiv-tui pulls articles from the arXiv RSS feeds for every category listed
in ``config.yml`` under ``article_settings.topics``. Articles are stored in
a local SQLite database so they remain available offline after the initial
fetch.

Use ``--fetch`` to update the database without opening the TUI — suitable for
a cron job:

.. code-block:: bash

   arxiv-tui --fetch
   # or schedule with cron
   0 7 * * 1-5  arxiv-tui --fetch

Set ``auto_refresh_minutes`` in the config to have the background thread
re-fetch feeds automatically while the TUI is open.

Filtering
---------

The left pane lists all available filters. Switch between them with ``h``/``l``
to move focus to the filter pane, then ``j``/``k`` to select:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Filter
     - Shows
   * - **All**
     - Every article in the database
   * - **Bookmarks**
     - Articles you have bookmarked
   * - **Today**
     - Articles submitted or updated today
   * - **Range**
     - Articles within a custom date range (press ``r`` to set the range)
   * - **Search**
     - Full-text search results (press ``/`` to enter a query)
   * - **Recommended**
     - Articles scoring at or above ``recommend_threshold`` according to the ranking model
   * - **Unread**
     - Articles not yet opened in the detail pane
   * - **<project name>**
     - Articles assigned to that project

Full-text and fuzzy search
--------------------------

Pressing ``/`` opens a search dialog. arxiv-tui uses SQLite's FTS5 extension
to search across titles, authors, and abstracts with stemming and relevance
ranking. Fuzzy matching surfaces near-miss results when an exact match is not
found.

Read/unread tracking
---------------------

An article is marked read when:

- the detail pane is opened on it,
- you navigate to another article while the detail pane is open, or
- a PDF is downloaded for it.

Read articles render dimmer in the article list. The **Unread** filter shows
only articles that have not been read yet.

Bookmarks
---------

Press ``b`` to toggle a bookmark on the current article. With a selection
active (``Space``), ``b`` bookmarks all selected articles at once.
Bookmarked articles are protected from the ``max_article_age_days`` pruning.

Projects
--------

Projects are named groups of articles. They support:

- **Hierarchical sub-projects** — nest projects arbitrarily deep.
- **Per-article notes** — press ``N`` to attach a note to an article scoped
  to the current project.
- **Export** — press ``e`` to export the project as Markdown, plain text,
  BibTeX, or JSON.
- **Import** — press ``I`` to import a project from a JSON file.
- **Auto-update .bib** — adding an article to a project that has a
  previously exported ``.bib`` file automatically appends the new BibTeX
  entry without a manual re-export.

Tags
----

Tags are user-defined labels that live outside the project hierarchy. An
article can carry multiple tags. Tags appear as filters alongside projects
in the filter pane and are included in BibTeX exports.

Author subscriptions
--------------------

In addition to category feeds, you can follow specific authors. Use the
settings dialog (``S``) to add or remove author subscriptions. Articles by
subscribed authors are ingested regardless of category.

BibTeX export
-------------

Press ``c`` on any article to copy its BibTeX entry to the system clipboard.
arxiv-tui first queries InspireHEP for rich metadata and falls back to the
arXiv-derived fields if the lookup fails.

To export BibTeX to a file, use the project export dialog (``e``), which
can produce a ``.bib`` for the entire project or for a selection of articles.

Multi-article selection and bulk actions
-----------------------------------------

Press ``Space`` to toggle the selection on the current article. While articles
are selected the article pane header shows ``[N selected]``. Bulk actions:

- ``b`` — bookmark all selected
- ``n`` — rate all selected with a single score
- ``p`` — assign all selected to projects
- ``D`` — delete all selected (with confirmation)
- ``g`` — export as a Markdown digest + PDF bundle
- ``o`` — export to an Obsidian vault

Clipboard integration
---------------------

``c`` copies BibTeX directly to the system clipboard using ``xclip``,
``xsel``, or ``wl-clipboard`` (probed in that order). Set
``ARXIV_TUI_CLIPBOARD`` to force a specific backend.

Replay system and crash handler
--------------------------------

All UI actions are recorded to a JSONL replay log at
``~/.local/state/arxiv-tui/replay.jsonl``. On a crash, a report containing
a backtrace and the full replay log is written to
``~/.local/state/arxiv-tui/crash_*.txt``.

To reproduce a crash:

.. code-block:: bash

   arxiv-tui --replay ~/.local/state/arxiv-tui/crash_20260101_120000_SIGSEGV.txt

Database pruning
----------------

Set ``max_article_age_days`` in ``config.yml`` to automatically remove old
articles on startup. Articles are exempt from pruning if they are bookmarked,
rated, or assigned to a project. Set the value to ``0`` (the default) to
disable pruning entirely.
