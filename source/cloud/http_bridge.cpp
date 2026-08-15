#include "cloud/http_bridge.hpp"

#include "util/http.hpp"
#include "util/http_pool.hpp"

#include "cloud/curl_http.h"

#include <cstdlib>
#include <cstring>
#include <future>
#include <string>

namespace {

char* dupBytes(const std::string& value)
{
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (out)
        std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

ChiakiErrorCode akiraCloudTransport(ChiakiLog* log, const CCHttpRequest* request,
    CCHttpResponse* response, void* user)
{
    (void)log;
    (void)user;

    std::memset(response, 0, sizeof(*response));

    HttpRequest req;
    req.url = request->url ? request->url : "";
    req.followLocation = request->follow_redirects;
    req.timeoutSec = request->timeout_ms > 0 ? (request->timeout_ms + 999) / 1000 : 30;

    if (request->method && std::strcmp(request->method, "POST") == 0)
    {
        req.post = true;
        if (request->body)
        {
            size_t len = request->body_len > 0 ? request->body_len : std::strlen(request->body);
            req.postFields.assign(request->body, len);
        }
    }

    for (size_t i = 0; i < request->header_count; i++)
        if (request->headers[i])
            req.headers.emplace_back(request->headers[i]);

    std::promise<HttpResponse> promise;
    std::future<HttpResponse> future = promise.get_future();
    HttpPool::instance().submit([&req, &promise](HttpSession& session) {
        promise.set_value(session.perform(req));
    });
    HttpResponse res = future.get();

    if (res.transportFailed())
        return CHIAKI_ERR_NETWORK;

    response->status_code = res.status;

    if (!res.body.empty())
    {
        response->data = dupBytes(res.body);
        response->size = res.body.size();
    }

    if (request->capture_headers && !res.headers.empty())
    {
        std::string raw;
        for (const auto& header : res.headers)
        {
            raw += header.first;
            raw += ": ";
            raw += header.second;
            raw += "\r\n";
        }
        response->headers = dupBytes(raw);
        response->headers_size = raw.size();
    }

    std::string location = res.header("Location");
    if (!location.empty())
        response->redirect_url = dupBytes(location);

    return CHIAKI_ERR_SUCCESS;
}

} // namespace

namespace cloud {

void registerHttpBridge()
{
    cc_http_set_transport(akiraCloudTransport, nullptr);
}

}
