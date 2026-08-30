#pragma once
// Single include point for this project's vendored MAVLink subset. Use this instead
// of reaching into common/mavlink.h directly - see readme.md in this directory for
// why `common` alone isn't enough (it has a hard dependency on the `standard`
// dialect for 2 messages this generator needs).
#include "common/mavlink.h"
