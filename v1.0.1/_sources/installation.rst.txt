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

If a crash report is saved to ``~/.local/state/arxiv-tui/``, you can replay
the session to reproduce the crash:

.. code-block:: bash

   arxiv-tui --replay ~/.local/state/arxiv-tui/crash_20260101_120000_SIGSEGV.txt
