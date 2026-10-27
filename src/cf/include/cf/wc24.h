// WiiConnect24 on the host (src/cf/sdk/nwc24_revoex.cpp): what the debug UI can ask of it
#pragma once

#include <string>

namespace cf {
namespace wc24 {

// Runs every registered download task now, as the KD's scheduler does when they are due (in the
// background; the game reads the files at its next WiiConnect24 check, at boot)
void DownloadNow();

// The last downloads, one per line
std::string DownloadLog();

// This console's Wii number
unsigned long long WiiNumber();

// Mail: a send and receive pass now (the worker also runs one every few minutes)
void MailNow();

// Mail: a message from this console to itself through the mail server, checked when it comes back
void MailSelfTest();

// The Wii number and whether it has a mail account
std::string MailStatus();

// The mail worker's last steps, one per line
std::string MailLog();

} // namespace wc24
} // namespace cf
