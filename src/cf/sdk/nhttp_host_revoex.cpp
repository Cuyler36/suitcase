// The RevoEX NHTTP library (revolution/nhttp.h) on the host: the HTTP client DWC logs in with (the NAS)
// and the game's WUS notifications use. Following the decomp's RevoEX NHTTP (lib/RevoEX/src/nhttp) for
// the calls' contract:
//   - a request owns the caller's receive buffer: the body lands there, NUL-terminated when it fits
//     (DWC strtoks it), and NHTTPGetBodyAll / NHTTPGetBodyBuffer point into it;
//   - post fields are sent application/x-www-form-urlencoded, encoded as NHTTPi_encodeUrlChar does
//     (letters and digits kept, ' ' as '+', the rest %XX);
//   - NHTTPSendRequestAsync runs the request on a worker thread and calls back from it with an NHTTPErr
//     and the response (the request's own object), which NHTTPDestroyResponse frees; cancelling makes
//     the callback report NHTTP_ERROR_CANCELED.
// The connection calls the game's WUS code uses (__NHTTPCreateConnection, NHTTPStartConnection,
// NHTTPGetConnectionError: NHTTP_ERROR_BUSY while running, ...) work on the same objects.
//
// http:// goes over the host's sockets (host_net.h); https:// through borealis::http, with the platform's
// TLS (Nintendo's built-in root CA and client certificate options do nothing). Host names go through
// the online service (cf::online::MapHost); a Wi-Fi Connection host the service serves over plain HTTP
// (as WiiLink's patch points the NAS to http://nas.<domain>/ac) is asked over http://.
#include <revolution/nhttp.h>

#include <borealis/http.hpp>
#include <borealis/log_c.h>
#include <cf/online.h>

#include "host_net.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

#define WARN(...) borealis_log_printf(BOREALIS_LOG_WARNING, "cf::nhttp", __VA_ARGS__)

struct Transfer {
    std::string url;
    NHTTPReqMethod method = NHTTP_REQMETHOD_GET;
    char* buffer = nullptr; // the caller's receive buffer
    u32 bufferSize = 0;
    NHTTPReqCallback callback = nullptr;
    void* userParam = nullptr;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::pair<std::string, std::string>> fields;
    std::string rawBody; // NHTTPAddPostDataRaw
    // The result
    std::atomic<int> state{0}; // 0 not started, 1 running, 2 done
    std::atomic<bool> canceled{false};
    std::mutex finish; // between the worker finishing and the caller destroying it
    bool orphaned = false; // destroyed while running: the worker frees it, without a callback
    NHTTPErr error = NHTTP_ERROR_NONE;
    int status = 0;
    std::string responseHeaders;
    u32 bodyLength = 0;
    std::string headerValue; // NHTTPGetHeaderField's last answer
};

std::mutex sMutex;
std::vector<Transfer*> sConnections; // NHTTPConnection: the index

std::string UrlEncode(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            out.push_back(c);
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 15]);
            out.push_back(kHex[static_cast<unsigned char>(c) & 15]);
        }
    }
    return out;
}

// The URL as the service serves it: the host renamed, and a Wi-Fi Connection host asked over http://
std::string ServiceUrl(const std::string& url) {
    const std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return url;
    }
    const std::size_t hostStart = schemeEnd + 3;
    const std::size_t hostEnd = url.find_first_of(":/", hostStart);
    const std::string host = url.substr(hostStart, hostEnd - hostStart);
    const std::string mapped = cf::online::MapHost(host);
    std::string scheme = url.substr(0, schemeEnd);
    if (mapped != host) {
        scheme = "http"; // the service's Wi-Fi Connection hosts: plain HTTP, as WiiLink's patch
    }
    return scheme + "://" + mapped + (hostEnd == std::string::npos ? "" : url.substr(hostEnd));
}

