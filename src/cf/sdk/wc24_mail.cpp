// WiiConnect24 mail (wc24_mail.h): the SDK's message format, written and parsed.
#include "wc24_mail.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace cf {
namespace sdk {
namespace wc24 {

namespace {

constexpr std::size_t kBase64Step = 57;          // bytes per base64 line (76 characters)
constexpr std::uint32_t kTextPlain = 0x00010000; // NWC24_TEXT_PLAIN

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// The SDK's MIME tables (lib/RevoEX/src/nwc24/NWC24Parser.c), with the NWC24MIMEType and NWC24Charset
// values (revolution/nwc24/NWC24Types.h)
struct MimeInfo {
    std::uint32_t type;
    const char* name;
    const char* suffix;
};
const MimeInfo kMimeTypes[] = {
    {0x00030000, "application/octet-stream", "dat"},   {0x00030002, "application/x-wii-minidata", "wii"},
    {0x00030001, "application/x-wii-msgboard", "wii"}, {0x00020000, "image/jpeg", "jpg"},
    {0x00020001, "image/x-wii-picture", "wii"},        {0x000F0001, "multipart/alternative", "???"},
    {0x000F0000, "multipart/mixed", "???"},            {0x000F0002, "multipart/related", "???"},
    {0x00010001, "text/html", "htm"},                  {kTextPlain, "text/plain", "txt"},
};

struct CharsetInfo {
    std::uint32_t charset;
    const char* name;
};
const CharsetInfo kCharsets[] = {
    {0x00020002, "euc-jp"},      {0x00020000, "iso-2022-jp"}, {0x00080001, "iso-8859-1"},  {0x0008000A, "iso-8859-10"},
    {0x0008000F, "iso-8859-15"}, {0x00080002, "iso-8859-2"},  {0x00080003, "iso-8859-3"},  {0x00080005, "iso-8859-5"},
    {0x00080007, "iso-8859-7"},  {0x00080009, "iso-8859-9"},  {0x00020001, "shift_jis"},   {0x00000000, "us-ascii"},
    {0x00010010, "utf-16be"},    {0x00010020, "utf-32be"},    {0x00010008, "utf-8"},       {0x000F1252, "windows-1252"},
};
const char* const kEncodings[] = {"7bit", "8bit", "base64", "quoted-printable"};

const MimeInfo* FindMime(std::uint32_t type) {
    for (const MimeInfo& m : kMimeTypes) {
        if (m.type == type) {
            return &m;
        }
    }
    return nullptr;
}

const char* CharsetName(std::uint32_t charset) {
    for (const CharsetInfo& c : kCharsets) {
        if (c.charset == charset) {
            return c.name;
        }
    }
    return nullptr;
}

std::string Base64Line(const std::uint8_t* p, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; i += 3) {
        const std::uint32_t b = (std::uint32_t(p[i]) << 16) | (i + 1 < n ? std::uint32_t(p[i + 1]) << 8 : 0) |
                                (i + 2 < n ? p[i + 2] : 0);
        out += kBase64[(b >> 18) & 63];
        out += kBase64[(b >> 12) & 63];
        out += i + 1 < n ? kBase64[(b >> 6) & 63] : '=';
        out += i + 2 < n ? kBase64[b & 63] : '=';
    }
    return out;
}

// The SDK's base64 body: 57-byte lines with CRLF, the last one followed by a blank line
std::string Base64Body(const std::vector<std::uint8_t>& data) {
    std::string out;
    std::size_t pos = 0;
    while (data.size() - pos >= kBase64Step) {
        out += Base64Line(data.data() + pos, kBase64Step) + "\r\n";
        pos += kBase64Step;
    }
    out += Base64Line(data.data() + pos, data.size() - pos) + "\r\n\r\n";
    return out;
}

std::vector<std::uint8_t> Base64Decode(const std::string& text) {
    std::vector<std::uint8_t> out;
    std::uint32_t accum = 0;
    int bits = 0;
    for (char c : text) {
        const char* p = std::strchr(kBase64, c);
        if (c == '\0' || p == nullptr) {
            continue; // line breaks, padding
        }
        accum = (accum << 6) | static_cast<std::uint32_t>(p - kBase64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>(accum >> bits));
        }
    }
    return out;
}

std::string Address(std::uint64_t id, const std::string& domain) {
    char buf[80];
    std::snprintf(buf, sizeof(buf), "w%016llu@%s", static_cast<unsigned long long>(id), domain.c_str());
    return buf;
}

// Days since 1900-01-01 (the SDK's ConvertDateToDays)
std::int32_t DaysSince1900(int year, int month, int day) {
    static const int kDaysOfYear[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    auto leap = [](int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; };
    std::int32_t dayOfYear = day - 1 + kDaysOfYear[month - 1];
    if (month >= 3 && leap(year)) {
        dayOfYear++;
    }
    const std::int32_t y = year - 1900;
    return y * 365 + dayOfYear + (y - 1) / 4 - (y - 1) / 100 + (y + 299) / 400;
}

std::string Lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string Trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        a++;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        b--;
    }
    return s.substr(a, b - a);
}

