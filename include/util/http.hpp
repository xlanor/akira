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
    bool verifyPeer = true;
    bool certInfo = false;
    bool followLocation = true;
    bool freshConnect = false;

    void* reuseHandle = nullptr;
};

void httpMarkConnectionsStale();

HttpResponse httpPerform(const HttpRequest& request);

HttpResponse httpGet(const std::string& url, const std::string& bearer, long timeoutSec = 15);

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
    unsigned long long epoch = 0;
};

#endif // AKIRA_HTTP_HPP
