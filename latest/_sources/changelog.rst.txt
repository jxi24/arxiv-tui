.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Changelog
=========

v1.2.0
------

- **A corrupt ranker.bin can no longer produce NaN scores** — the model
  loader refused NaN and infinite values but accepted any finite one, and
  weights near the float maximum overflowed the scoring arithmetic, so every
  paper scored NaN. Values beyond 10⁶ (trained values are tiny) are now
  refused like any other corrupt model, which is retrained from your ratings.
  Found by the ``ranker_load`` fuzzer.

- **Quitting no longer waits for the network** — closing the app during the
  start-up fetch, a missed-days backfill, an auto-refresh or a PDF download
  used to wait for it to finish (a backfill pauses 3 s between API pages, and
  a stalled request could take its whole 15–120 s timeout). Quitting now
  abandons the request in flight; nothing half-fetched is recorded, so the
  next start fetches those days again.

- **Today over the weekend** — Today, Recommended and the daily digests
  (``X``, ``--export-today``, ``--export-yaml``) show the latest arXiv
  mailing instead of the last 24 hours. arXiv does not announce on weekends
  or holidays, so they used to be empty from Saturday morning until Monday's
  mailing; now they show Friday's papers until the next one arrives.

- **GUI restyle** — ``arxiv-gui`` has a dark theme with one accent colour,
  a system sans font (Inter, Noto Sans, Cantarell or DejaVu Sans;
  ``ARXIV_GUI_FONT`` overrides), a header with search and Refresh, a sidebar
  of views with counts, article cards (unread dot, bookmark star, category
  chip), a detail pane with clickable stars and action buttons, and
  keyboard navigation (``j``/``k``, ``/``, ``b``, ``o``). Greek, maths and
  accented characters in titles and author names render instead of ``?``.
  With a Nerd Font installed, views, buttons and menus also get icons
  (``ARXIV_GUI_ICON_FONT`` picks the font; empty turns them off).

- **GUI catches up** — ``arxiv-gui`` opens at once and fetches in the
  background (it used to fetch before showing the window), Refresh
  fetches from arXiv instead of re-reading the database, and downloads no
  longer freeze the window. Papers can be rated, tagged and added to
  projects; read papers are dimmed, the filter list shows counts and
  headings, and a status bar reports fetches, downloads and offline state.
  Its ``imgui.ini`` goes to the state directory, not the working directory.

- **Project JSON round-trips** — the export now carries the project's parent
  and each paper's category, tags and rating, and the import restores them
  (without overriding a rating you already gave). A paper without a date is
  dated at import instead of 1 January 1970, and a blank project name is
  refused. A new sub-project typed with spaces around its name now keeps
  its parent.

- **Rename and move** — ``E`` renames the project or tag selected in the
  filter pane (papers, notes, sub-projects and an exported ``.bib`` follow the
  new name); ``M`` moves a project under another one or back to the top
  level. A project cannot be moved under itself or its own sub-projects.

- **Clear the selection** — ``C`` deselects everything. The selection is
  kept when you switch views, so bulk actions could apply to papers no longer
  on screen and there was no way to drop it. The footer shows the key while
  papers are selected.

- **Bookmarks view updates** — un-bookmarking a paper in the Bookmarks view
  now removes it from the list.

- **Offline notice** — when arXiv cannot be reached (at start-up or on an
  auto refresh) the footer says "Offline: showing stored papers" until a
  fetch succeeds; before, the list just stayed as it was.

- **Accented names and titles are cut cleanly** — the article list
  truncated author names and other columns, and scrolled long titles, by
  byte, so a cut could fall inside a character (``Schr\xC3…``). Both now work
  by character.

- **Deleted papers stay deleted** — the next fetch brought a deleted paper
  back (as "new"). Deletions are now remembered and fetches skip them; ``u``
  restores a paper with the time it was first stored, so it is not new again,
  and its tags reappear in the filter pane even if the tag was deleted
  meanwhile. (Database schema version 2; older arxiv-tui versions refuse to
  open the upgraded database.)

- **Papers from a cron ``--fetch`` stay new** — a ``--fetch`` that crossed a
  day moved the New Articles window just like opening the TUI does, so with a
  daily cron job each run hid what the previous run had stored. ``--fetch``
  now leaves the window alone; it moves only when you open arxiv-tui.

- **Search** — asking for several fields at once (the GUI searches title,
  authors and abstract) searched titles only; every field asked for is now
  searched. When nothing matches exactly, near misses are shown (the fuzzy
  matching the docs described was never used). The docs no longer claim
  stemming, which the search index does not do.