struct Header {
    std::string name; // lower case
    std::string value;
};

// Splits a block of header lines (folded continuation lines joined) from its body
std::vector<Header> ParseHeaders(const std::string& text, std::size_t* bodyStart) {
    std::vector<Header> headers;
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
        pos = end + 1;
        if (line.empty()) {
            break;
        }
        if ((line[0] == ' ' || line[0] == '\t') && !headers.empty()) {
            headers.back().value += " " + Trim(line);
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            headers.push_back({Lower(Trim(line.substr(0, colon))), Trim(line.substr(colon + 1))});
        }
    }
    *bodyStart = pos < text.size() ? pos : text.size();
    return headers;
}

const std::string* Find(const std::vector<Header>& headers, const char* name) {
    for (const Header& h : headers) {
        if (h.name == name) {
            return &h.value;
        }
    }
    return nullptr;
}

// A parameter of a header value: name=value or name="value"
std::string Param(const std::string& value, const char* name) {
    const std::string lower = Lower(value);
    const std::string key = std::string(name) + "=";
    std::size_t p = lower.find(key);
    if (p == std::string::npos) {
        return {};
    }
    p += key.size();
    if (p < value.size() && value[p] == '"') {
        const std::size_t end = value.find('"', p + 1);
        return value.substr(p + 1, end == std::string::npos ? std::string::npos : end - p - 1);
    }
    std::size_t end = p;
    while (end < value.size() && value[end] != ';' && !std::isspace(static_cast<unsigned char>(value[end]))) {
        end++;
    }
    return value.substr(p, end - p);
}

std::uint32_t MimeTypeOf(const std::string& contentType) {
    const std::string type = Lower(Trim(contentType.substr(0, contentType.find(';'))));
    for (const MimeInfo& m : kMimeTypes) {
        if (type == m.name) {
            return m.type;
        }
    }
    return 0x00030000; // application/octet-stream
}

} // namespace

std::uint64_t ParseWiiAddress(const std::string& address) {
    std::string a = Trim(address);
    const std::size_t lt = a.find('<');
    if (lt != std::string::npos) {
        a = a.substr(lt + 1, a.find('>', lt) - lt - 1);
    }
    if (a.size() < 17 || (a[0] != 'w' && a[0] != 'W')) {
        return 0;
    }
    std::uint64_t id = 0;
    for (int i = 1; i <= 16; i++) {
        if (!std::isdigit(static_cast<unsigned char>(a[i]))) {
            return 0;
        }
        id = id * 10 + (a[i] - '0');
    }
    return (a.size() == 17 || a[17] == '@') ? id : 0;
}

