#pragma once
#ifndef YA_UFTP_RECEIVER_DETAIL_FILES_ACCEPT_SESSION_HPP_
#define YA_UFTP_RECEIVER_DETAIL_FILES_ACCEPT_SESSION_HPP_

#include "receiver/adi.hpp"
#include "detail/common.hpp"
#include "detail/message.hpp"
#include "receiver/detail/worker.hpp"

#include <memory>

namespace ya_uftp{
	namespace receiver{
		namespace detail{
			class files_accept_session : 
				public std::enable_shared_from_this<files_accept_session>,
				public worker::employer {
				struct private_ctor_tag{};
				enum class phase : std::uint8_t {
					registering,
					receiving,
					completed
				};
			public:
				class file_receive_task;
				class visa {
					friend class file_receive_task;
				};

				files_accept_session(
					boost::asio::io_context& net_io_ctx,
					boost::asio::io_context& file_io_ctx,
					boost::asio::ip::address private_mcast_addr,
					boost::asio::ip::udp::endpoint	sender_ep,
					bool open_group, std::uint16_t blk_size, std::uint8_t robust,
					const std::uint32_t session_id,
					message::member_id	sender_id,
					const std::uint32_t& announce_ts_high,
					const std::uint32_t& announce_ts_low,
					task::parameters& params,
					private_ctor_tag tag);
					
				static std::shared_ptr<files_accept_session>
					create(boost::asio::io_context& net_io_ctx, 
						boost::asio::io_context& file_io_ctx,
						boost::asio::ip::address private_mcast_addr,
						boost::asio::ip::udp::endpoint	sender_ep,
						bool open_group, std::uint16_t blk_size, std::uint8_t robust,
						const std::uint32_t session_id,
						message::member_id	sender_id,
						const std::uint32_t& announce_ts_high,
						const std::uint32_t& announce_ts_low,
						task::parameters& params);
				
				void start();
				void stop();
				std::uint8_t on_message_received(message::validated_packet valid_packet) override;
				
				void on_file_receive_complete(visa v, 
					message_blob next_file_info = nullptr);
				void on_file_receive_error(bool fatal, visa v);
				void on_whole_session_end(visa v);
				~files_accept_session();
			private:
				void do_register();
				void do_report_completed();
				void on_regconf_received(api::blob_span packet, message::member_id source_id);
				void on_done_conf_received(api::blob_span packet, message::member_id source_id);
				
				std::unique_ptr<worker>		m_worker;
				session_context&			m_context;
				phase						m_phase;
				const std::uint32_t&		m_last_announce_ts_high;
				const std::uint32_t&		m_last_announce_ts_low;
			public:

			};
		}
	}
}

#endif
