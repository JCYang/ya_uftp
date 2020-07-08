#include "api_binder.hpp"

namespace api{
#ifdef HAS_STD_FILE_TIME_TYPE
	std::uint64_t convert_file_time(const api::fs::file_time_type& ft){
		auto utc_ts = std::chrono::file_clock::to_utc(ft);
        return std::chrono::duration_cast<std::chrono::seconds>(utc_ts.time_since_epoch()).count();
    }
    api::fs::file_time_type convert_file_time(const std::uint64_t ft)
    {
        auto utc_ts = std::chrono::utc_time{std::chrono::seconds(ft)};
        return std::chrono::file_clock::from_utc(utc_ts);
    }
#else
	std::uint64_t convert_file_time(const std::time_t ft){
		return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::from_time_t(ft).time_since_epoch()).count();
	}
    std::time_t convert_file_time(const std::uint64_t ft)
    {
        return std::chrono::system_clock::to_time_t(std::chrono::system_clock::time_point{std::chrono::seconds(ft)});
    }
#endif

}