std::string FormatSendMessage(const OutgoingMessage& msg) {
    std::string out;
    char buf[256];

    // createTime: minutes since 1900 (UTC)
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    const std::uint32_t createTime = static_cast<std::uint32_t>(
        DaysSince1900(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday) * (24 * 60) + utc.tm_hour * 60 + utc.tm_min);

    // SMTP envelope (the send mailbox's)
    out += "MAIL FROM: " + Address(msg.from, msg.domain) + "\r\n";
    for (std::uint64_t to : msg.to) {
        out += "RCPT TO: " + Address(to, msg.domain) + "\r\n";
    }
    out += "DATA\r\n";

    static const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::snprintf(buf, sizeof(buf), "Date: %02d %s %d %02d:%02d:%02d -0000\r\n", utc.tm_mday, kMonths[utc.tm_mon],
                  utc.tm_year + 1900, utc.tm_hour, utc.tm_min, utc.tm_sec);
    out += buf;
    out += "From: " + Address(msg.from, msg.domain) + "\r\n";
    out += "To: ";
    for (std::size_t i = 0; i < msg.to.size(); i++) {
        out += Address(msg.to[i], msg.domain);
        if (i + 1 < msg.to.size()) {
            out += ",\r\n ";
        }
    }
    out += "\r\n";
    std::snprintf(buf, sizeof(buf), "Message-Id: <%05X%08X%08X%08X@%s>\r\n", msg.id,
                  static_cast<std::uint32_t>(msg.from >> 32), static_cast<std::uint32_t>(msg.from), createTime,
                  msg.domain.c_str());
    out += buf;

    if (msg.flags & MSG_FOR_RECIPIENT) {
        const std::uint32_t idFlag = ((msg.flags & MSG_FOR_APP) ? 1 : 0) | ((msg.flags & MSG_FOR_MENU) ? 2 : 0);
        std::snprintf(buf, sizeof(buf), "X-Wii-AppId: %u-%08X-%04X\r\n", idFlag, msg.appId, msg.groupId);
        out += buf;
        if (msg.command != 0) {
            std::snprintf(buf, sizeof(buf), "X-Wii-Cmd: %08X\r\n", msg.command);
            out += buf;
        }
        if (msg.tag != 0) {
            std::snprintf(buf, sizeof(buf), "X-Wii-Tag: %08X\r\n", msg.tag);
            out += buf;
        }
        if (msg.dwcId) {
            std::snprintf(buf, sizeof(buf), "X-Wii-DWCId: %08X\r\n", *msg.dwcId);
            out += buf;
        }
    }

    char boundary[48];
    std::snprintf(boundary, sizeof(boundary), "Boundary-NWC24-%08X%05X", createTime, msg.id);
    const bool multipart = !msg.attachments.empty();
    out += "MIME-Version: 1.0\r\n";
    if (multipart) {
        out += std::string("Content-Type: multipart/mixed;\r\n boundary=\"") + boundary + "\"\r\n";
        out += std::string("\r\n--") + boundary + "\r\n";
    }

    if (const char* charset = CharsetName(msg.charset)) {
        out += std::string("Content-Type: text/plain; charset=") + charset + "\r\n";
    }
    if (msg.encoding < 4) {
        out += std::string("Content-Transfer-Encoding: ") + kEncodings[msg.encoding] + "\r\n\r\n";
    }
    if (!msg.text.empty()) {
        if (msg.encoding == 2) {
            out += Base64Body(msg.text);
        } else {
            out.append(msg.text.begin(), msg.text.end());
        }
    }

    for (std::size_t i = 0; i < msg.attachments.size(); i++) {
        const Attachment& a = msg.attachments[i];
        const MimeInfo* mime = FindMime(a.mimeType);
        const char* typeName = mime != nullptr ? mime->name : "application/octet-stream";
        const char* suffix = mime != nullptr ? mime->suffix : "dat";
        out += std::string("\r\n--") + boundary + "\r\n";
        std::snprintf(buf, sizeof(buf), "Content-Type: %s;\r\n name=%c%07d.%s\r\n", typeName,
                      static_cast<char>('a' + i), static_cast<int>(msg.id), suffix);
        out += buf;
        out += "Content-Transfer-Encoding: base64\r\n";
        std::snprintf(buf, sizeof(buf), "Content-Disposition: attachment;\r\n filename=%c%07d.%s\r\n\r\n",
                      static_cast<char>('a' + i), static_cast<int>(msg.id), suffix);
        out += buf;
        out += Base64Body(a.data);
        if (i + 1 == msg.attachments.size()) {
            out += std::string("\r\n--") + boundary + "--\r\n";
        }
    }
    return out;
}

