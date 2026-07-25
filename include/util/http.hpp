#ifndef AKIRA_HTTP_HPP
#define AKIRA_HTTP_HPP

#include <string>
#include <utility>
#include <vector>

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string error;
    std::vector<std::pair<std::string, std::string>> headers;

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    bool transportFailed() const { return !error.empty(); }

    std::string header(const std::string& name) const;
};

struct HttpRequest {
    std::string url;
    std::string bearer;
    std::string basicUser;
    std::string basicPassword;
    std::vector<std::string> headers;
    std::string postFields;
    bool post = false;
    long timeoutSec = 15;
    long connectTimeoutSec = 0;
    bool verifyPeer = false;

    // Optional caller-owned CURL* reused across calls. curl-libnx creates a Switch SSL
    // context per connection, and the ssl service only allows a few at once, so a fresh
    // handle per request exhausts them. Reusing one keeps the connection (and its context)
    // alive. The caller must guarantee only one thread uses a given handle at a time.
    void* reuseHandle = nullptr;
};

HttpResponse httpPerform(const HttpRequest& request);

HttpResponse httpGet(const std::string& url, const std::string& bearer, long timeoutSec = 15);

// A long-lived CURL handle. curl-libnx calls sslCreateContext per connection and the
// Switch ssl service only allows a few at once, so a handle per request exhausts them.
// One session per worker thread keeps the connection, and its SSL context, alive across
// every request that thread makes. Not thread-safe: one session, one thread.
class HttpSession {
public:
    HttpSession();
    ~HttpSession();

    HttpSession(const HttpSession&) = delete;
    HttpSession& operator=(const HttpSession&) = delete;

    HttpResponse perform(HttpRequest request);
    HttpResponse get(const std::string& url, const std::string& bearer, long timeoutSec = 15);

private:
    void* handle = nullptr;
};

#endif // AKIRA_HTTP_HPP
