#include "string_utils.h"
#include <cctype>

std::string to_lower(const std::string& s) {
    std::string result;
    result.reserve(s.length());
    for (char c : s) {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

bool has_extension(const fs::path& path, const std::string& ext) {
    return to_lower(path.extension().string()) == ext;
}
