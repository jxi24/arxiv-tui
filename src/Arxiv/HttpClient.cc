// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/HttpClient.hh"

#include "Arxiv/Fetcher.hh"

#include "cpr/cpr.h"

namespace Arxiv {

namespace {

// User-Agent and timeout on a Session: cpr's variadic Get/Download with
// three options trips -Wduplicated-branches inside cpr's own header.
cpr::Session session_for(const std::string& url, std::chrono::milliseconds timeout) {
    cpr::Session session;
    session.SetUrl(cpr::Url{url});
    session.SetUserAgent(cpr::UserAgent{Fetcher::UserAgent()});
    session.SetTimeout(cpr::Timeout{timeout});
    return session;
}

HttpResponse convert(const cpr::Response& r, bool keep_body) {
    HttpResponse out;
    out.status = r.status_code;
    if (keep_body)
        out.body = r.text;
    out.error = r.error.message;
    return out;
}

class CprHttpClient : public HttpClient {
  public:
    HttpResponse Get(const std::string& url, std::chrono::milliseconds timeout) override {
        return convert(session_for(url, timeout).Get(), true);
    }
    HttpResponse Download(const std::string& url,
                          std::chrono::milliseconds timeout,
                          const std::function<bool(std::string_view)>& sink) override {
        return convert(session_for(url, timeout)
                           .Download(cpr::WriteCallback{
                               [&sink](std::string data, intptr_t) { return sink(data); }}),
                       false);
    }
};

} // namespace

std::unique_ptr<HttpClient> MakeCprHttpClient() { return std::make_unique<CprHttpClient>(); }

} // namespace Arxiv
