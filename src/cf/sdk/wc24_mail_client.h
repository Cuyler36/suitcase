// The WiiConnect24 mail server protocol, as the Wii's KD speaks it to the CGIs its nwc24msg.cfg names
// (account.cgi, send.cgi, check.cgi, receive.cgi, delete.cgi): form POSTs answered with "key=value" lines
// (cd: the result code, 100 for success). Written against WiiLink's Mail-Server (docs/wc24/README.md).
// Blocking: call from worker threads.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cf {
namespace sdk {
namespace wc24 {

// A console's mail account: its Wii number and the credentials account.cgi gave it
struct MailAccount {
    std::uint64_t wiiNumber = 0;
    std::string password; // passwd: send, receive, delete
    std::string checkId;  // mlchkid: check
};

struct CgiReply {
    int code = 0;        // cd; 0 when there was no answer
    std::string message; // msg
    std::map<std::string, std::string> values;
    std::string error; // why there was no answer
};

constexpr int kCgiSuccess = 100;
constexpr int kCgiDuplicateRegistration = 211;

// Registers the Wii number: on success values has "passwd" and "mlchkid"
CgiReply RegisterAccount(const std::string& cgiUrl, std::uint64_t wiiNumber);

// Sends messages (each as the SDK's send box holds it: SMTP envelope, then the message): codes gets each
// message's result (cd<n>)
CgiReply SendMail(const std::string& cgiUrl, const MailAccount& account, const std::vector<std::string>& messages,
                  std::vector<int>* codes);

// Whether mail waits (the server's mail flag differs from the no-mail one)
CgiReply CheckMail(const std::string& cgiUrl, const MailAccount& account, bool* hasMail);

// The waiting messages (each an RFC 822 message), which the server then counts as delivered
CgiReply ReceiveMail(const std::string& cgiUrl, const MailAccount& account, std::vector<std::string>* messages);

// Removes the delivered messages from the server
CgiReply DeleteMail(const std::string& cgiUrl, const MailAccount& account, int count);

} // namespace wc24
} // namespace sdk
} // namespace cf
