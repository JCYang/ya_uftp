#pragma once
#ifndef YA_UFTP_DETAIL_COMMON_HPP_
#define YA_UFTP_DETAIL_COMMON_HPP_

#include <memory>
#include <vector>
#include <set>
#include <iostream>

#ifdef _WIN32
template<typename PATH>
std::string to_u8string(PATH&& p){
	auto wp = p.wstring();
	auto u8buf = std::vector<char>{};
	auto bufsize = WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), wp.size(), nullptr, 0, nullptr, nullptr);
	u8buf.reserve(bufsize);
	auto bytes_converted = WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), wp.size(), u8buf.data(), bufsize, nullptr, nullptr);
	if (bytes_converted > 0)
		std::cout << "WCTMB convert the path to " << bytes_converted << " bytes\n";
	else
		std::cout << "WCTMB failed to convered because of error code " << std::hex << GetLastError() << '\n'; 
	
	auto u8str = std::string{u8buf.data(), u8buf.capacity()};
	std::cout << "u8str length is " << u8str.length() << '\n'; 
	std::cout << "File path utf8 is " << u8str << '\n';
	return u8str;
}
#else
template<typename PATH>
std::string to_u8string(PATH&& p){
	return p.string();
}
#endif

namespace ya_uftp{
	template<std::size_t S = sizeof(void*)>
	struct machine_native;
	
	template<>
	struct machine_native<4>{
		using uint = std::uint32_t;
	};
	
	template<>
	struct machine_native<8>{
		using uint = std::uint64_t;
	};
	
	using message_blob = std::shared_ptr<std::vector<std::uint8_t>>;
	
	template<typename... Args>
	inline auto make_message_blob(Args&&... args) -> decltype(std::make_shared<std::vector<std::uint8_t>>(std::forward<Args>(args)...)){
		return std::make_shared<std::vector<std::uint8_t>>(std::forward<Args>(args)...);
	}
	
	template<typename K>
	void merge_2nd_set(std::set<K>& first, const std::set<K>& second){
		for (auto item : second)
			first.emplace(item);
	}
}

#endif