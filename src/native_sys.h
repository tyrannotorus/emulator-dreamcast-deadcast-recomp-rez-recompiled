// Native devices: Shinobi's hardware-facing functions, replaced by native code.
#pragma once

namespace dream {
class System;
}

namespace rez {

// Once, before the first guest instruction.
void native_sys_start(dream::System& sys);

}  // namespace rez
