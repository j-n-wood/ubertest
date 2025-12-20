#ifndef SHARED_STRING_UTILS_H
#define SHARED_STRING_UTILS_H

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// Convert string to lowercase
std::string to_lower(const std::string& s);

// Check if path has specific extension (case-insensitive)
// ext should include leading dot, e.g., ".asc"
bool has_extension(const fs::path& path, const std::string& ext);

#endif // SHARED_STRING_UTILS_H
