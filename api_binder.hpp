#pragma once
#ifndef YA_UFTP_API_BINDER_HPP_
#define YA_UFTP_API_BINDER_HPP_

#include <chrono>
// string_view
#if __has_include(<string_view>)
#include <string_view>
namespace api{
using std::basic_string_view;
using blob_view = std::basic_string_view<std::uint8_t>;
}
#elif __has_include(<experimental/string_view>)
#include <experimental/string_view>
namespace api{
using std::experimental::basic_string_view;
using blob_view = std::experimental::basic_string_view<std::uint8_t>;
}
#else
#include "boost/utility/string_view.hpp"
namespace api{
using boost::basic_string_view;
using blob_view = boost::basic_string_view<std::uint8_t>;
}
#endif

// end of string_view

// optional
#if __has_include(<optional>)
#include <optional>
namespace api{
using std::optional;
using std::nullopt;
using std::in_place;
}
#elif __has_include(<experimental/optional>)
#include <experimental/optional>
namespace api{
using std::experimental::optional;
using std::experimental::nullopt;
using std::experimental::in_place;
}
#else
#include "boost/optional.hpp"
namespace api{
using boost::optional;
constexpr auto nullopt = boost::none;
using boost::optional::in_place;
}
#endif
// end of optional

// variant
#if __has_include(<variant>)
#include <variant>
namespace api{
using std::variant;
using std::holds_alternative;
using std::get;
}
#else
#include "boost/variant.hpp"
namespace api{
using boost::variant;
template <typename T, typename... Ts>
bool holds_alternative(const boost::variant<Ts...>& v) noexcept
{
    return boost::get<T>(&v) != nullptr;
}
using boost::get;
}
#endif
// end of variant

// span
/*
#if __has_include(<span>)
#include <span>
namespace api{
using std::span;
using blob_span = span<std::uint8_t>;
}
#else
*/
#include <gsl/span>
namespace api{
using gsl::span;
using blob_span = span<std::uint8_t>;
}
//#endif

// filesystem
#if __has_include(<filesystem>) && __cpp_lib_chrono >= 201907
#include <filesystem>
namespace api{
namespace fs = std::filesystem;
using error_code = std::error_code;
using errc = std::errc;

#define HAS_STD_FILE_TIME_TYPE 1
std::uint64_t convert_file_time(const api::fs::file_time_type& ft);
api::fs::file_time_type convert_file_time(const std::uint64_t ft);
}
#elif __has_include("boost/filesystem.hpp")
#include "boost/filesystem.hpp"
namespace api{
namespace fs = boost::filesystem;
using error_code = boost::system::error_code;
using errc = boost::system::errc::errc_t;

#ifdef HAS_STD_FILE_TIME_TYPE
#undef HAS_STD_FILE_TIME_TYPE
#endif
std::uint64_t convert_file_time(const std::time_t ft);
std::time_t convert_file_time(const std::uint64_t ft);
}
#endif

#ifdef _MSC_VER
#include <iso646.h>
#endif

#endif