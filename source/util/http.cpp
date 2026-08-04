#include "util/http.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>

#include "util/curl_wrappers.hpp"

static std::array<std::mutex, CURL_LOCK_DATA_LAST> g_shareLocks;

static void curlShareLock(CURL*, curl_lock_data data, curl_lock_access, void*)
{
    g_shareLocks[data].lock();
}

static void curlShareUnlock(CURL*, curl_lock_data data, void*)
{
    g_shareLocks[data].unlock();
}

static CURLSH* sharedDnsCache()
{
    static CURLSH* share = []() -> CURLSH* {
        CURLSH* created = curl_share_init();
        if (!created)
            return nullptr;

        curl_share_setopt(created, CURLSHOPT_LOCKFUNC, curlShareLock);
        curl_share_setopt(created, CURLSHOPT_UNLOCKFUNC, curlShareUnlock);
        curl_share_setopt(created, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        return created;
    }();

    return share;
}

static size_t curlWriteToString(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

static std::string trimHeaderValue(const std::string& value)
{
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();

    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

static size_t curlCollectHeader(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    std::string line(static_cast<char*>(contents), total);

    size_t colon = line.find(':');
    if (colon != std::string::npos)
    {
        std::string name = trimHeaderValue(line.substr(0, colon));
        std::string value = trimHeaderValue(line.substr(colon + 1));
        if (!name.empty())
        {
            auto* headers = static_cast<std::vector<std::pair<std::string, std::string>>*>(userp);
            headers->emplace_back(std::move(name), std::move(value));
        }
    }

    return total;
}

std::string HttpResponse::header(const std::string& name) const
{
    auto equalsIgnoreCase = [](const std::string& a, const std::string& b) {
        return a.size() == b.size() &&
            std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
                return std::tolower(x) == std::tolower(y);
            });
    };

    for (const auto& entry : headers)
    {
        if (equalsIgnoreCase(entry.first, name))
            return entry.second;
    }

    return std::string();
}

HttpResponse httpPerform(const HttpRequest& request)
{
    HttpResponse response;

    CurlHandle ownHandle;
    CURL* curl = static_cast<CURL*>(request.reuseHandle);
    if (curl)
        curl_easy_reset(curl);
    else
        curl = ownHandle.handle;

    if (!curl)
    {
        response.error = "Failed to initialize CURL";
        return response;
    }

    char errorBuffer[CURL_ERROR_SIZE];
    errorBuffer[0] = '\0';
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    CurlSlist headers;
    if (!request.bearer.empty())
    {
        std::string bearerHeader = "Authorization: Bearer " + request.bearer;
        headers.append(bearerHeader.c_str());
    }
    for (const std::string& header : request.headers)
    {
        headers.append(header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    if (static_cast<curl_slist*>(headers))
    {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, static_cast<curl_slist*>(headers));
    }
    if (!request.basicUser.empty())
    {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, request.basicUser.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, request.basicPassword.c_str());
    }
    if (request.post)
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.postFields.size()));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.postFields.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlCollectHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, request.verifyPeer ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, request.verifyPeer ? 2L : 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSec);
    if (request.connectTimeoutSec > 0)
    {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, request.connectTimeoutSec);
    }

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
    if (CURLSH* share = sharedDnsCache())
    {
        curl_easy_setopt(curl, CURLOPT_SHARE, share);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        response.error = errorBuffer[0] != '\0'
            ? std::string(errorBuffer)
            : std::string(curl_easy_strerror(res));
        response.body.clear();
        response.headers.clear();
        return response;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

HttpResponse httpGet(const std::string& url, const std::string& bearer, long timeoutSec)
{
    HttpRequest request;
    request.url = url;
    request.bearer = bearer;
    request.timeoutSec = timeoutSec;
    return httpPerform(request);
}

HttpSession::HttpSession()
    : handle(curl_easy_init())
{
}

HttpSession::~HttpSession()
{
    if (handle)
        curl_easy_cleanup(static_cast<CURL*>(handle));
}

HttpResponse HttpSession::perform(HttpRequest request)
{
    request.reuseHandle = handle;
    return httpPerform(request);
}

HttpResponse HttpSession::get(const std::string& url, const std::string& bearer, long timeoutSec)
{
    HttpRequest request;
    request.url = url;
    request.bearer = bearer;
    request.timeoutSec = timeoutSec;
    return perform(request);
}
