#pragma once

#include <string>
#include <string_view>

namespace alfred {

// Rewrite assistant text so Piper speaks it the way a person would say it:
// no markdown, no LaTeX, no symbol names like "backslash" or "asterisk".
std::string to_speakable(std::string_view text);

// User turn sent to Arbiter: keep the spoken question first, then a reminder
// that the reply will be read aloud.
std::string voice_user_message(std::string_view transcript);

}  // namespace alfred
