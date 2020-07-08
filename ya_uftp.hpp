#pragma once
#ifndef YA_UFTP_HPP_
#define YA_UFTP_HPP_

#include "sender/server.hpp"
#include "receiver/server.hpp"

namespace ya_uftp {
	void set_max_threads_count(api::optional<std::size_t> mtc);
}

#endif
