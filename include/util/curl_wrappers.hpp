#ifndef AKIRA_CURL_WRAPPERS_HPP
#define AKIRA_CURL_WRAPPERS_HPP

#include <curl/curl.h>

struct CurlHandle {
    CURL* handle;
    CurlHandle() : handle(curl_easy_init()) {}
    ~CurlHandle() { if (handle) curl_easy_cleanup(handle); }
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
    operator CURL*() { return handle; }
    explicit operator bool() { return handle != nullptr; }
};

struct CurlSlist {
    curl_slist* list = nullptr;
    CurlSlist() = default;
    void append(const char* s) { list = curl_slist_append(list, s); }
    ~CurlSlist() { if (list) curl_slist_free_all(list); }
    CurlSlist(const CurlSlist&) = delete;
    CurlSlist& operator=(const CurlSlist&) = delete;
    operator curl_slist*() { return list; }
};

#endif // AKIRA_CURL_WRAPPERS_HPP
