// Private JSON helpers for the standalone parity fixture executable.
#pragma once

#include <boost/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace arb::parity {

constexpr long double WAD = 1e18L;
constexpr long double FEE_SCALE = 1e10L;

inline bool is_number_or_string(const boost::json::value& value) {
    return value.is_string() || value.is_double() || value.is_int64() || value.is_uint64();
}

// Keep integer JSON literals exact and round-trip JSON doubles at binary64
// precision.  Exact-mode callers use the resulting decimal as uint256 input;
// floating callers materialize through parse_input_double below.
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

// Pool/config inputs have a binary64 precision boundary, independent of the
// pool's arithmetic type.  A long-double simulation widens the resulting
// double; it must not recover extra bits by parsing source decimals as long
// double.
inline double parse_input_double(const boost::json::value& value) {
    if (value.is_string()) return std::strtod(value.as_string().c_str(), nullptr);
    if (value.is_double()) return value.as_double();
    if (value.is_int64()) return static_cast<double>(value.as_int64());
    if (value.is_uint64()) return static_cast<double>(value.as_uint64());
    throw std::runtime_error("expected a number or string");
}

template <typename T>
inline std::string to_str_1e18(T value) {
    long double scaled = static_cast<long double>(value) * WAD;
    if (!std::isfinite(scaled)) scaled = 0;
    if (scaled < 0) scaled = 0;
    const auto rounded = std::floor(scaled + 0.5L);
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(0);
    stream << rounded;
    return stream.str();
}

template <typename T>
inline std::string to_int_string(T value) {
    long double converted = static_cast<long double>(value);
    if (!std::isfinite(converted)) converted = 0;
    if (converted < 0) converted = 0;
    const auto rounded = std::floor(converted + 0.5L);
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(0);
    stream << rounded;
    return stream.str();
}

template <typename T>
inline T parse_plain_real(const boost::json::value& value) {
    return static_cast<T>(parse_input_double(value));
}

template <typename T>
inline T parse_scaled_1e18(const boost::json::value& value) {
    return static_cast<T>(parse_input_double(value) / 1e18);
}

template <typename T>
inline T parse_fee_1e10(const boost::json::value& value) {
    return static_cast<T>(parse_input_double(value) / 1e10);
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

inline uint64_t get_u64_opt(
    const boost::json::object& object,
    const char* key,
    uint64_t default_value
) {
    const auto it = object.find(key);
    if (it == object.end()) return default_value;
    const auto& value = it->value();
    if (value.is_uint64()) return value.as_uint64();
    if (value.is_int64()) return static_cast<uint64_t>(value.as_int64());
    if (value.is_string()) {
        try {
            return static_cast<uint64_t>(std::stoull(std::string(value.as_string().c_str())));
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
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

