.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Configuration
=============

On first launch, arxiv-tui creates ``~/.config/arxiv-tui/config.yml`` with
default values. Edit this file to customise topics, key bindings, and
behaviour.

Example config
--------------

.. code-block:: yaml

   article_settings:
     download_dir: /home/user/.local/share/arxiv-tui/downloads
     topics:
       - hep-ph
       - hep-ex
       - hep-lat
       - hep-th
   recommend_threshold: 3.5
   retrain_interval: 5
   auto_refresh_minutes: 0
   scroll_margin: 3
   max_article_age_days: 0
   key_mappings:
     - action: next
       key: j
     - action: previous
       key: k
     - action: quit
       key: q
     - action: bookmark
       key: b
     - action: download_article
       key: d
     - action: show_detail
       key: a
     - action: toggle_selection
       key: " "

Reference
---------

``article_settings.topics``
    List of arXiv category identifiers to fetch. Any valid arXiv category
    is accepted, for example:

    .. code-block:: yaml

       topics:
         - cs.LG
         - quant-ph
         - math.AG
         - hep-ph

    The full category list is available at
    `arxiv.org/category_taxonomy <https://arxiv.org/category_taxonomy>`_.

``article_settings.download_dir``
    Directory where PDFs are saved when you press ``d``. Created
    automatically if it does not exist. Defaults to
    ``<XDG_DATA_HOME>/arxiv-tui/downloads/``.

``recommend_threshold``
    Minimum predicted score (1.0–5.0) an article must reach to appear in
    the *Recommended* filter. Default: ``3.5``.

``retrain_interval``
    Number of new article ratings that must accumulate before the ranking
    model retrains automatically in the background. Default: ``5``.
    Press ``R`` to force an immediate retrain at any time.

``auto_refresh_minutes``
    Interval (in minutes) at which the background thread re-fetches arXiv
    feeds. Set to ``0`` to disable automatic refresh. Default: ``0``.

``scroll_margin``
    Number of context lines kept visible above and below the selected
    article when scrolling. Default: ``3``.

``undo_buffer_size``
    Number of delete operations kept in the in-memory undo ring buffer.
    When the buffer is full, the oldest entry is evicted to make room.
    Set to ``0`` to disable undo entirely. Default: ``10``.

``max_article_age_days``
    Maximum age (in days) of articles kept in the database. Articles older
    than this threshold are deleted on startup unless they are bookmarked,
    rated, or assigned to a project. Set to ``0`` to disable pruning.
    Default: ``0``.

``key_mappings``
    List of ``{action, key}`` pairs that remap the default key bindings.
    See :doc:`keybindings` for the full list of action names.

Environment overrides
---------------------

The XDG environment variables take precedence over the compiled-in defaults
and the values derived from ``CMAKE_INSTALL_PREFIX``:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Variable
     - Overrides
   * - ``XDG_CONFIG_HOME``
     - Config directory (default: ``~/.config``)
   * - ``XDG_DATA_HOME``
     - Data directory: database, ranker, downloads (default: ``~/.local/share``)
   * - ``XDG_STATE_HOME``
     - State directory: logs, replay, crash reports (default: ``~/.local/state``)

``ARXIV_TUI_CLIPBOARD``
    Force a specific clipboard backend. Accepted values: ``xclip``,
    ``xsel``, ``wl-clipboard``. When unset, arxiv-tui probes for
    available backends in that order.
