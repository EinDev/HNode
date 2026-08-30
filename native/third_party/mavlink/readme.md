# Vendored: mavlink/c_library_v2

Header-only, generated C MAVLink 2 implementation, vendored from:
https://github.com/mavlink/c_library_v2
commit `663f9109025433f241ee9fef60e0dbbec64a6cea` (2026-08-27)

Only the `common`, `standard`, and `minimal` dialects are included. The other ~30
dialect directories upstream (ardupilotmega, etc.) are not needed and were not copied.
Each dialect's `testsuite.h` (self-test code, not needed to use the library) was also
dropped.

Used the same way this project vendors Spout2 (`native/third_party/Spout`) and stb
(`native/third_party/stb`) - included directly via the compiler include path, no
vcpkg package exists for this. See the upstream repository for license terms.

`standard` is vendored too even though MAVLinkDroneNetworkGenerator (this project's
only MAVLink user) is otherwise a `common`-dialect consumer: in this upstream commit,
`common/common.h` unconditionally does `#include "../standard/standard.h"` as its
"base include" (GLOBAL_POSITION_INT and AUTOPILOT_VERSION - both needed here - are
defined in `standard.xml`, not `common.xml`, and `common.h`'s own
`MAVLINK_MESSAGE_INFO` table references both by name). So `common/mavlink.h` simply
won't compile without a sibling `standard/` directory present - it's a hard
dependency, not an optional extra. `standard/mavlink.h` and `standard/version.h`
aren't themselves included by anything here (only `standard/standard.h`, via that
relative include) but are vendored alongside it for completeness/future updates.

One more borrowed message: `common/mavlink_msg_led_control.h` was copied from the
`ardupilotmega` dialect (upstream: `ardupilotmega/mavlink_msg_led_control.h`) -
MAVLinkDroneNetworkGenerator needs LED_CONTROL (id 186) to detect a "flash" command,
but that message isn't defined in `common`, `standard`, or `minimal` at all. Same
self-contained-per-message-header property as the other 2 borrowed files, so it's
just dropped into `common/` and `#include`d explicitly from `mavlink_all.h` (not part
of `common.h`'s own include chain, unlike the `standard`-borrowed pair) - vendoring
all of `ardupilotmega` (which is large) for one message wasn't worth it.

To update: re-clone upstream, sparse-checkout `common` and `minimal`, copy over
(minus `testsuite.h`), and update the commit hash above.
