#pragma once
#ifndef YA_UFTP_SENDER_DETAIL_SESSION_CONTEXT_HPP_
#define YA_UFTP_SENDER_DETAIL_SESSION_CONTEXT_HPP_

#include "sender/adi.hpp"
#include "detail/message.hpp"

namespace ya_uftp{
	namespace sender{
		namespace detail{
			struct session_context {
				boost::asio::ip::udp::endpoint	public_mcast_dest;
				
				boost::asio::ip::udp::endpoint	private_mcast_dest;	
				const bool						is_open_group;
				std::uint32_t					in_group_id;
				std::uint32_t					session_id;
				std::uint8_t					task_instance;
				std::chrono::microseconds		grtt;
				std::uint16_t					msg_seq_num = 0u;
				const std::uint16_t				block_size;
				std::uint8_t					robust_factor;
				bool							follow_symbolic_link;
				const std::uint16_t				max_block_count_per_section = block_size * 8 > message::max_block_count_per_section ? message::max_block_count_per_section : block_size * 8;
				bool							quit_on_error;
				
				api::optional<std::uint64_t>	transfer_speed;
				
				struct receiver_properties{
					enum class status : std::uint8_t{
						mute,
						lost,
						abort,
						registered, // when a need to authenticated client's id is verified
						active,
						active_nak,
						done
					};
					
					explicit receiver_properties(status init_status);
					receiver_properties(const receiver_properties& other) = default;
					receiver_properties(receiver_properties&& other) = default;
					receiver_properties& operator=(const receiver_properties& rhs) = default;
					receiver_properties& operator=(receiver_properties&& rhs) = default;
					~receiver_properties() = default;
					status		current_status;
					bool		confirm_sent = false;
					bool		is_proxy = false;
					api::optional<std::chrono::microseconds> rtt;
				};
				
				std::map<message::member_id, receiver_properties>	receivers_properties;
				
				session_context(bool open_group, std::uint16_t blk_size);
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