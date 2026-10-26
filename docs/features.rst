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

Today's articles can also be exported without the TUI. ``--export-yaml``
writes them ranked by predicted score (the trained model blended with the
interest keywords), highest first, with ``id``, ``title``, ``authors``,
``link``, ``category``, ``score`` and ``abstract`` per article, so another
tool can take the top few as-is (``--export-today`` writes the Markdown
digest):

.. code-block:: bash

   arxiv-tui --fetch && arxiv-tui --export-yaml ~/notes/arxiv-today.yml

Set ``auto_refresh_minutes`` in the config to have the background thread
re-fetch feeds automatically while the TUI is open.

Filtering
---------

The left pane lists all available filters. Switch between them with ``h``/``l``
to move focus to the filter pane, then ``j``/``k`` to select. Bookmarks,
Unread, each tag and each project show how many papers they hold (the whole
database, before any category filter), and tags and projects are listed under
their own **Tags** and **Projects** headings:

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

In addition to category feeds, you can follow specific authors. Press ``f``
to open the *Followed Authors* dialog: it lists the focused article's authors
(Enter follows or unfollows one) and everyone you follow (Enter or ``d``
unfollows); press ``+`` to type any name. The *Followed Authors* filter
shows their papers.

A name without a first initial (just ``Li``) would match every author with
that last name, so the dialog warns and asks for a second Enter before
following it, and flags such entries in the list.

Names are matched whole, using initials to tell apart authors who share a
common last name:

- the last names must be identical (``Li`` does not match ``Lin``; particles
  such as ``van der`` are part of the last name);
- the first initials must agree, and the first names too when both are spelled
  out — following ``W. Li`` matches *Wei Li* but not *Xiao Li*, and
  ``Wei Li`` does not match *Wen Li*;
- a middle initial only counts when both names have one — ``J. A. Smith``
  matches *John Smith* and *J. A. Smith* but not *J. B. Smith*;
- following a last name alone (``Li``) matches every author with that last
  name.

Matching is case-insensitive and accepts initials with or without dots
(``J.A. Smith``, ``J A Smith``).

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
``~/.local/state/arxiv-tui/replay.jsonl``. On a crash the terminal is
restored and a report with a backtrace and the end of the replay log is
written to ``~/.local/state/arxiv-tui/crash_*.txt``.

To reproduce a crash, replay the crash report (its replay log is replayed)
or the full log:

.. code-block:: bash

   arxiv-tui --replay ~/.local/state/arxiv-tui/crash_20260101_120000_SIGSEGV_1234.txt
   arxiv-tui --replay ~/.local/state/arxiv-tui/replay.jsonl

A crash report carries the last 64 KiB of the log, which covers the actions
leading up to the crash; replay ``replay.jsonl`` for the whole history. Both
are read and replayed by the same code, so they give the same result for the
same actions.

The first line of a report is a one-line JSON header for tools and scripts::

   {"arxiv_tui_crash_report":1,"version":"1.1.0","signal":"SIGSEGV","signum":11,
    "time":"2026-10-26T05:09:35Z","pid":2745,"replay_log":"/home/you/.local/state/arxiv-tui/replay.jsonl"}

(on one line in the file). The backtrace and the replay log follow as text.
``--replay`` prints the signal, time and version from the header, and warns
when the report came from a different version than the one replaying it.

A replay never changes your data: it runs on an in-memory copy of the
database (the ranker and keywords are copied to a scratch directory), and
actions that write files or download — exports, imports, PDF downloads,
saving keywords — are skipped and counted as such. A replay log from
someone else's bug report is therefore safe to run.

Database pruning
----------------

Set ``max_article_age_days`` in ``config.yml`` to automatically remove old
articles on startup. Articles are exempt from pruning if they are bookmarked,
rated, or assigned to a project. Set the value to ``0`` (the default) to
disable pruning entirely.

Tags
----

Press ``#`` to tag the focused article, or every selected article. The dialog
lists your tags with ``[x]`` (all targets have it), ``[-]`` (some do) or
``[ ]``; ``Enter`` or ``Space`` adds the tag to all targets, or removes it
when they all have it. ``+`` creates a new tag and applies it; ``Del`` deletes
a tag from every paper after asking. Each tag gets a ``#name`` entry in the
filter pane.

Undo delete
-----------

Press ``u`` to restore the most recently deleted article (or bulk-deleted
selection). The full state is restored:

- The article row itself (title, authors, abstract, date, category, bookmark flag)
- Its star rating, if it had one
- All project memberships, with the per-article note for each project
- All tags

Undo history is held in a ring buffer in memory. When the buffer is full, the
oldest entry is silently evicted to make room for the new one. The buffer size
is configurable:

.. code-block:: yaml

   undo_buffer_size: 10   # default; set to 0 to disable undo entirely

Changing ``undo_buffer_size`` at runtime (via the settings dialog) clears the
existing history. The buffer is session-scoped — it does not persist across
restarts.