void Run(Transfer* t) {
    const std::string url = ServiceUrl(t->url);
    std::string body = t->rawBody;
    for (const auto& [name, value] : t->fields) {
        body += (body.empty() ? "" : "&") + UrlEncode(name) + "=" + UrlEncode(value);
    }
    const bool post = t->method == NHTTP_REQMETHOD_POST;
    int status = 0;
    std::string headers;
    std::string content;
    NHTTPErr error = NHTTP_ERROR_NONE;
    if (url.compare(0, 7, "http://") == 0) {
        std::string extra;
        for (const auto& [name, value] : t->headers) {
            extra += name + ": " + value + "\r\n";
        }
        const cf::sdk::net::HttpResponse response =
            cf::sdk::net::HttpRequest(url, "", post ? "application/x-www-form-urlencoded" : "", body, extra);
        if (response.status == 0) {
            WARN("%s: %s", url.c_str(), response.error.c_str());
            error = NHTTP_ERROR_CONNECT;
        }
        status = response.status;
        headers = response.headers;
        content = response.body;
    } else {
        borealis::http::Request request;
        request.method = post ? borealis::http::Method::Post : borealis::http::Method::Get;
        request.url = url;
        for (const auto& [name, value] : t->headers) {
            request.headers.push_back({name, value});
        }
        if (post) {
            request.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
            request.body = body;
        }
        borealis::Task<borealis::http::Result> task = borealis::http::start(std::move(request));
        while (!task.ready()) {
            if (t->canceled) {
                task.cancel();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const std::optional<borealis::http::Result> result = task.try_take();
        if (!result || result->error != borealis::http::Error::None) {
            WARN("%s: %s", url.c_str(), result ? result->message.c_str() : "no result");
            error = result && result->error == borealis::http::Error::Canceled ? NHTTP_ERROR_CANCELED : NHTTP_ERROR_SSL;
        } else {
            status = result->response.statusCode;
            for (const auto& header : result->response.headers) {
                headers += header.name + ": " + header.value + "\r\n";
            }
            content = result->response.body;
        }
    }
    if (t->canceled) {
        error = NHTTP_ERROR_CANCELED;
    }
    t->status = status;
    t->responseHeaders = std::move(headers);
    // The body into the caller's buffer, NUL-terminated when it fits
    if (error == NHTTP_ERROR_NONE && t->buffer != nullptr && t->bufferSize != 0) {
        if (content.size() + 1 > t->bufferSize) {
            error = NHTTP_ERROR_BUFFULL;
        }
        const std::size_t n = std::min<std::size_t>(content.size(), t->bufferSize - 1);
        std::memcpy(t->buffer, content.data(), n);
        t->buffer[n] = '\0';
        t->bodyLength = static_cast<u32>(n);
    }
    t->error = error;
    bool orphaned;
    {
        std::lock_guard lock(t->finish);
        t->state = 2;
        orphaned = t->orphaned;
    }
    if (orphaned) {
        delete t;
        return;
    }
    if (t->callback != nullptr) {
        t->callback(error, reinterpret_cast<NHTTPResponse*>(t), t->userParam);
    }
}

Transfer* Create(const char* url, NHTTPReqMethod method, void* buffer, u32 bufferSize, NHTTPReqCallback callback,
                 void* userParam) {
    if (url == nullptr) {
        return nullptr;
    }
    auto* t = new Transfer;
    t->url = url;
    t->method = method;
    t->buffer = static_cast<char*>(buffer);
    t->bufferSize = bufferSize;
    t->callback = callback;
    t->userParam = userParam;
    return t;
}

Transfer* Of(const void* handle) {
    return const_cast<Transfer*>(static_cast<const Transfer*>(handle));
}

NHTTPConnection Start(Transfer* t) {
    if (t == nullptr || t->state != 0) {
        return -1;
    }
    t->state = 1;
    NHTTPConnection id;
    {
        std::lock_guard lock(sMutex);
        id = static_cast<NHTTPConnection>(sConnections.size());
        sConnections.push_back(t);
    }
    std::thread(Run, t).detach();
    return id;
}

} // namespace

extern "C" {

s32 NHTTPStartup(NHTTPAlloc alloc, NHTTPFree free, u32 priority) {
    (void)alloc, (void)free, (void)priority;
    return 0;
}

void NHTTPCleanupAsync(NHTTPCleanupCallback callback) {
    if (callback != nullptr) {
        callback();
    }
}

s32 NHTTPSSLGetError(void) {
    return 0;
}

NHTTPRequest* NHTTPCreateRequest(const char* url, NHTTPReqMethod method, void* buffer, u32 bufferSize,
                                 NHTTPReqCallback callback, void* userParam) {
    return reinterpret_cast<NHTTPRequest*>(Create(url, method, buffer, bufferSize, callback, userParam));
}

NHTTPRequest* __NHTTPCreateRequestEx(const char* url, NHTTPReqMethod method, void* buffer, u32 bufferSize,
                                     NHTTPReqCallback callback, void* userParam, NHTTPResponseCallback responseCallback,
                                     NHTTPResponseCleanup cleanup) {
    (void)responseCallback, (void)cleanup;
    return NHTTPCreateRequest(url, method, buffer, bufferSize, callback, userParam);
}

s32 NHTTPAddHeaderField(NHTTPRequest* request, const char* name, const char* value) {
    if (request == nullptr || name == nullptr || value == nullptr) {
        return -1;
    }
    Of(request)->headers.emplace_back(name, value);
    return 0;
}

s32 NHTTPAddPostDataAscii(NHTTPRequest* request, const char* name, const char* value) {
    if (request == nullptr || name == nullptr || value == nullptr) {
        return -1;
    }
    Of(request)->fields.emplace_back(name, value);
    return 0;
}

s32 NHTTPAddPostDataBinary(NHTTPRequest* request, const char* name, const char* value, u32 length) {
    if (request == nullptr || name == nullptr || value == nullptr) {
        return -1;
    }
    Of(request)->fields.emplace_back(name, std::string(value, length));
    return 0;
}

s32 NHTTPAddPostDataRaw(NHTTPRequest* request, const void* data, u32 length) {
    if (request == nullptr || data == nullptr) {
        return -1;
    }
    Of(request)->rawBody.append(static_cast<const char*>(data), length);
    return 0;
}

s32 NHTTPSetPostDataEncoding(NHTTPRequest* request, NHTTPEncodingType encoding) {
    (void)request;
    return encoding == NHTTP_ENCODING_TYPE_MULTIPART ? -1 : 0; // URL encoding only (all the game needs)
}

// Certificates, proxies and verification: the platform's TLS decides
s32 NHTTPSetVerifyOption(NHTTPRequest* request, u32 option) {
    (void)request, (void)option;
    return 0;
}

s32 NHTTPSetProxy(NHTTPRequest* request, const char* server, u16 port, const char* username, const char* password) {
    (void)request, (void)server, (void)port, (void)username, (void)password;
    return 0;
}

s32 NHTTPSetProxyDefault(NHTTPRequest* request) {
    (void)request;
    return 0;
}

s32 NHTTPSetClientCertDefault(NHTTPRequest* request) {
    (void)request;
    return 0;
}

s32 NHTTPSetRootCADefault(NHTTPRequest* request) {
    (void)request;
    return 0;
}

s32 NHTTPSetBuiltinRootCA(NHTTPRequest* request, u32 index) {
    (void)request, (void)index;
    return 0;
}

s32 NHTTPSetBuiltinClientCert(NHTTPRequest* request, u32 index) {
    (void)request, (void)index;
    return 0;
}

s32 NHTTPDisableVerifyOptionForDebug(NHTTPRequest* request, u32 option) {
    (void)request, (void)option;
    return 0;
}

NHTTPConnection NHTTPSendRequestAsync(NHTTPRequest* request) {
    return Start(Of(request));
}

s32 NHTTPCancelRequestAsync(NHTTPConnection connection) {
    std::lock_guard lock(sMutex);
    if (connection < 0 || static_cast<std::size_t>(connection) >= sConnections.size() || sConnections[connection] == nullptr) {
        return -1;
    }
    sConnections[connection]->canceled = true;
    return 0;
}

void NHTTPDestroyResponse(NHTTPResponse* response) {
    Transfer* t = Of(response);
    if (t == nullptr) {
        return;
    }
    {
        std::lock_guard lock(sMutex);
        for (Transfer*& c : sConnections) {
            if (c == t) {
                c = nullptr;
            }
        }
    }
    {
        std::lock_guard lock(t->finish);
        if (t->state == 1) {
            t->canceled = true;
            t->orphaned = true; // the worker frees it
            return;
        }
    }
    delete t;
}

s32 NHTTPGetBodyAll(NHTTPResponse* response, void** buffer) {
    Transfer* t = Of(response);
    if (t == nullptr || buffer == nullptr) {
        return -1;
    }
    *buffer = t->buffer;
    return static_cast<s32>(t->bodyLength);
}

s32 NHTTPGetResultCode(NHTTPResponse* response) {
    Transfer* t = Of(response);
    return t != nullptr && t->state == 2 ? t->status : -1;
}

s32 NHTTPGetHeaderField(NHTTPResponse* response, const char* name, char** value) {
    Transfer* t = Of(response);
    if (t == nullptr || name == nullptr || value == nullptr) {
        return -1;
    }
    const std::string key = name;
    std::size_t pos = 0;
    while (pos < t->responseHeaders.size()) {
        const std::size_t end = t->responseHeaders.find("\r\n", pos);
        const std::string line = t->responseHeaders.substr(pos, end - pos);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos && colon == key.size()) {
            bool same = true;
            for (std::size_t i = 0; i < key.size() && same; i++) {
                same = std::tolower(static_cast<unsigned char>(line[i])) == std::tolower(static_cast<unsigned char>(key[i]));
            }
            if (same) {
                std::size_t start = colon + 1;
                while (start < line.size() && line[start] == ' ') {
                    start++;
                }
                t->headerValue = line.substr(start);
                *value = t->headerValue.data();
                return static_cast<s32>(t->headerValue.size());
            }
        }
        if (end == std::string::npos) {
            break;
        }
        pos = end + 2;
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------
// The connection calls (d_net_wus.cpp): the same objects, started without a callback and polled
// ---------------------------------------------------------------------------------------------
void* __NHTTPCreateConnection(const char* url, int method, void* buffer, u32 size, void* callback, void* arg) {
    return Create(url, static_cast<NHTTPReqMethod>(method), buffer, size, reinterpret_cast<NHTTPReqCallback>(callback),
                  arg);
}

int NHTTPStartConnection(void* handle) {
    return Start(Of(handle)) >= 0 ? 0 : -1;
}

int NHTTPGetConnectionError(void* handle) {
    Transfer* t = Of(handle);
    if (t == nullptr) {
        return NHTTP_ERROR_SYSTEM;
    }
    return t->state != 2 ? NHTTP_ERROR_BUSY : t->error;
}

u32 NHTTPGetBodyBuffer(void* handle, char** buffer, u32* size) {
    Transfer* t = Of(handle);
    if (t == nullptr) {
        return 0;
    }
    if (buffer != nullptr) {
        *buffer = t->buffer;
    }
    if (size != nullptr) {
        *size = t->bodyLength;
    }
    return t->bodyLength;
}

int NHTTPDeleteConnection(void* handle) {
    NHTTPDestroyResponse(static_cast<NHTTPResponse*>(handle));
    return 0;
}

} // extern "C"