- **InspireHEP answers are checked** — the BibTeX it returned went to the
  clipboard and into project ``.bib`` files whatever it was; an answer that
  does not start with an ``@`` entry or is larger than 64 KiB is now ignored
  (the paper's own BibTeX is used instead).

- **Only arXiv links are opened in the browser** — ``O`` passed whatever link
  a stored paper had to ``xdg-open``; rows from before links were checked
  could hold ``file://`` or ``javascript:`` links. Anything that is not an
  arXiv page is now skipped.

- **Feed text in the log is escaped** — a rejected feed link was logged as
  it came, so escape sequences in it ran in the terminal of anyone tailing
  the log and newlines in it forged log lines. Control characters are now
  shown as ``\xNN`` and long values are cut.

- **Obsidian notes: feed text stays inside its values** — the category went
  into the frontmatter unquoted, so a category with a newline could add keys
  or end the frontmatter; control characters made the YAML invalid. Every
  value (and each tag) is now quoted and control characters are dropped.

- **Network limits** — responses are capped at 64 MiB (a huge or endless
  answer no longer fills memory), redirects are followed at most five hops
  and only to ``https`` (the InspireHEP host check only saw the first URL),
  and a backfill stops with an error after 50 API pages (10,000 papers)
  instead of paging for as long as the API keeps answering.

- **Private files** — the replay log (searches, notes, keywords, paths), the
  log file and a new database (notes, ratings) are created readable by you
  only (mode 0600), and new data and state directories mode 0700; before
  they followed the umask (usually world-readable). Programs arxiv-tui starts
  (browser, ``tar``, the clipboard tool) no longer inherit its open files.
  Existing files and directories keep their permissions:
  ``chmod 600 ~/.local/share/arxiv-tui/articles.db ~/.local/state/arxiv-tui/*``
  tightens them.

- **The cursor stays where you were** — every reload of the list (auto
  refresh, a retrain finishing, tagging, deleting, undo) sent the cursor back
  to the first paper. It now stays on the same paper, or on the same row if
  that paper was deleted. Switching views, searching or setting a date range
  still starts at the top.

- **Deleting a project asks first** — ``x`` in the filter pane removed the
  project with its memberships and notes at once; it now asks (``y`` deletes,
  any other key keeps it), like deleting articles and tags.

- **Untitled papers** — a paper with no title (a link-only entry in a
  project import) is listed as "(no title)"; with it focused, the title
  scroller divided by the empty title's length.

- **The first automatic retrain learns from your ratings** — with no saved
  model yet, reaching ``retrain_interval`` ratings started a "warm" retrain
  from an empty model, which kept its empty vocabulary: every paper got the
  same score until a full retrain (``R``). It now trains from scratch when
  there is no model to continue from.

- **Key bindings edited in Settings work** — the Keys section saved the
  action's display name ("Rate Article") instead of its config name
  (``rate_article``), so the new key was ignored. It now saves the config
  name, and entries saved the old way take effect too. Settings also checks
  numbers with the same rules as ``config.yml``: a retrain interval below 1,
  negative minutes or margins, or a threshold outside 1–5 are refused.

- **Typing in a dialog no longer triggers other commands** — keys reached
  the global bindings before the open dialog: naming a project "dark matter"
  toggled the detail pane and opened the category filter, ``/`` opened Search
  instead of being typed into a path, ``?`` in a search opened the help, and
  Space in "Assign to project" selected the article instead of ticking the
  project. An open dialog now gets every key first, and keys it does not use
  go nowhere (an error message no longer lets ``j``/``k`` move the list
  behind it). ``Esc`` closes New Project and Assign to Project.

- **A config.yml of the wrong shape no longer stops the app starting** — a
  file that is valid YAML but not a list of settings (a single word, a list),
  a non-map ``article_settings``, or a key mapping that is not an
  action/key pair threw and aborted. These now load the defaults with a
  warning, like any other bad value. A key that is missing from the file
  (e.g. ``topics``) now keeps its default instead of being left empty. An
  empty ``key_mappings`` list is saved as ``[]`` so it stays empty.

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
- **Obsidian notes never embed a missing PDF** — with *Download PDFs* on,
  a note whose PDF failed to download links to the PDF on arXiv instead of
  embedding a file that is not there.
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
  downloading and embedding them. If a download fails, that note links to
  arXiv instead of embedding a missing file.

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
