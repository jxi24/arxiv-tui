.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Key Bindings
============

All bindings are remappable in ``config.yml`` via the ``key_mappings`` section.
See :doc:`configuration` for the config format.

Navigation
----------

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Key
     - Action name
     - Description
   * - ``j``
     - ``next``
     - Move to the next article
   * - ``k``
     - ``previous``
     - Move to the previous article
   * - ``h``
     - ``focus_left``
     - Move focus to the left pane
   * - ``l``
     - ``focus_right``
     - Move focus to the right pane
   * - ``a``
     - ``show_detail``
     - Toggle the detail (abstract) pane
   * - ``?``
     - ``help``
     - Toggle the help overlay
   * - ``q``
     - ``quit``
     - Quit the application

Article actions
---------------

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Key
     - Action name
     - Description
   * - ``Space``
     - ``toggle_selection``
     - Toggle selection on the current article
   * - ``b``
     - ``bookmark``
     - Bookmark (or unbookmark) the current article; bookmarks all selected articles when a selection is active
   * - ``D``
     - ``delete_article``
     - Delete the current article (or all selected articles) with confirmation
   * - ``u``
     - ``undo``
     - Undo the last delete, restoring the article(s), rating, project memberships, and tags
   * - ``d``
     - ``download_article``
     - Download the article PDF to ``download_dir``
   * - ``n``
     - ``rate``
     - Rate the article 1–5 stars; with a selection active, rates all selected articles at once
   * - ``c``
     - ``copy_bibtex``
     - Copy the BibTeX entry to the system clipboard (with InspireHEP lookup)
   * - ``N``
     - ``edit_note``
     - Edit the per-article note for the current project

Selection and bulk actions
--------------------------

Select any number of articles with ``Space``. While a selection is active the
article pane header shows ``[N selected]``.

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Key
     - Bulk behaviour
   * - ``b``
     - Bookmark all selected articles
   * - ``n``
     - Rate all selected articles with a single score
   * - ``p``
     - Open the project dialog in bulk-add mode — confirming links all selected articles to the checked projects
   * - ``D``
     - Delete all selected articles (confirmation required)
   * - ``g``
     - Export selected articles as a Markdown digest + PDF bundle
   * - ``o``
     - Export selected articles to an Obsidian vault

Filtering and search
--------------------

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Key
     - Action name
     - Description
   * - ``/``
     - ``search``
     - Open the search dialog
   * - ``r``
     - ``date_range``
     - Set the date range filter (when the *Date Range* filter is active)
   * - ``t``
     - ``toggle_category``
     - Toggle the category filter

Projects
--------

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Key
     - Action name
     - Description
   * - ``p``
     - ``create_project``
     - Assign or remove the current article from projects
   * - ``x``
     - ``delete_project``
     - Delete the focused project
   * - ``e``
     - ``export_project``
     - Export the project (Markdown / plain text / BibTeX / JSON)
   * - ``I``
     - ``import_project``
     - Import a project from JSON

Ranking and settings
--------------------

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Key
     - Action name
     - Description
   * - ``R``
     - ``retrain``
     - Force a full cold-start retrain of the ranking model
   * - ``S``
     - ``settings``
     - Open the settings dialog

Remapping a key
---------------

Add or override an entry in the ``key_mappings`` section of
``~/.config/arxiv-tui/config.yml``:

.. code-block:: yaml

   key_mappings:
     - action: next
       key: n          # remap "next article" from j to n
     - action: previous
       key: p          # remap "previous article" from k to p
     - action: quit
       key: q
