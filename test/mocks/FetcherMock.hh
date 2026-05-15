#pragma once

#include <Arxiv/Fetcher.hh>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

namespace arxiv_tui {
namespace test {

class FetcherMock : public Arxiv::Fetcher {
public:
    // Constructor
    FetcherMock() : Arxiv::Fetcher({}) {
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, Fetch())
                .RETURN(std::vector<Arxiv::Article>{}));
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, FetchSince(ANY(std::string)))
                .RETURN(std::vector<Arxiv::Article>{}));
    }

    // Mock methods using trompeloeil
    MAKE_MOCK0(Fetch, std::vector<Arxiv::Article>(), override);
    MAKE_MOCK0(FetchToday, std::vector<Arxiv::Article>(), override);
    MAKE_MOCK1(FetchSince, std::vector<Arxiv::Article>(const std::string&), override);
    MAKE_MOCK2(DownloadPaper, bool(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetPaperAbstract, std::string(const std::string&), override);
    MAKE_MOCK1(FetchBibTeX, std::string(const std::string&), override);

    // Helper methods for testing
    void setFetchResponse(const std::vector<Arxiv::Article>& articles) {
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, Fetch())
                .RETURN(articles));
    }

    void setFetchTodayResponse(const std::vector<Arxiv::Article>& articles) {
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, FetchToday())
                .RETURN(articles));
    }

    void setDownloadPaperResponse(bool success) {
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, DownloadPaper(trompeloeil::_, trompeloeil::_))
                .RETURN(success));
    }

    void setGetPaperAbstractResponse(const std::string& abstract) {
        m_expectations.push_back(
            NAMED_ALLOW_CALL(*this, GetPaperAbstract(trompeloeil::_))
                .RETURN(abstract));
    }

    /// Set the BibTeX response for a specific paper_id, or for any ID when
    /// paper_id is empty.
    void setBibTeXResponse(const std::string& paper_id, const std::string& bibtex) {
        if (paper_id.empty()) {
            m_expectations.push_back(
                NAMED_ALLOW_CALL(*this, FetchBibTeX(trompeloeil::_))
                    .RETURN(bibtex));
        } else {
            m_expectations.push_back(
                NAMED_ALLOW_CALL(*this, FetchBibTeX(paper_id))
                    .RETURN(bibtex));
        }
    }

private:
    std::vector<std::unique_ptr<trompeloeil::expectation>> m_expectations;
};

} // namespace test
} // namespace arxiv_tui
