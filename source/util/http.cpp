#include "util/http.hpp"

#include <curl/curl.h>

#include "util/curl_wrappers.hpp"

static size_t curlWriteToString(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

HttpResponse httpPerform(const HttpRequest& request)
{
    HttpResponse response;

    CurlHandle curl;
    if (!curl)
    {
        response.error = "Failed to initialize CURL";
        return response;
    }

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
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, request.verifyPeer ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, request.verifyPeer ? 2L : 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSec);
    if (request.connectTimeoutSec > 0)
    {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, request.connectTimeoutSec);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        response.error = curl_easy_strerror(res);
        response.body.clear();
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
