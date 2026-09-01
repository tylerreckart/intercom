#pragma once

namespace intercom {

// Sent once per device conversation at Intercom boot so the local model
// prefills Arthur's constitution before the first real PTT.
inline constexpr const char* kPrefixWarmMessage = "PREFIX WARM";

}  // namespace intercom
