#pragma once
#ifndef YA_UFTP_RECEIVER_DETAIL_SESSION_CONTEXT_HPP_
#define	YA_UFTP_RECEIVER_DETAIL_SESSION_CONTEXT_HPP_

#include "boost/asio.hpp"
#include "detail/message.hpp"

namespace ya_uftp{
	namespace receiver{
		namespace detail{
			struct session_context{
				const boost::asio::ip::address	private_mcast_addr;
				const bool						is_open_group;
				std::uint32_t					in_group_id;
				const std::uint32_t				session_id;
				message::member_id				sender_id;
				std::uint8_t					task_instance;
				std::chrono::microseconds		grtt;
				std::uint16_t					msg_seq_num = 0u;
				const std::uint16_t				block_size;
				std::uint8_t					robust_factor;
				bool							follow_symbolic_link;
				const std::uint16_t				max_block_count_per_section = block_size * 8 > message::max_block_count_per_section ? message::max_block_count_per_section : block_size * 8;
				bool							quit_on_error;
				bool							encryption_enabled = false;
				//api::optional<std::uint64_t>	transfer_speed;
				std::uint8_t					retry_count = 0u;
				bool							register_confirmed = false;
				std::vector<api::fs::path>					destination_dirs;
				api::optional<std::vector<api::fs::path>>	temp_dirs;
				
				session_context(boost::asio::ip::address mcast_addr, bool open_group, 
					std::uint32_t session_id, message::member_id sender_id, std::uint16_t blk_size, std::uint8_t robust);
				session_context(const session_context&) = delete;
				session_context(session_context&&) = delete;
				session_context& operator=(const session_context&) = delete;
				session_context& operator=(session_context&&) = delete;
				~session_context();
			};
		}
	}
}

#endif
