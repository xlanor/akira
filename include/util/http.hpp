#ifndef AKIRA_HTTP_HPP
#define AKIRA_HTTP_HPP

#include <string>
#include <vector>

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string error;

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    bool transportFailed() const { return !error.empty(); }
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
};

HttpResponse httpPerform(const HttpRequest& request);

HttpResponse httpGet(const std::string& url, const std::string& bearer, long timeoutSec = 15);

#endif // AKIRA_HTTP_HPP
