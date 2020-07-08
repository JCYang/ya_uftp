#include "ya_uftp.hpp"
#include "detail/core.hpp"

namespace ya_uftp {
	void set_max_threads_count(api::optional<std::size_t> mtc){
		core::detail::execution_unit::set_max_thread_count(mtc);
	}
}