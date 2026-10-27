// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/HttpClient.hh"

#include "Arxiv/Fetcher.hh"

#include <curl/curl.h>

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
    // Redirects: a few hops, https only. Host checks (InspireHEP) see only
    // the first URL, so a redirect must not lead off https or loop on.
    session.SetRedirect(cpr::Redirect{5L});
    CURL* handle = session.GetCurlHolder()->handle;
#if LIBCURL_VERSION_NUM >= 0x075500 // 7.85
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
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
    explicit CprHttpClient(std::size_t max_body_bytes)
        : m_max_body(max_body_bytes) {}

    HttpResponse Get(const std::string& url, std::chrono::milliseconds timeout) override {
        // Collect the body ourselves so an endless or huge response is cut
        // off at m_max_body instead of filling memory.
        std::string body;
        bool too_large = false;
        auto out = convert(
            session_for(url, timeout).Download(cpr::WriteCallback{[&](std::string data, intptr_t) {
                if (body.size() + data.size() > m_max_body) {
                    too_large = true;
                    return false; // abort the transfer
                }
                body += data;
                return true;
            }}),
            false);
        if (too_large) {
            out.status = 0;
            out.error = "response larger than " + std::to_string(m_max_body) + " bytes";
        } else {
            out.body = std::move(body);
        }
        return out;
    }
    HttpResponse Download(const std::string& url,
                          std::chrono::milliseconds timeout,
                          const std::function<bool(std::string_view)>& sink) override {
        return convert(session_for(url, timeout)
                           .Download(cpr::WriteCallback{
                               [&sink](std::string data, intptr_t) { return sink(data); }}),
                       false);
    }

  private:
    std::size_t m_max_body;
};

} // namespace

std::unique_ptr<HttpClient> MakeCprHttpClient(std::size_t max_body_bytes) {
    return std::make_unique<CprHttpClient>(max_body_bytes);
}

} // namespace Arxiv
