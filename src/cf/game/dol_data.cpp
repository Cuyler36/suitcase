// Game data that the decomp references but does not define in source: in the MWCC build it comes from
// the original binary's data sections (config/<version>/symbols.txt).
#include <game/framework/f_profile.hpp>
#include <game/mLib/m_video.hpp>

// .sbss: zero until set at run time
mVideo *mVideo::m_video;
// Set by the profile module's prolog on the Wii (dProfile::install); the PC startup has to do the same
const fProfListMg_c::fProfilePtr_c (*fProfListMg_c::m_data_p)[fProf::PROFILE_COUNT];
