// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

// The real (cpr) client against a one-shot HTTP server on 127.0.0.1.

#include "Arxiv/HttpClient.hh"

#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

// Serves `response` (raw HTTP) to each of the first `connections` clients.
class OneShotServer {
  public:
    explicit OneShotServer(std::string response, int connections = 1) {
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        socklen_t len = sizeof(addr);
        ::getsockname(m_fd, reinterpret_cast<sockaddr*>(&addr), &len);
        m_port = ntohs(addr.sin_port);
        ::listen(m_fd, 4);
        m_thread = std::thread([this, response = std::move(response), connections] {
            for (int i = 0; i < connections; ++i) {
                const int client = ::accept(m_fd, nullptr, nullptr);
                if (client < 0)
                    return;
                char buf[4096];
                (void)::recv(client, buf, sizeof(buf), 0); // the request
                std::size_t sent = 0;
                while (sent < response.size()) {
                    const ssize_t n = ::send(
                        client, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
                    if (n <= 0)
                        break;
                    sent += static_cast<std::size_t>(n);
                }
                ::close(client);
            }
        });
    }
    ~OneShotServer() {
        ::shutdown(m_fd, SHUT_RDWR);
        ::close(m_fd);
        m_thread.join();
    }
    std::string Url() const { return "http://127.0.0.1:" + std::to_string(m_port) + "/"; }

  private:
    int m_fd = -1;
    int m_port = 0;
    std::thread m_thread;
};

std::string ok(const std::string& body) {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body;
}

constexpr std::chrono::milliseconds kTimeout{5000};

} // namespace

TEST_CASE("HttpClient: a response body over the cap is refused", "[http][security]") {
    ::setenv("NO_PROXY", "127.0.0.1", 1);
    ::setenv("no_proxy", "127.0.0.1", 1);
    OneShotServer server(ok(std::string(4096, 'x')));
    auto http = Arxiv::MakeCprHttpClient(/*max_body_bytes=*/1024);

    const auto r = http->Get(server.Url(), kTimeout);

    REQUIRE(r.status != 200);
    REQUIRE(r.body.empty());
    REQUIRE_FALSE(r.error.empty());
}

TEST_CASE("HttpClient: a body under the cap arrives whole", "[http]") {
    ::setenv("NO_PROXY", "127.0.0.1", 1);
    ::setenv("no_proxy", "127.0.0.1", 1);
    OneShotServer server(ok("hello"));
    auto http = Arxiv::MakeCprHttpClient(1024);

    const auto r = http->Get(server.Url(), kTimeout);

    REQUIRE(r.status == 200);
    REQUIRE(r.body == "hello");
}

TEST_CASE("HttpClient: redirects to plain http are not followed", "[http][security]") {
    // The InspireHEP host check only sees the first URL; a redirect could
    // otherwise take a request anywhere, including off https.
    ::setenv("NO_PROXY", "127.0.0.1", 1);
    ::setenv("no_proxy", "127.0.0.1", 1);
    OneShotServer target(ok("followed"));
    OneShotServer redirect("HTTP/1.1 302 Found\r\nLocation: " + target.Url() +
                           "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    auto http = Arxiv::MakeCprHttpClient();

    const auto r = http->Get(redirect.Url(), kTimeout);

    REQUIRE(r.body != "followed");
    REQUIRE(r.status != 200);
    // Let the target's accept() return.
    (void)Arxiv::MakeCprHttpClient()->Get(target.Url(), kTimeout);
}
