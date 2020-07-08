#include "receiver/detail/session_context.hpp"

namespace ya_uftp{
	namespace receiver{
		namespace detail{
			session_context::session_context(boost::asio::ip::address mcast_addr, 
				bool open_group, std::uint32_t ss_id, message::member_id sdr_id,
				std::uint16_t blk_size, std::uint8_t robust)
				: private_mcast_addr(mcast_addr), is_open_group(open_group), 
				session_id(ss_id), sender_id(sdr_id), block_size(blk_size), robust_factor(robust) {}
				
			
			session_context::~session_context() = default;
		}
	}
}
