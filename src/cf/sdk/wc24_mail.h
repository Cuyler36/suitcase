// WiiConnect24 mail on the host: messages written as the SDK's NWC24CommitMsg writes them into the send
// mailbox (SMTP envelope, RFC 822 headers, X-Wii headers, MIME body: lib/RevoEX/src/nwc24/
// NWC24MsgCommit.c), so the KD's upload and a Wii reading them see the same bytes, and received mail
// parsed for the fields the SDK's message objects report.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cf {
namespace sdk {
namespace wc24 {

// The message's audience, as the SDK's message object flags (NWC24_MSGOBJ_FOR_*)
enum MsgFlags : std::uint32_t {
    MSG_FOR_RECIPIENT = 1 << 0, // to Wii numbers (w<16 digits>@wii.com)
    MSG_FOR_APP = 1 << 1,       // the application reads it
    MSG_FOR_MENU = 1 << 2,      // the Message Board shows it
    MSG_FOR_PUBLIC = 1 << 3,    // to e-mail addresses
};

struct Attachment {
    std::uint32_t mimeType; // NWC24MIMEType
    std::vector<std::uint8_t> data;
};

struct OutgoingMessage {
    std::uint32_t id = 0;      // the mailbox's message number
    std::uint32_t flags = 0;   // MsgFlags
    std::uint64_t from = 0;    // this console's Wii number
    std::vector<std::uint64_t> to;
    std::uint32_t appId = 0;   // e.g. 'RUUE'
    std::uint16_t groupId = 0; // the disc's maker code, e.g. '01'
    std::uint32_t tag = 0;
    std::uint32_t command = 0;
    std::optional<std::uint32_t> dwcId; // X-Wii-DWCId: the sender's DWC profile id
    std::uint32_t charset = 0;          // NWC24Charset of the text
    std::uint32_t encoding = 0;         // NWC24Encoding of the text
    std::vector<std::uint8_t> text;
    std::vector<Attachment> attachments;
    std::string domain = "wii.com"; // the account domain (NWC24GetAccountDomain): the mail service's
};

// The message file, as the SDK writes it to the send mailbox; createTime is the SDK's (minutes since
// 1900, UTC), taken from now
std::string FormatSendMessage(const OutgoingMessage& msg);

struct IncomingMessage {
    std::uint32_t flags = 0;  // MsgFlags, from the From address and X-Wii-AppId
    std::uint64_t from = 0;   // 0 for an e-mail sender
    std::uint32_t appId = 0;
    std::uint16_t groupId = 0;
    std::optional<std::uint32_t> dwcId;
    std::vector<Attachment> attachments; // decoded
};

// A received message (RFC 822; an SMTP envelope before it, as in a send mailbox file, is skipped).
// Empty when it is not a parsable message.
std::optional<IncomingMessage> ParseMessage(const std::string& text);

// The Wii number of an address "w<16 digits>@...", or 0
std::uint64_t ParseWiiAddress(const std::string& address);

} // namespace wc24
} // namespace sdk
} // namespace cf