std::optional<IncomingMessage> ParseMessage(const std::string& raw) {
    // Skip an SMTP envelope (MAIL FROM / RCPT TO / DATA) before the headers
    std::string text = raw;
    const std::size_t data = text.find("DATA\r\n");
    if (text.rfind("MAIL FROM:", 0) == 0 && data != std::string::npos) {
        text = text.substr(data + 6);
    }

    std::size_t bodyStart = 0;
    const std::vector<Header> headers = ParseHeaders(text, &bodyStart);
    const std::string* from = Find(headers, "from");
    if (from == nullptr) {
        return std::nullopt;
    }
    IncomingMessage msg;
    msg.from = ParseWiiAddress(*from);
    if (msg.from != 0) {
        msg.flags |= MSG_FOR_RECIPIENT;
        if (const std::string* appId = Find(headers, "x-wii-appid")) {
            unsigned idFlag = 0, app = 0, group = 0;
            if (std::sscanf(appId->c_str(), "%u-%x-%x", &idFlag, &app, &group) == 3) {
                msg.flags |= ((idFlag & 1) ? MSG_FOR_APP : 0) | ((idFlag & 2) ? MSG_FOR_MENU : 0);
                msg.appId = app;
                msg.groupId = static_cast<std::uint16_t>(group);
            }
        }
    } else {
        msg.flags |= MSG_FOR_PUBLIC;
    }
    if (const std::string* dwc = Find(headers, "x-wii-dwcid")) {
        msg.dwcId = static_cast<std::uint32_t>(std::strtoul(dwc->c_str(), nullptr, 16));
    }

    // The attachments: every base64 part but the text
    const std::string* contentType = Find(headers, "content-type");
    if (contentType != nullptr && Lower(*contentType).find("multipart/") != std::string::npos) {
        const std::string boundary = Param(*contentType, "boundary");
        if (boundary.empty()) {
            return msg;
        }
        const std::string delim = "--" + boundary;
        std::size_t pos = text.find(delim, bodyStart);
        while (pos != std::string::npos) {
            pos += delim.size();
            if (text.compare(pos, 2, "--") == 0) {
                break; // the closing boundary
            }
            const std::size_t next = text.find(delim, pos);
            std::string part = text.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            while (!part.empty() && (part[0] == '\r' || part[0] == '\n')) {
                part.erase(0, 1);
            }
            std::size_t partBody = 0;
            const std::vector<Header> partHeaders = ParseHeaders(part, &partBody);
            const std::string* type = Find(partHeaders, "content-type");
            const std::string* encoding = Find(partHeaders, "content-transfer-encoding");
            const std::uint32_t mime = type != nullptr ? MimeTypeOf(*type) : kTextPlain;
            if (mime != kTextPlain) {
                Attachment a;
                a.mimeType = mime;
                const std::string body = part.substr(partBody);
                if (encoding != nullptr && Lower(*encoding) == "base64") {
                    a.data = Base64Decode(body);
                } else {
                    a.data.assign(body.begin(), body.end());
                }
                msg.attachments.push_back(std::move(a));
            }
            pos = next;
        }
    }
    return msg;
}

} // namespace wc24
} // namespace sdk
} // namespace cf
