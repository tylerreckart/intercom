#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alfred {

std::string expand_home(std::string path);
std::string make_turn_id();
bool file_exists(const std::string& path);
bool executable_on_path_or_file(const std::string& binary);

// Write mono s16le PCM as a minimal WAV file.
bool write_wav_s16le(const std::string& path,
                     const std::vector<std::uint8_t>& pcm,
                     int sample_rate,
                     int channels);

// Read WAV (PCM s16le) into raw PCM bytes; returns sample rate via out param.
std::optional<std::vector<std::uint8_t>> read_wav_s16le(const std::string& path,
                                                        int* out_sample_rate,
                                                        int* out_channels);

// Cubic resample mono s16le PCM.
std::vector<std::uint8_t> resample_s16le_mono(const std::vector<std::uint8_t>& pcm,
                                              int from_rate,
                                              int to_rate);

std::string to_lower(std::string s);
std::string trim(std::string_view s);

// Pull completed sentences from a growing buffer; leaves incomplete tail in buf.
std::vector<std::string> flush_sentences(std::string& buf, bool final_flush);

}  // namespace alfred
