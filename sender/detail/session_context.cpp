#include "sender/detail/session_context.hpp"

namespace ya_uftp{
	namespace sender{
		namespace detail{
			session_context::session_context(bool open_group, std::uint16_t blk_size) :
				is_open_group(open_group), block_size(blk_size){}
				
			
			session_context::~session_context() = default;
			
			session_context::receiver_properties::receiver_properties(status init_status) : current_status(init_status){}
		}
	}
}