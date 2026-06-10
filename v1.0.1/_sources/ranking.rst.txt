.. SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
.. SPDX-License-Identifier: GPL-3.0-only

Article Ranking
===============

arxiv-tui includes a built-in personalised ranking system that learns from the
articles you rate and automatically surfaces the most relevant new papers each
day.

Getting started
---------------

1. **Rate articles** — press ``n`` on any article to give it a score from
   1 to 5 stars. With a selection active, ``n`` opens a *Rate Selection*
   dialog that applies the chosen score to all selected articles at once.
2. **Wait for training** — once you have rated at least 3 articles, the
   model trains in the background. A ``[Training…]`` badge appears in the
   article pane header while training is in progress. A
   ``[N rating(s) pending]`` counter shows how many more ratings are needed
   to trigger the next retrain.
3. **Open the Recommended filter** — select *Recommended* in the filter
   pane to see today's articles that score at or above
   ``recommend_threshold``. Articles are sorted by predicted score and each
   entry shows a ``[X.X★]`` badge.

Configuration
-------------

Two config keys control the ranking behaviour:

``recommend_threshold``
    Minimum predicted score (1.0–5.0) required to appear in *Recommended*.
    Default: ``3.5``.

``retrain_interval``
    Number of new ratings that must accumulate before an automatic
    background retrain is triggered. Default: ``5``.

Press ``R`` at any time to force an immediate *full cold-start* retrain,
which rebuilds the vocabulary and resets the network weights from scratch.
Use this when the corpus has grown significantly or the scores feel stale.

Technical details
-----------------

The ranker is implemented in pure C++17 with no external ML dependencies.

**TF-IDF vectorisation**
    Article text (title weighted 2×, abstract 1×) is tokenised, stop-word
    filtered, and projected onto the top 512 vocabulary terms by document
    frequency.

**2-layer MLP**
    Input (512) → hidden (32 units, ReLU) → output (1 unit, sigmoid scaled
    to [1.0, 5.0]). Weights are Xavier-initialised and trained with
    full-batch SGD + MSE loss for 200 epochs.

**Warm-start retraining**
    Threshold-triggered retrains continue from the existing weights rather
    than re-initialising, so each incremental update builds on prior
    learning. Force retrain (``R``) performs a cold start with a fresh
    vocabulary.

**Persistence**
    The trained model (vocabulary map, IDF weights, all network tensors) is
    saved to ``~/.local/share/arxiv-tui/ranker.bin`` after every retrain
    and loaded automatically on startup, so no retraining is needed between
    sessions.
