#pragma once

namespace cf {

// Runs the static initializers of every REL (src/cf/include/cf/rel_static_init.h), as their prologs
// would when linked. Only the first call does anything.
void RunRelStaticInitializers();

} // namespace cf
