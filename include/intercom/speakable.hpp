#pragma once

#include <string>
#include <string_view>

namespace intercom {

// Rewrite assistant text so TTS speaks it the way a person would say it:
// no markdown, no LaTeX, no symbol names like "backslash" or "asterisk".
std::string to_speakable(std::string_view text);

}  // namespace intercom
