#pragma once

namespace cf {

// Copies the data the game takes straight from main.dol (tools/gen_pc_dol_data.py) out of the disc's
// DOL. Call after aurora_dvd_open and before the game starts; returns false if anything is missing.
bool LoadDolData();

} // namespace cf
