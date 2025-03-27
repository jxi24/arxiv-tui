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
    FetcherMock() : Arxiv::Fetcher({}) {}

    // Mock methods using trompeloeil
    MAKE_MOCK0(Fetch, std::vector<Arxiv::Article>(), override);
    MAKE_MOCK0(FetchToday, std::vector<Arxiv::Article>(), override);
    MAKE_MOCK2(DownloadPaper, bool(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetPaperAbstract, std::string(const std::string&), override);

    // Helper methods for testing
    void setFetchResponse(const std::vector<Arxiv::Article>& articles) {
        ALLOW_CALL(*this, Fetch())
            .RETURN(articles);
    }

    void setFetchTodayResponse(const std::vector<Arxiv::Article>& articles) {
        ALLOW_CALL(*this, FetchToday())
            .RETURN(articles);
    }

    void setDownloadPaperResponse(bool success) {
        ALLOW_CALL(*this, DownloadPaper(trompeloeil::_, trompeloeil::_))
            .RETURN(success);
    }

    void setGetPaperAbstractResponse(const std::string& abstract) {
        ALLOW_CALL(*this, GetPaperAbstract(trompeloeil::_))
            .RETURN(abstract);
    }
};

} // namespace test
} // namespace arxiv_tui 
