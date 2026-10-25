.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Changelog
=========

Unreleased
----------

- **Exports and downloads no longer freeze the TUI** — ``o`` (Obsidian),
  ``g`` (selected digest), ``G`` (digest archive) and ``d`` (download PDF)
  run in the background. A badge in the bottom-right corner shows progress
  (``Exporting to Obsidian 1/3…``) and then the outcome; you can keep
  browsing meanwhile. One export runs at a time.
- **Settings apply immediately** — saving the Settings dialog now applies
  the new values (and key bindings) to the running session, and reopening it
  shows what you saved. Topics still take effect on the next start. Letters
  such as ``j``/``k`` can now be typed into settings fields.
- **Auto-refresh works** — ``auto_refresh_minutes`` now periodically fetches
  new articles; the list is refreshed without losing your place.
- **Safer fetching** — the RSS feed is fetched over HTTPS; network requests
  time out; links that are not ``https://arxiv.org/abs/<id>`` are rejected
  from feeds and imports; arXiv API error responses are no longer stored as
  articles; ``xdg-open`` and ``tar`` are started without a shell.
- **No more lost days** — a failed fetch no longer marks the missed days as
  fetched, so the next start retries the backfill. ``--fetch`` now uses the
  same backfill logic as the TUI, and ``--export-today``/``--export-yaml``/
  ``--replay`` no longer fetch or move the *New Articles* window.
- **Robust config** — a wrongly typed or out-of-range value in
  ``config.yml`` falls back to its default with a warning instead of
  crashing; an unparseable file is reported clearly.
- **Data directories** — when the compiled-in install prefix is not
  writable (e.g. ``/usr/local``), data and state fall back to
  ``~/.local/share`` / ``~/.local/state`` instead of failing at startup.
- **Fixes** — the YAML digest is valid even when titles contain LaTeX;
  sub-projects at any depth (and projects whose parent was removed) show in
  the filter pane; the category filter and followed authors match whole
  names (``math`` no longer matches ``math-ph``, ``Wei Li`` no longer
  matches ``Wei Lin``); re-importing a project keeps it nested; deleting an
  article removes its tags and pruning keeps tagged articles; undo restores
  selected articles outside the current view and keeps the read state;
  old-style IDs such as ``hep-ph/0101001`` work for PDFs, BibTeX and
  Obsidian notes; a corrupt ``ranker.bin`` is rejected instead of crashing;
  malformed import or replay files are reported instead of aborting.
- **Found by fuzzing** — pruning no longer deletes articles that carry a
  project note (the note was left orphaned); text containing a NUL byte is
  stored in full; link normalisation only strips real version suffixes;
  out-of-range dates in feeds are rejected instead of overflowing; a corrupt
  stored date no longer aborts startup.
- **Themes** — ``theme`` selects ``frappe`` (default), ``latte``,
  ``mocha``, ``high-contrast`` or ``terminal``; ``theme_colors`` overrides
  individual colours. Settings → General → Theme cycles them live.
- **Easier to find your way** — the help overlay (``?``) groups bindings by
  task (Navigation, Articles, Selection, Projects, Export & share, Ranking,
  App) with the key first and nothing cut off, and warns about keys bound to
  two actions. The unused *Generate BibTeX* binding, which shared ``c``
  with *Export BibTeX*, is gone. Empty views say what to do next ("No
  bookmarks yet — press b …"). With many list columns the title keeps a
  readable width (optional columns are hidden from the right), and the
  footer drops the topic list instead of overlapping on narrow terminals.
- **Smoother under load** — the database uses WAL journaling and waits for
  a busy lock instead of failing, so a cron ``--fetch`` and the TUI can run
  together; bookmarking or marking read no longer rewrites the search index;
  lists are ordered newest first; the idle TUI no longer redraws 20 times a
  second; fuzzy search is much faster on long abstracts and matches a typo
  that drops a letter.
- **BibTeX** — clipboard and file exports produce the same entry; ``%``,
  ``&`` and ``#`` are escaped and braces balanced; the year is the UTC year;
  re-linking an article no longer appends it to the project ``.bib`` again.
- **Fixes** — exports report failure when the disk is full; adding a tag no
  longer switches the view to a different project; removing the project you
  are viewing switches to New Articles; search input with control
  characters no longer errors.
- **Builds with Clang** — fixed a ``class``/``struct`` mismatch that Clang
  rejects, and cpr's own ``-Werror`` no longer trips on libstdc++ 13.

- **Obsidian export without PDFs** — ``o`` no longer downloads each PDF into
  the vault. Notes link to the PDF on arXiv instead
  (``[PDF on arXiv](https://arxiv.org/pdf/<id>)``), keeping git-synced vaults
  small. Set ``obsidian_download_pdfs: true`` (or toggle *Download PDFs* in
  the Settings dialog's Export section) to restore the old behaviour of
  downloading and embedding them.

v0.9.4
------

- **Undo delete** — press ``u`` to restore the last deleted article or
  bulk-deleted selection. The full article state is restored: the row
  itself, its star rating, all project memberships with per-article notes,
  and all tags. Backed by a ring buffer in memory; the buffer size is
  configurable via ``undo_buffer_size`` in ``config.yml`` (default: 10).
  Changing the capacity at runtime clears existing history.

v0.9.3
------

- **Auto-refresh race fix** — eliminated a TSan-detected lost-wakeup race
  in the background refresh thread that caused the thread to block for the
  full timer interval instead of waking up promptly on ``StopAutoRefresh``.
- **Project logo** — SVG logo added to ``assets/`` and wired into the
  documentation site.

v0.9.2
------

- **Documentation site** — full Sphinx docs published to GitHub Pages,
  covering installation, configuration, key bindings, features, the ranking
  system, contributing guide, and changelog. Versioned by git tag; the root
  URL redirects to the latest release. Built automatically on every push to
  ``main`` and on every ``v*`` tag.

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
