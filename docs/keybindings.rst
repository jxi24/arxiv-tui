.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Key Bindings
============

All bindings are remappable in ``config.yml`` via the ``key_mappings`` section,
using the *action names* below. See :doc:`configuration` for the config format.
The groups match the in-app help overlay (``?``).

Navigation
----------

.. list-table::
   :header-rows: 1
   :widths: 15 30 55

   * - Key
     - Action name
     - Description
   * - ``j``
     - ``next``
     - Move to the next article
   * - ``k``
     - ``previous``
     - Move to the previous article
   * - ``a``
     - ``show_detail``
     - Toggle the detail (abstract) pane
   * - ``l``
     - ``move_right``
     - Move focus to the pane on the right
   * - ``h``
     - ``move_left``
     - Move focus to the pane on the left
   * - ``r``
     - ``set_date_range``
     - Set the date range (when the *Range* filter is active)
   * - ``/``
     - ``search``
     - Open the search dialog
   * - ``t``
     - ``filter_categories``
     - Choose which arXiv categories are shown
   * - ``O``
     - ``open_in_browser``
     - Open the article (or every selected article) in the default browser

Articles
--------

.. list-table::
   :header-rows: 1
   :widths: 15 30 55

   * - Key
     - Action name
     - Description
   * - ``d``
     - ``download_article``
     - Download the article PDF to ``download_dir`` (runs in the background)
   * - ``b``
     - ``bookmark``
     - Bookmark (or unbookmark) the article; bookmarks every selected article when a selection is active
   * - ``n``
     - ``rate_article``
     - Rate the focused article 1–5 stars (always the focused article; see ``W`` for a selection)
   * - ``N``
     - ``edit_note``
     - Edit the article's note for the current project
   * - ``D``
     - ``delete_article``
     - Delete the article (or every selected article), with confirmation
   * - ``u``
     - ``undo_delete``
     - Undo the last delete, restoring rating, project memberships, notes, tags and read state
   * - ``f``
     - ``follow_authors``
     - Follow or unfollow authors: the focused article's authors, your followed list, or a typed name (``+``)
   * - ``#``
     - ``tag_article``
     - Tags for the article (or every selected article): toggle a tag, add a new one (``+``), delete one everywhere (``Del``)

Selection
---------

.. list-table::
   :header-rows: 1
   :widths: 15 30 55

   * - Key
     - Action name
     - Description
   * - ``Space``
     - ``toggle_selection``
     - Select or deselect the article
   * - ``C``
     - ``clear_selection``
     - Deselect everything (the selection is kept across views)
   * - ``W``
     - ``rate_selection``
     - Rate every selected article with one score

Select any number of articles with ``Space`` (the selection survives
switching views). While a selection is active the article pane header shows
``[N selected]``, and these keys act on the whole selection:

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Key
     - Bulk behaviour
   * - ``b``
     - Bookmark every selected article
   * - ``W``
     - Rate every selected article with a single score
   * - ``p``
     - Add every selected article to the checked projects
   * - ``D``
     - Delete every selected article (with confirmation)
   * - ``O``
     - Open every selected article in the browser
   * - ``g`` / ``G`` / ``o``
     - Export the selection (digest, digest archive, Obsidian)

Projects
--------

.. list-table::
   :header-rows: 1
   :widths: 15 30 55

   * - Key
     - Action name
     - Description
   * - ``p``
     - ``create_project``
     - Add the article (or the selection) to projects, or create one
   * - ``x``
     - ``delete_project``
     - Delete the focused project
   * - ``e``
     - ``export_project``
     - Export the project (Markdown / plain text / BibTeX / JSON)
   * - ``I``
     - ``import_project``
     - Import a project from JSON

Export & share
--------------

.. list-table::
   :header-rows: 1
   :widths: 15 30 55

   * - Key
     - Action name
     - Description
   * - ``c``
     - ``export_bibtex``
     - Copy the article's BibTeX to the clipboard (InspireHEP first, else built from arXiv data); saves ``<id>.bib`` when no clipboard tool is available
   * - ``X``
     - ``export_digest``
     - Write today's articles to ``digest-YYYY-MM-DD.md``
   * - ``g``
     - ``export_selected_digest``
     - Export the selection as a Markdown digest plus PDFs (background)
   * - ``G``
     - ``export_digest_archive``
     - As ``g``, then pack it into a ``.tar.gz`` (background)
   * - ``o``
     - ``export_to_obsidian``
     - Export the selection to the Obsidian vault (background)

Ranking
-------

.. list-table::
   :header-rows: 1
   :widths: 15 30 55

   * - Key
     - Action name
     - Description
   * - ``R``
     - ``force_retrain``
     - Retrain the ranking model from scratch
   * - ``K``
     - ``edit_keywords``
     - Edit the interest keywords used for ranking

App
---

.. list-table::
   :header-rows: 1
   :widths: 15 30 55

   * - Key
     - Action name
     - Description
   * - ``S``
     - ``settings``
     - Open the settings dialog
   * - ``?``
     - ``show_help``
     - Toggle the help overlay (type to filter it)
   * - ``q``
     - ``quit``
     - Quit (or close the open dialog)

Remapping a key
---------------

Add or override an entry in the ``key_mappings`` section of
``~/.config/arxiv-tui/config.yml``:

.. code-block:: yaml

   key_mappings:
     - action: bookmark
       key: m          # bookmark with m instead of b
     - action: quit
       key: Q          # quit with Q, so a stray q does not exit

Keys are single characters and case-sensitive. If two actions end up on the
same key only one of them fires; the help overlay (``?``) lists any such
conflict.

Mouse
-----

Click a view in the filter pane or an article in the list to select it (and
move the focus there); the wheel moves through views or articles. Clicks are
ignored while a dialog or the help overlay is open.
