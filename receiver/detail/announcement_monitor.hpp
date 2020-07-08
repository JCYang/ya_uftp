#pragma once
#ifndef YA_UFTP_RECEIVER_DETAIL_ANNOUNCEMENT_MONITOR_HPP_
#define YA_UFTP_RECEIVER_DETAIL_ANNOUNCEMENT_MONITOR_HPP_

#include "receiver/adi.hpp"
#include "detail/common.hpp"
#include "detail/message.hpp"

#include "receiver/detail/files_accept_session.hpp"
#include <array>

namespace ya_uftp::receiver::detail{
	class announcement_monitor :
		public std::enable_shared_from_this<announcement_monitor>{
		struct private_ctor_tag{};
		struct session_prop {
			std::uint32_t ts_high;
			std::uint32_t ts_low;
			std::weak_ptr<files_accept_session>		pointer;
		};

		boost::asio::ip::udp::socket	m_socket;
		boost::asio::io_context&		m_net_io_ctx;
		boost::asio::io_context&		m_file_io_ctx;
		std::array<std::uint8_t, 1500>	m_msg_buffer;
		boost::asio::ip::udp::endpoint	m_sender_ep;
		task::parameters				m_params;
		std::map<boost::asio::ip::address, session_prop>	m_known_announcements;
		
		void do_monitor_announcement();
	public:
		announcement_monitor(boost::asio::io_context& net_io_ctx, 
			boost::asio::io_context& file_io_ctx,
			const task::parameters& params,  
			private_ctor_tag tag);
			
		void run();
		void stop();
		static std::shared_ptr<announcement_monitor> 
			create(
				boost::asio::io_context& net_io_ctx,
				boost::asio::io_context& file_io_ctx,
				const task::parameters& params);
	};
}

#endif
