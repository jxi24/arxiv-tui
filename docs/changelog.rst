.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Changelog
=========

Unreleased
----------

- **Invalid UTF-8 no longer crashes the app** — Backspace in every text field
  removed one byte, so "é" then Backspace left half a character behind; the
  replay recorder then threw on it and the TUI exited. The same happened when
  exporting a project whose papers had invalid bytes from the feed. Backspace
  now removes a whole character, and the JSON writers replace invalid bytes
  with U+FFFD instead of throwing.

- **A very long link no longer crashes the app** — links in feeds and project
  imports were matched with ``std::regex``, which recurses once per character
  and overflowed the stack on a link of a few hundred kilobytes. Links longer
  than any real arXiv link (256 characters) are now refused before any regex
  runs, and CI fuzzes inputs up to 256 KiB (it used libFuzzer's 4 KiB default).

- **Crash reports start with a JSON header** — one line with the format
  marker, version, signal, UTC time, pid and replay-log path, written from
  the signal handler. ``--replay`` prints it and warns when the report came
  from another version. Reports from earlier versions still replay.

- **One replay path** — replay logs and crash reports are parsed into the
  same entries and replayed by the same loop, and the sandbox ``--replay``
  uses moved from ``main`` into the library (``ReplayPlayer::RunSandboxed``)
  where it is tested. Its scratch directory now has an unpredictable,
  private name (``mkdtemp``) instead of ``/tmp/arxiv-tui-replay-<pid>``.
  A replayed "link paper to project" no longer appends to the project's real
  exported ``.bib`` file or looks the paper up on InspireHEP: the sandbox has
  no network and no ``.bib`` paths.

- **GUI uses the TUI's files** — ``arxiv-gui`` read ``.arxiv-tui.yml`` and
  ``articles.db`` from the working directory (and logged there), so it never
  saw the TUI's papers. It now resolves config, database, ranker, keywords and
  log exactly like the TUI (``Paths::ApplyTo``), and CI builds it.

- **Mouse support** — click a view or an article to select it, and scroll
  views or articles with the wheel. The article list draws its own rows, so
  clicks there never selected anything, and clicks only reached the focused
  pane.

- **Richer detail pane** — under the link the detail pane shows the arXiv id,
  category and your star rating, and the paper's tags and projects when it
  has any.

- **Filter pane counts and sections** — Bookmarks, Unread, every tag and
  every project show how many papers they hold, and tags and projects sit
  under **Tags** / **Projects** headings instead of running on from the
  fixed views.

- **Footer hints** — the footer lists the keys that apply where you are: view
  and project keys in the filter pane, article actions in the list, selection
  actions when papers are selected — with your own key bindings.

- **Success messages no longer block** — "BibTeX copied", "Exported to …"
  and similar now appear for a few seconds in the bottom-right corner instead
  of a dialog that had to be dismissed (and swallowed the next key). Errors
  still open a dialog.

- **Status column** — selection (``+``) and bookmark (``★``) markers have
  their own column in the article list instead of being prefixed to titles,
  where the double-width ``⭐`` pushed titles out of line.

- **Layout follows the window** — pane widths and the number of article rows
  come from the area the TUI is drawn in rather than a separate terminal-size
  query, and the article list no longer lays out two rows more than fit (the
  cursor could sit on a hidden row at the bottom).

- **Bulk project assignment updates the project's BibTeX file** — assigning a
  selection to a project skipped the auto-update of an exported ``.bib`` that
  single assignment does, and single assignment only found papers shown in
  the current view.

- **Tagging from the TUI** — ``#`` opens a Tags dialog for the focused
  article or the selection: toggle tags, add new ones, delete one from every
  paper. Tags existed in the database and filter pane, but nothing in the TUI
  could create or apply them; a newly created tag now also appears in the
  filter pane at once.

- **Settings can be cancelled** — ``q`` closes the Settings dialog without
  saving (and undoes a theme preview); ``Esc`` while editing a field now
  abandons that edit instead of keeping it. ``Esc`` still saves and closes.

- **Dates are arXiv days everywhere** — the article list and exports showed
  dates in local time, so west of UTC a paper announced on 4 July showed as
  3 July; the date-range filter (``r``) used local midnights with DST off,
  missing papers on the first day and including some from the day after the
  range. Dates are now shown and filtered as UTC days.
- **Keyword boosts work in the TUI** — *Recommended*, the *New Articles*
  order and the score column used only the rating model, so keywords had no
  effect until you had rated papers (only ``--export-yaml`` used them). They
  now use the blended score, and saving keywords re-ranks the current view.
  In the keyword editor (``K``) every letter is text — ``d``, ``j`` and
  ``k`` used to delete or move the selection — and ``↑``/``↓`` select.
- **Exports no longer land in the current directory** — project exports, the
  daily digest and the BibTeX fallback file were written wherever arxiv-tui
  was started. They now go to the new ``export_dir`` (default
  ``~/.local/share/arxiv-tui/exports``, editable in Settings), and project
  names containing ``/`` no longer break the file name.
- **Database integrity** — the database now has a schema version and enforces
  its foreign keys: deleting a paper or project removes its memberships,
  ratings, notes and tags in the database itself. On first start the link
  tables are rebuilt once and leftover rows for papers that no longer exist
  are dropped. A database written by a newer arxiv-tui is refused instead of
  being modified.
- **Arrow keys in the filter and article lists** — moving the filter list
  with ``↑``/``↓`` changed the highlighted view without loading it (the old
  view's papers stayed on screen), and moving the article list that way did
  not mark papers read with the detail pane open or record the move for
  replay. Both lists now behave exactly like ``j``/``k``.
- **Retraining keeps keywords** — a full retrain (``R``) dropped the interest
  keywords until the next start, so keyword boosts silently stopped working.
- **``--help`` and ``--version``** — and unknown options, a missing file name
  or two modes at once (``--fetch`` with an export, say) are now errors
  instead of being silently ignored.
- **Reproducible builds** — FTXUI is pinned to the v7.0.3 release instead of
  tracking its ``main`` branch.
- **``replay.jsonl`` no longer grows forever** — when it is over 5 MiB at
  start-up it is moved to ``replay.jsonl.1`` and a new log is started.

- **Polite to arXiv** — every request identifies itself
  (``arxiv-tui/<version> (+https://github.com/jxi24/arxiv-tui)``), and the
  backfill after missed days waits 3 s between arXiv API pages, as the API
  terms of use ask.

- **No more half-written files** — ``config.yml``, ``ranker.bin``, the
  keywords file, exports, Obsidian notes and project ``.bib`` files are
  written to a temporary file and renamed into place, so a full disk or a
  crash mid-write leaves the previous version instead of a truncated file.
  A symlinked ``config.yml`` stays a symlink. Obsidian export no longer
  overwrites a paper's existing note (and your reading notes in it), and a
  second export on the same day adds to that day's index instead of
  replacing it. Importing a project no longer overwrites papers already in
  the database (their title, authors and category were replaced by the
  file's, and the category emptied).

- **Security hardening** — a crash no longer hangs, restores the terminal
  and leaves ``SIGTERM`` alone; the crash report holds the end of
  ``replay.jsonl``. The clipboard backend is started without a shell
  (``ARXIV_TUI_CLIPBOARD`` names a program, not a command line). The
  InspireHEP lookup only sends real arXiv ids and only follows BibTeX links
  back to ``https://inspirehep.net/``. PDF downloads are streamed to a
  temporary file, must start with ``%PDF-`` and stay under 256 MiB, and
  replace the target only when complete — an HTML error page or a dropped
  connection no longer leaves a broken ``.pdf`` behind. ``--replay`` accepts a
  crash report as well as ``replay.jsonl`` (it never could: the report's
  text header stopped it at line 1), and runs on
  an in-memory copy of the database and skips exports, imports, downloads
  and keyword saves. Project import files over 64 MiB are refused.

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
  time out; links that are not arXiv abstract or PDF links
  (``https://arxiv.org/abs/<id>`` or ``.../pdf/<id>``) are rejected from
  feeds and imports, and PDF links are stored as their ``/abs/`` form so a
  paper is never kept twice; arXiv API error responses are no longer stored as
  articles; ``xdg-open`` and ``tar`` are started without a shell.
- **No more lost days** — a failed fetch no longer marks the missed days as
  fetched, so the next start retries the backfill. ``--fetch`` now uses the
  same backfill logic as the TUI, and ``--export-today``/``--export-yaml``/
  ``--replay`` no longer fetch or move the *New Articles* window.
- **Robust config** — a wrongly typed or out-of-range value in
  ``config.yml`` falls back to its default instead of crashing, and a pop-up
  at startup lists each one; an unparseable file runs on the defaults (and
  is left unchanged) with the same pop-up.
- **Data directories** — when the compiled-in install prefix is not
  writable (e.g. ``/usr/local``), data and state fall back to
  ``~/.local/share`` / ``~/.local/state`` instead of failing at startup.
- **Fixes** — the YAML digest is valid even when titles contain LaTeX;
  sub-projects at any depth (and projects whose parent was removed) show in
  the filter pane; the category filter and followed authors match whole
  names (``math`` no longer matches ``math-ph``); followed authors match
  whole names with first and optional middle initials (``W. Li`` matches
  *Wei Li* but not *Xiao Li* or *Wei Lin*); re-importing a project keeps it nested; deleting an
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
- **Follow authors from the TUI** — ``f`` opens a *Followed Authors* dialog
  with the focused article's authors and your followed list; ``+`` adds a
  typed name. Names without a first initial are flagged and need a second
  Enter, since they match everyone with that last name.
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
