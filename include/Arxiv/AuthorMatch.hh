// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

namespace Arxiv {

/// Whether the followed name `followed` refers to the article author `author`.
/// Whole last names must agree (name particles such as "van der" are part of
/// them), first initials must agree, first names must agree when both are
/// spelled out, and middle initials count only when both names give one —
/// so "W. Li" matches "Wei Li" but not "Xiao Li" or "Wei Lin". A followed
/// last name alone matches that last name. Case-insensitive.
bool AuthorMatches(const std::string& followed, const std::string& author);

/// Whether `name` gives at least a first initial besides the last name.
bool HasFirstInitial(const std::string& name);

} // namespace Arxiv
