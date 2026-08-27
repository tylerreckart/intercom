#pragma once

#include <ctime>
#include <string>

namespace intercom {

std::tm local_now();

// Constitution line injected on every Arbiter turn. Not stored as user text.
std::string local_clock_rule();

std::string spoken_time_now();
std::string spoken_date_now();

}  // namespace intercom
