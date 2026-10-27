// The WiiConnect24 mail server protocol (wc24_mail_client.h)
#include "wc24_mail_client.h"

#include "host_net.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace cf {
namespace sdk {
namespace wc24 {

namespace {

std::string Address(std::uint64_t wiiNumber) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "w%016llu", static_cast<unsigned long long>(wiiNumber));
    return buf;
}

std::string UrlEncode(const std::string& text) {
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string Form(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string body;
    for (const auto& [key, value] : fields) {
        body += (body.empty() ? "" : "&") + key + "=" + UrlEncode(value);
    }
    return body;
}

// The "key=value" lines of a CGI answer
void ParseLines(const std::string& text, CgiReply* reply) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) {
            reply->values.emplace(line.substr(0, eq), line.substr(eq + 1));
        }
        pos = end + 1;
    }
    const auto code = reply->values.find("cd");
    reply->code = code != reply->values.end() ? std::atoi(code->second.c_str()) : 0;
    const auto message = reply->values.find("msg");
    if (message != reply->values.end()) {
        reply->message = message->second;
    }
    if (reply->code == 0) {
        reply->error = "no result in the answer";
    }
}

CgiReply Post(const std::string& url, const std::string& contentType, const std::string& body,
              std::string* rawBody = nullptr) {
    CgiReply reply;
    const net::HttpResponse response = net::HttpRequest(url, "", contentType, body);
    if (response.status != 200) {
        reply.error = response.status == 0 ? response.error : "HTTP " + std::to_string(response.status);
        return reply;
    }
    if (rawBody != nullptr) {
        *rawBody = response.body;
    } else {
        ParseLines(response.body, &reply);
    }
    return reply;
}

} // namespace

CgiReply RegisterAccount(const std::string& cgiUrl, std::uint64_t wiiNumber) {
    return Post(cgiUrl + "account.cgi", "application/x-www-form-urlencoded", Form({{"mlid", Address(wiiNumber)}}));
}

// multipart/form-data: "mlid" holds the account lines, "m1".."mN" the messages
CgiReply SendMail(const std::string& cgiUrl, const MailAccount& account, const std::vector<std::string>& messages,
                  std::vector<int>* codes) {
    std::random_device random;
    char boundary[48];
    std::snprintf(boundary, sizeof(boundary), "----NWC24Boundary%08X%08X", random(), random());
    std::string body;
    auto part = [&](const std::string& name, const std::string& value) {
        body += std::string("--") + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" +
                value + "\r\n";
    };
    part("mlid", "mlid=" + Address(account.wiiNumber) + "\r\npasswd=" + account.password);
    for (std::size_t i = 0; i < messages.size(); i++) {
        part("m" + std::to_string(i + 1), messages[i]);
    }
    body += std::string("--") + boundary + "--\r\n";
    CgiReply reply = Post(cgiUrl + "send.cgi", std::string("multipart/form-data; boundary=") + boundary, body);
    codes->assign(messages.size(), 0);
    for (std::size_t i = 0; i < messages.size(); i++) {
        const auto code = reply.values.find("cd" + std::to_string(i + 1));
        if (code != reply.values.end()) {
            (*codes)[i] = std::atoi(code->second.c_str());
        }
    }
    return reply;
}

CgiReply CheckMail(const std::string& cgiUrl, const MailAccount& account, bool* hasMail) {
    std::random_device random;
    char challenge[16];
    std::snprintf(challenge, sizeof(challenge), "%08X", random());
    CgiReply reply = Post(cgiUrl + "check.cgi", "application/x-www-form-urlencoded",
                          Form({{"mlchkid", account.checkId}, {"chlng", challenge}}));
    const auto flag = reply.values.find("mail.flag");
    *hasMail = reply.code == kCgiSuccess && flag != reply.values.end() &&
               flag->second.find_first_not_of('0') != std::string::npos;
    return reply;
}

// The answer is multipart/mixed: a first part with the result lines, then a part per message
CgiReply ReceiveMail(const std::string& cgiUrl, const MailAccount& account, std::vector<std::string>* messages) {
    std::string body;
    CgiReply reply = Post(cgiUrl + "receive.cgi", "application/x-www-form-urlencoded",
                          Form({{"mlid", Address(account.wiiNumber)}, {"passwd", account.password}, {"maxsize", "1000000"}}),
                          &body);
    messages->clear();
    if (!reply.error.empty()) {
        return reply;
    }
    if (body.compare(0, 2, "--") != 0) {
        reply.error = "not a multipart answer";
        return reply;
    }
    const std::string boundary = "\r\n--" + body.substr(2, body.find("\r\n") - 2);
    std::vector<std::string> parts;
    std::size_t pos = body.find("\r\n");
    while (pos != std::string::npos) {
        const std::size_t next = body.find(boundary, pos);
        // The part: its headers, a blank line, its content
        const std::string part = body.substr(pos + 2, (next == std::string::npos ? body.size() : next) - pos - 2);
        const std::size_t contentStart = part.find("\r\n\r\n");
        if (contentStart != std::string::npos) {
            parts.push_back(part.substr(contentStart + 4));
        }
        if (next == std::string::npos || body.compare(next + boundary.size(), 2, "--") == 0) {
            break;
        }
        pos = next + boundary.size();
    }
    if (parts.empty()) {
        reply.error = "no result part";
        return reply;
    }
    ParseLines(parts[0], &reply);
    messages->assign(parts.begin() + 1, parts.end());
    return reply;
}

CgiReply DeleteMail(const std::string& cgiUrl, const MailAccount& account, int count) {
    return Post(cgiUrl + "delete.cgi", "application/x-www-form-urlencoded",
                Form({{"mlid", Address(account.wiiNumber)}, {"passwd", account.password}, {"delnum", std::to_string(count)}}));
}

} // namespace wc24
} // namespace sdk
} // namespace cf
