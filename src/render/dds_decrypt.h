#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dds {

// Decrypts a DNG ("rc00") container. If the input is already a plain DDS file
// (starts with "DDS "), it is copied as-is. key_name is the file name that the
// game uses to derive the container key (case-insensitive basename).
bool DecryptDng(const std::vector<uint8_t>& input, const std::string& key_name, std::vector<uint8_t>& output);

} // namespace dds
