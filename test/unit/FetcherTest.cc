#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <Arxiv/Fetcher.hh>
#include <fixtures/test_data.hh>
#include <mocks/FetcherMock.hh>
#include <filesystem>

using namespace arxiv_tui;
using namespace arxiv_tui::test;
using namespace Catch::Matchers;

TEST_CASE("Article fetching", "[fetcher]") {
    FetcherMock fetcher;
    
    SECTION("Should fetch articles from arXiv") {
        // Set up mock expectations
        REQUIRE_CALL(fetcher, Fetch())
            .RETURN(fixtures::sample_articles);
        
        auto articles = fetcher.Fetch();
        REQUIRE(articles.size() == 2);
        REQUIRE_THAT(articles[0].title, ContainsSubstring("Sample Article Title"));
    }

    SECTION("Should fetch today's articles") {
        // Set up mock expectations
        REQUIRE_CALL(fetcher, FetchToday())
            .RETURN(fixtures::sample_articles);
        
        auto articles = fetcher.FetchToday();
        REQUIRE(articles.size() == 2);
        REQUIRE_THAT(articles[0].title, ContainsSubstring("Sample Article Title"));
    }
}

TEST_CASE("Paper downloading", "[fetcher]") {
    FetcherMock fetcher;
    
    SECTION("Should download paper PDF") {
        std::string paper_id = "2403.12345";
        std::string output_path = "test.pdf";
        
        // Set up mock expectations
        REQUIRE_CALL(fetcher, DownloadPaper(paper_id, output_path))
            .RETURN(true);
        
        REQUIRE(fetcher.DownloadPaper(paper_id, output_path));
        
        // Clean up test file
        std::filesystem::remove(output_path);
    }
    
    SECTION("Should handle download failures") {
        std::string paper_id = "2403.12345";
        std::string output_path = "test.pdf";
        
        // Set up mock expectations
        REQUIRE_CALL(fetcher, DownloadPaper(paper_id, output_path))
            .RETURN(false);
        
        REQUIRE_FALSE(fetcher.DownloadPaper(paper_id, output_path));
    }
}

TEST_CASE("Abstract retrieval", "[fetcher]") {
    FetcherMock fetcher;
    
    SECTION("Should retrieve paper abstract") {
        std::string paper_id = "2403.12345";
        std::string expected_abstract = "This is a sample abstract for testing purposes.";
        
        // Set up mock expectations
        REQUIRE_CALL(fetcher, GetPaperAbstract(paper_id))
            .RETURN(expected_abstract);
        
        std::string abstract = fetcher.GetPaperAbstract(paper_id);
        REQUIRE_THAT(abstract, ContainsSubstring(expected_abstract));
    }
    
    SECTION("Should handle missing abstracts") {
        std::string paper_id = "2403.12345";
        
        // Set up mock expectations
        REQUIRE_CALL(fetcher, GetPaperAbstract(paper_id))
            .RETURN("");
        
        std::string abstract = fetcher.GetPaperAbstract(paper_id);
        REQUIRE(abstract.empty());
    }
} 
