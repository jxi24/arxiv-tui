#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <Arxiv/Article.hh>
#include <Arxiv/DatabaseManager.hh>
#include <Arxiv/Fetcher.hh>

namespace arxiv_tui {
namespace test {

// Common test data
namespace fixtures {
    const std::vector<Arxiv::Article> sample_articles = {
        {
            "Sample Article Title",
            "https://arxiv.org/abs/2403.12345",
            "This is a sample abstract for testing purposes.",
            "John Doe, Jane Smith",
            std::chrono::system_clock::now(),
            "cs.AI",
            false
        },
        {
            "Another Test Article",
            "https://arxiv.org/abs/2403.12346",
            "Another sample abstract for testing.",
            "Alice Johnson, Bob Wilson",
            std::chrono::system_clock::now(),
            "math.PR",
            true
        }
    };

    // Sample RSS feed response
    const std::string sample_rss_response = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/">
    <channel>
        <item>
            <title>Sample Article Title</title>
            <link>https://arxiv.org/abs/2403.12345</link>
            <description>This is a sample abstract for testing purposes.</description>
            <pubDate>2024-03-25T12:00:00Z</pubDate>
            <dc:creator>John Doe, Jane Smith</dc:creator>
            <category>cs.AI</category>
        </item>
    </channel>
</rss>)";
}

} // namespace test
} // namespace arxiv_tui 
