// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// The network side of Fetcher, driven through a scripted HttpClient: paging
// and pacing of the arXiv API, error handling, streamed PDF downloads and the
// InspireHEP lookup. These paths had no tests (cpr was called directly).

#include "Arxiv/Article.hh"
#include "Arxiv/Fetcher.hh"
#include "Arxiv/HttpClient.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "fixtures/test_data.hh"

using namespace Arxiv;
using Catch::Matchers::ContainsSubstring;

namespace {

// Answers each request with the first rule whose URL fragment matches.
struct FakeHttp : HttpClient {
    struct Rule {
        std::string url_contains;
        std::function<HttpResponse(const std::string& url)> reply;
    };
    std::vector<Rule> rules;
    std::vector<std::string>* requests; // shared with the test

    explicit FakeHttp(std::vector<std::string>* log)
        : requests(log) {}

    HttpResponse Reply(const std::string& url) {
        requests->push_back(url);
        for (const auto& r : rules)
            if (url.find(r.url_contains) != std::string::npos)
                return r.reply(url);
        return {404, "", ""};
    }
    HttpResponse Get(const std::string& url, std::chrono::milliseconds) override {
        return Reply(url);
    }
    HttpResponse Download(const std::string& url,
                          std::chrono::milliseconds,
                          const std::function<bool(std::string_view)>& sink) override {
        auto r = Reply(url);
        // Hand the body over in small chunks, like a real transfer.
        for (std::size_t i = 0; i < r.body.size(); i += 7)
            if (!sink(std::string_view(r.body).substr(i, 7))) {
                r.error = "aborted by sink";
                break;
            }
        r.body.clear();
        return r;
    }
};

std::string atom_page(int first, int count) {
    std::string xml = R"(<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom")"
                      R"( xmlns:arxiv="http://arxiv.org/schemas/atom">)";
    for (int i = first; i < first + count; ++i) {
        const std::string id = "2605." + std::to_string(10000 + i);
        xml += "<entry><id>http://arxiv.org/abs/" + id + "v1</id><title>T" + id +
               "</title><summary>S</summary><author><name>A. B</name></author>"
               "<published>2026-05-10T00:00:00Z</published>"
               "<arxiv:primary_category term=\"hep-ph\"/></entry>";
    }
    return xml + "</feed>";
}

struct Net {
    std::vector<std::string> requests;
    FakeHttp* http = nullptr;
    std::unique_ptr<Fetcher> fetcher;
    std::vector<RequestPacer::Clock::duration> waits;

    explicit Net(std::string base = "") {
        auto fake = std::make_unique<FakeHttp>(&requests);
        http = fake.get();
        fetcher =
            std::make_unique<Fetcher>(std::vector<std::string>{"hep-ph"}, base, std::move(fake));
        // Record the pacing instead of sleeping.
        fetcher->SetApiPacer(std::make_unique<RequestPacer>(
            std::chrono::seconds(3),
            [this](RequestPacer::Clock::duration d) { waits.push_back(d); },
            [] { return RequestPacer::Clock::time_point{}; }));
    }
    void Answer(std::string fragment, std::function<HttpResponse(const std::string&)> reply) {
        http->rules.push_back({std::move(fragment), std::move(reply)});
    }
    std::size_t Count(const std::string& fragment) const {
        std::size_t n = 0;
        for (const auto& r : requests)
            if (r.find(fragment) != std::string::npos)
                ++n;
        return n;
    }
};

const std::string kRss = arxiv_tui::test::fixtures::sample_rss_response;

} // namespace

TEST_CASE("FetchSince pages through the arXiv API", "[fetcher][net]") {
    Net net;
    net.Answer("rss.arxiv.org", [](const std::string&) { return HttpResponse{200, kRss, ""}; });
    net.Answer("start=0&",
               [](const std::string&) { return HttpResponse{200, atom_page(0, 200), ""}; });
    net.Answer("start=200&",
               [](const std::string&) { return HttpResponse{200, atom_page(200, 5), ""}; });

    auto articles = net.fetcher->FetchSince("2026-05-01");

    SECTION("two API pages, the second shorter, then the RSS feed") {
        REQUIRE(net.Count("export.arxiv.org") == 2);
        REQUIRE(net.Count("rss.arxiv.org") == 1);
    }
    SECTION("every paper from both pages is returned") { REQUIRE(articles.size() >= 205); }
    SECTION("API pages are spaced by the pacer") {
        REQUIRE(net.waits.size() == 1); // before the second page
    }
}

TEST_CASE("FetchSince fails as a whole when a page fails", "[fetcher][net]") {
    // A partial result must not look like a complete one: the caller would
    // mark the missed days as fetched.
    Net net;
    net.Answer("start=0&",
               [](const std::string&) { return HttpResponse{200, atom_page(0, 200), ""}; });
    net.Answer("start=200&", [](const std::string&) { return HttpResponse{503, "", ""}; });

    REQUIRE_THROWS_AS(net.fetcher->FetchSince("2026-05-01"), FetchError);
}

