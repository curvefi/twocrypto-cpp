// Private JSON helpers for the exact uint256 parity executable.
#pragma once

#include <boost/json.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace arb::parity {

inline bool is_number_or_string(const boost::json::value& value) {
    return value.is_string() || value.is_double() || value.is_int64() || value.is_uint64();
}

// Preserve decimal input text and integer JSON literals exactly. JSON doubles
// are rendered with their full binary64 precision before uint256 conversion.
inline std::string scalar_to_string(const boost::json::value& value) {
    if (value.is_string()) return std::string(value.as_string().c_str());
    if (value.is_int64()) return std::to_string(value.as_int64());
    if (value.is_uint64()) return std::to_string(value.as_uint64());
    if (value.is_double()) {
        std::ostringstream stream;
        stream << std::setprecision(17) << value.as_double();
        return stream.str();
    }
    throw std::runtime_error("expected a number or string");
}


inline std::string get_str(const boost::json::object& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        throw std::runtime_error(std::string("missing key: ") + key);
    }
    if (!it->value().is_string()) {
        throw std::runtime_error(std::string("expected string for key: ") + key);
    }
    return std::string(it->value().as_string().c_str());
}

inline uint64_t parse_u64_value(const boost::json::value& value, const char* key) {
    if (value.is_uint64()) return value.as_uint64();
    if (value.is_int64()) {
        if (value.as_int64() < 0) {
            throw std::runtime_error(std::string("expected nonnegative uint64 for key: ") + key);
        }
        return static_cast<uint64_t>(value.as_int64());
    }
    if (value.is_string()) {
        const std::string_view raw(value.as_string().data(), value.as_string().size());
        if (raw.empty()) {
            throw std::runtime_error(std::string("expected nonnegative uint64 for key: ") + key);
        }
        uint64_t parsed = 0;
        const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size()) {
            throw std::runtime_error(std::string("expected nonnegative uint64 for key: ") + key);
        }
        return parsed;
    }
    throw std::runtime_error(std::string("expected integer for key: ") + key);
}

inline uint64_t get_u64_opt(
    const boost::json::object& object,
    const char* key,
    uint64_t default_value
) {
    const auto it = object.find(key);
    if (it == object.end()) return default_value;
    return parse_u64_value(it->value(), key);
}

inline uint64_t env_u64(const char* key, uint64_t default_value) {
    if (const char* value = std::getenv(key)) {
        try {
            return static_cast<uint64_t>(std::stoull(value));
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

inline bool env_flag(const char* key) {
    if (const char* value = std::getenv(key)) {
        return std::string(value) == "1";
    }
    return false;
}

inline std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    input.seekg(0, std::ios::end);
    const std::streampos end_pos = input.tellg();
    if (end_pos < 0) {
        throw std::runtime_error("Cannot stat file: " + path);
    }
    input.seekg(0, std::ios::beg);
    std::string data;
    data.resize(static_cast<size_t>(end_pos));
    if (!data.empty()) {
        input.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (input.gcount() != static_cast<std::streamsize>(data.size())) {
            throw std::runtime_error("Short read on file: " + path);
        }
    }
    return data;
}

} // namespace arb::parity
