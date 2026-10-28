.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Installation
============

Requirements
------------

.. list-table::
   :header-rows: 1
   :widths: 20 20 60

   * - Dependency
     - Version
     - Notes
   * - CMake
     - ≥ 3.17
     - Build system
   * - GCC or Clang
     - C++17 support
     - Compiler
   * - SQLite3
     - any
     - System package (``libsqlite3-dev`` on Debian/Ubuntu)
   * - libcurl
     - any
     - System package; bundled automatically via CPM if absent
   * - Internet
     - —
     - Required on first build for CPM package downloads

Recommended install (user-space)
---------------------------------

The recommended installation places the binary under ``~/.local/bin``, which
is on ``$PATH`` by default on most Linux distributions, and requires no root
access.

.. code-block:: bash

   git clone https://github.com/jxi24/arxiv-tui.git
   cd arxiv-tui
   cmake -B build -DCMAKE_INSTALL_PREFIX=~/.local
   cmake --build build -j$(nproc)
   cmake --install build

After installation, run ``arxiv-tui`` from any terminal.

System-wide install
-------------------

Omit the prefix (defaults to ``/usr/local``) and run the install step as root:

.. code-block:: bash

   cmake -B build
   cmake --build build -j$(nproc)
   sudo cmake --install build

Running from the build tree
----------------------------

If you want to test without installing:

.. code-block:: bash

   git clone https://github.com/jxi24/arxiv-tui.git
   cd arxiv-tui
   cmake -B build
   cmake --build build -j$(nproc)
   ./build/src/Arxiv/arxiv-tui

File layout after install
--------------------------

All runtime files follow the
`XDG Base Directory Specification <https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html>`_.
The paths below assume the standard defaults (``~/.config``,
``~/.local/share``, ``~/.local/state``); set ``XDG_CONFIG_HOME``,
``XDG_DATA_HOME``, or ``XDG_STATE_HOME`` to override them at runtime.

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - Path
     - Contents
   * - ``~/.config/arxiv-tui/config.yml``
     - Configuration (created on first run)
   * - ``~/.local/share/arxiv-tui/articles.db``
     - Article database and ratings
   * - ``~/.local/share/arxiv-tui/ranker.bin``
     - Trained ranking model
   * - ``~/.local/share/arxiv-tui/downloads/``
     - Default PDF download directory
   * - ``~/.local/state/arxiv-tui/arxiv_tui.log``
     - Rotating application log
   * - ``~/.local/state/arxiv-tui/replay.jsonl``
     - UI action replay log
   * - ``~/.local/state/arxiv-tui/crash_*.txt``
     - Crash reports with backtrace

Building with tests
-------------------

.. code-block:: bash

   cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure

Experimental GUI
----------------

An ImGui/GLFW window (``arxiv-gui``) over the same library, database and
config is built on request. It needs OpenGL and X11 development headers
(on Debian/Ubuntu: ``libgl-dev libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxi-dev libxkbcommon-dev``):

.. code-block:: bash

   cmake -B build -DARXIV_TUI_BUILD_GUI=ON
   cmake --build build --target arxiv-gui
   ./build/src/GUI/arxiv-gui

It reads ``config.yml`` and ``articles.db`` from the locations above and logs
to ``arxiv_gui.log`` (and keeps its window layout, ``imgui.ini``) in the state
directory. The window opens at once and fills in when the fetch finishes;
the Refresh button fetches again in the background. Pick a view in the
sidebar, search from the box in the header (``/`` focuses it), and move with
``j``/``k`` or the arrow keys (``b`` bookmarks, ``o`` opens in the browser).
The detail pane rates (click a star), downloads, bookmarks and opens the
paper; More… (or right-clicking a card) copies the link and edits tags and
projects. Unread papers carry a dot, and the status bar shows fetches,
downloads and when arXiv cannot be reached.

The window uses Inter, Noto Sans, Cantarell or DejaVu Sans, whichever is
installed first; set ``ARXIV_GUI_FONT`` (and ``ARXIV_GUI_FONT_BOLD``) to a
``.ttf``/``.otf`` path to choose another.

With a `Nerd Font <https://www.nerdfonts.com>`_ installed (any of them; the
icon-only *Symbols Nerd Font* is enough) the sidebar, buttons and menus get
icons. The GUI looks in ``~/.local/share/fonts``, ``~/.fonts``,
``/usr/local/share/fonts`` and ``/usr/share/fonts`` (following
``XDG_DATA_HOME`` / ``XDG_DATA_DIRS``). Set ``ARXIV_GUI_ICON_FONT`` to a font
file to pick one, or to an empty value to turn icons off.

Command line
------------

.. code-block:: text

   Usage: arxiv-tui [--trace] [MODE]

   Modes (one at a time):
     --fetch               fetch new papers into the database and exit (for cron)
     --export-today FILE   write today's papers as a Markdown digest
     --export-yaml FILE    write today's papers, ranked, as YAML
                           (--export-today and --export-yaml may be combined)
     --replay FILE         replay a replay.jsonl log or a crash report on a copy
                           of the database

   Options:
     --trace               log at trace level
     -h, --help            show this help and exit
     -V, --version         show the version and exit

An unknown option, a missing file name or two modes at once is an error
(exit status 2) rather than being ignored. To fetch and then export, run
``arxiv-tui --fetch && arxiv-tui --export-yaml digest.yml``.

Headless fetch and cron
-----------------------

arxiv-tui can update its database without opening the TUI, which makes it
suitable for a cron job:

.. code-block:: bash

   # Fetch new articles and exit
   arxiv-tui --fetch

   # Example crontab entry: refresh at 07:00 on weekdays
   0 7 * * 1-5  arxiv-tui --fetch

Replaying a crash report
-------------------------

If arxiv-tui crashes, a report is saved to ``~/.local/state/arxiv-tui/``.
Replay it to reproduce the crash (your database is not changed; see
:doc:`features`):

.. code-block:: bash

   arxiv-tui --replay ~/.local/state/arxiv-tui/crash_20260101_120000_SIGSEGV_1234.txt
