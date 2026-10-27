// WiiConnect24 Wii numbers (the 16-digit user ID of a console's mail account): generated as the Wii's
// KD does from the console ID, model, area and a counter, and checked as the SDK does
// (NWC24CheckUserId: the unscrambled ID carries a CRC).
#pragma once

#include <cstdint>

namespace cf {
namespace sdk {
namespace wc24 {

constexpr std::uint64_t kMaxWiiNumber = 9999999999999999ull; // 16 decimal digits

// The model and area fields of the ID
enum Model : std::uint32_t { MODEL_RVT = 0, MODEL_RVL = 1, MODEL_RVD = 2 };
enum Area : std::uint32_t { AREA_JPN = 0, AREA_USA = 1, AREA_EUR = 2, AREA_TWN = 3, AREA_KOR = 4, AREA_CHN = 6 };

// The Wii number of a console ID with the generation counter (0..31; the next one after a collision)
std::uint64_t GenerateWiiNumber(std::uint32_t consoleId, std::uint32_t counter, Model model, Area area);

// The ID's fields, unscrambled (the SDK's getUnScrambleId)
std::uint64_t UnscrambleWiiNumber(std::uint64_t id);

// Whether the ID's CRC holds (a well-formed Wii number)
bool CheckWiiNumber(std::uint64_t id);

} // namespace wc24
} // namespace sdk
} // namespace cf
