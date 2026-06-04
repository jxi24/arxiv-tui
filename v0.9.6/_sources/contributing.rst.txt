.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Contributing
============

Contributions are welcome. Please open an issue to discuss a feature or bug
before submitting a pull request.

Development setup
-----------------

.. code-block:: bash

   git clone https://github.com/jxi24/arxiv-tui.git
   cd arxiv-tui
   cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure

Install `pre-commit <https://pre-commit.com>`_ to run the same checks locally
that CI enforces:

.. code-block:: bash

   pip install pre-commit
   pre-commit install           # installs the git hook
   pre-commit run --all-files   # run manually on the whole tree

Test-driven development
-----------------------

All new behaviour must be developed using TDD:

1. Write a **failing test** that captures the expected behaviour.
2. Run the suite and confirm the new test fails (red).
3. Write the **minimum production code** needed to make the test pass.
4. Run the suite again and confirm all tests pass (green).
5. **Refactor** if needed; keep tests green throughout.

Tests live in ``test/unit/``. The project uses
`Catch2 <https://github.com/catchorg/Catch2>`_ v3 for assertions and
`trompeloeil <https://github.com/rollbear/trompeloeil>`_ for mocks.

Architecture notes
------------------

- ``AppCore`` contains all business logic and is testable without FTXUI.
  Never mix UI code into it.
- ``DatabaseManager`` and ``Fetcher`` expose all methods as ``virtual`` for
  mocking. Always inject them via constructor — never construct them inside
  ``AppCore``.
- Use ``std::unique_ptr`` for owned heap objects; no raw ``new``/``delete``.
- Log with ``spdlog``; prefer exceptions over error codes for unrecoverable
  errors.

See `CLAUDE.md <https://github.com/jxi24/arxiv-tui/blob/main/CLAUDE.md>`_
for full naming conventions, header style, and testing guidelines.

CI / Code quality
-----------------

Every push and pull request runs:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Workflow
     - What it checks
   * - **Build & Test**
     - CMake configure, build, and full test suite
   * - **Sanitizers**
     - ASan + UBSan and TSan, as separate jobs
   * - **Static Analysis**
     - clang-tidy on project sources
   * - **clang-format**
     - Code style against ``.clang-format``
   * - **REUSE**
     - Every file carries a valid SPDX header

License
-------

This project is licensed under the
`GNU General Public License v3.0 <https://www.gnu.org/licenses/gpl-3.0.html>`_.