TEST_CASE("Fetch reports an unreachable RSS feed", "[fetcher][net]") {
    Net net;
    net.Answer("rss.arxiv.org", [](const std::string&) { return HttpResponse{0, "", "timeout"}; });
    REQUIRE_THROWS_AS(net.fetcher->Fetch(), FetchError);
}

TEST_CASE("DownloadPaper over the network", "[fetcher][net][pdf]") {
    auto dir =
        std::filesystem::temp_directory_path() / ("arxiv_net_pdf_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Net net(dir.string());
    auto target = dir / "paper.pdf";

    SECTION("a PDF is streamed to the file") {
        const std::string pdf = "%PDF-1.7\n" + std::string(100, 'x');
        net.Answer("arxiv.org/pdf/2401.00001",
                   [&](const std::string&) { return HttpResponse{200, pdf, ""}; });
        REQUIRE(net.fetcher->DownloadPaper("2401.00001", "paper.pdf"));
        std::ifstream f(target, std::ios::binary);
        REQUIRE(std::string(std::istreambuf_iterator<char>(f), {}) == pdf);
    }
    SECTION("an HTML page with status 200 is rejected") {
        net.Answer("arxiv.org/pdf/", [](const std::string&) {
            return HttpResponse{200, "<html>rate limited</html>", ""};
        });
        REQUIRE_FALSE(net.fetcher->DownloadPaper("2401.00001", "paper.pdf"));
        REQUIRE_FALSE(std::filesystem::exists(target));
    }
    SECTION("a 404 is a failure") {
        REQUIRE_FALSE(net.fetcher->DownloadPaper("2401.00001", "paper.pdf"));
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("FetchBibTeX follows InspireHEP's BibTeX link", "[fetcher][net][inspire]") {
    Net net;

    SECTION("an InspireHEP link is fetched") {
        net.Answer("api/literature?q=", [](const std::string&) {
            return HttpResponse{
                200,
                R"({"hits":{"hits":[{"links":{"bibtex":"https://inspirehep.net/api/literature/1?format=bibtex"}}]}})",
                ""};
        });
        net.Answer("format=bibtex", [](const std::string&) {
            return HttpResponse{200, "@article{Doe:2024abc, title={X}}", ""};
        });
        REQUIRE_THAT(net.fetcher->FetchBibTeX("2401.00001"), ContainsSubstring("Doe:2024abc"));
    }
    SECTION("a link elsewhere is not followed") {
        net.Answer("api/literature?q=", [](const std::string&) {
            return HttpResponse{
                200, R"({"hits":{"hits":[{"links":{"bibtex":"https://evil.example/x"}}]}})", ""};
        });
        REQUIRE(net.fetcher->FetchBibTeX("2401.00001").empty());
        REQUIRE(net.Count("evil.example") == 0);
    }
    SECTION("no hit gives an empty result") {
        net.Answer("api/literature?q=", [](const std::string&) {
            return HttpResponse{200, R"({"hits":{"hits":[]}})", ""};
        });
        REQUIRE(net.fetcher->FetchBibTeX("2401.00001").empty());
    }
}

TEST_CASE("FetchSince gives up after a bounded number of API pages", "[fetcher][net][security]") {
    // An API that keeps answering full pages kept the loop (and the memory
    // for the papers) going for ever.
    Net net;
    net.Answer("export.arxiv.org",
               [](const std::string&) { return HttpResponse{200, atom_page(0, 200), ""}; });

    REQUIRE_THROWS_AS(net.fetcher->FetchSince("2026-05-01"), FetchError);
    REQUIRE(net.Count("export.arxiv.org") == static_cast<std::size_t>(Fetcher::kMaxApiPages));
}

TEST_CASE("FetchBibTeX accepts only a plausible BibTeX entry",
          "[fetcher][net][inspire][security]") {
    // The body went to the clipboard and into project .bib files unchecked:
    // an error page or a huge answer would land in the user's bibliography.
    Net net;
    net.Answer("api/literature?q=", [](const std::string&) {
        return HttpResponse{
            200,
            R"({"hits":{"hits":[{"links":{"bibtex":"https://inspirehep.net/api/literature/1?format=bibtex"}}]}})",
            ""};
    });
    std::string body;
    net.Answer("format=bibtex", [&](const std::string&) { return HttpResponse{200, body, ""}; });

    SECTION("not an entry") {
        body = "<html><body>Service unavailable</body></html>";
        REQUIRE(net.fetcher->FetchBibTeX("2401.00001").empty());
    }
    SECTION("too large") {
        body = "@article{Big, note={" + std::string(Fetcher::kMaxBibTeXBytes, 'x') + "}}";
        REQUIRE(net.fetcher->FetchBibTeX("2401.00001").empty());
    }
    SECTION("leading whitespace is fine") {
        body = "\n  @article{Doe:2024abc, title={X}}\n";
        REQUIRE_THAT(net.fetcher->FetchBibTeX("2401.00001"), ContainsSubstring("Doe:2024abc"));
    }
}
