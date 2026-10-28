// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Arxiv {

struct HttpResponse {
    long status = 0;   // HTTP status; 0 when no response arrived
    std::string body;  // empty for Download (the body went to the sink)
    std::string error; // transport error, if any
};

/// The HTTP requests Fetcher makes, behind an interface so tests can script
/// the network. Every request identifies arxiv-tui (Fetcher::UserAgent).
class HttpClient {
  public:
    virtual ~HttpClient() = default;
    virtual HttpResponse Get(const std::string& url, std::chrono::milliseconds timeout) = 0;
    /// Stream the body to `sink` chunk by chunk; `sink` returning false
    /// stops the transfer.
    virtual HttpResponse Download(const std::string& url,
                                  std::chrono::milliseconds timeout,
                                  const std::function<bool(std::string_view)>& sink) = 0;
    /// Abort the request in flight, if any, and fail every later one
    /// (status 0). Called from another thread when the app quits.
    virtual void Cancel() {}
};

/// The real client (cpr/libcurl).
/// Get() refuses (status 0, error set) a body larger than this.
inline constexpr std::size_t kMaxResponseBytes = std::size_t{64} << 20;

std::unique_ptr<HttpClient> MakeCprHttpClient(std::size_t max_body_bytes = kMaxResponseBytes);

} // namespace Arxiv
