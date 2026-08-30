#pragma once
// Single include point for this project's vendored MAVLink subset. Use this instead
// of reaching into common/mavlink.h directly - see readme.md in this directory for
// the `standard` dialect dependency and the borrowed-message exceptions below.
#include "common/mavlink.h"

// LED_CONTROL (id 186) lives in the `ardupilotmega` dialect upstream, not `common` -
// borrowed the same way GLOBAL_POSITION_INT/AUTOPILOT_VERSION were borrowed from
// `standard` (see readme.md). Self-contained (own LEN/CRC defines), so this single
// extra include is all that's needed rather than vendoring all of `ardupilotmega`.
#include "common/mavlink_msg_led_control.h"
