#pragma once
#ifndef YA_UFTP_RECEIVER_DETAIL_WORKER_HPP_
#define YA_UFTP_RECEIVER_DETAIL_WORKER_HPP_

#include "receiver/adi.hpp"
#include "receiver/detail/session_context.hpp"
#include "detail/common.hpp"

#include <list>
#include <random>

namespace ya_uftp{
	namespace receiver{
		namespace detail{
			class worker {
				public:
					class employer {
					public:
						// return the number of times of GRTT for signal lost timer
						virtual std::uint8_t on_message_received(message::validated_packet valid_packet) = 0; 
						virtual ~employer();
					};
					using rw_handler = std::function<void(const boost::system::error_code, std::size_t)>;
				private:
					boost::asio::io_context&		m_net_io_ctx;
					boost::asio::io_context&		m_file_io_ctx;
					boost::asio::ip::udp::socket	m_socket;
					boost::asio::ip::udp::endpoint	m_sender_endpoint;
					boost::asio::ip::udp::endpoint	m_source_ep;
					boost::asio::steady_timer		m_timeout_timer;
					std::chrono::steady_clock::time_point	m_last_msg_recv_time;
					static std::random_device		m_rd;
					static std::uniform_int_distribution<std::uint32_t>		
						m_rd_number_dist;
					session_context					m_session_context;
					
					std::weak_ptr<employer>			m_employer;
					std::list<std::weak_ptr<boost::asio::steady_timer>>
													m_job_timers;
					
					bool try_init_in_group_id_from_addr(const boost::asio::ip::address& uni_addr, const task::parameters& params);
					
					static std::size_t do_complete_message(message_blob msg, std::function<std::size_t (api::blob_span)> write_body);
					
				public:
					worker(boost::asio::io_context& net_io_ctx,
						boost::asio::io_context& file_io_ctx,
						boost::asio::ip::address private_mcast_addr,
						boost::asio::ip::udp::endpoint	sender_ep,
						bool open_group, std::uint32_t session_id, 
						message::member_id sender_id,
						std::uint16_t blk_size, std::uint8_t robust,
						task::parameters& params);
					worker(const worker&) = delete;
					worker(worker&&) = delete;
					worker& operator=(const worker&) = delete;
					worker& operator=(worker&&) = delete;
					~worker();
					void setup_header(message::protocol_header& uftp_hdr, message::role r);
					[[nodiscard]] 
					std::pair<bool, std::size_t> 
						send_packet(message_blob packet, 
						std::function<std::size_t (api::blob_span)> write_body = nullptr, 
						api::optional<std::size_t> known_length = api::nullopt,
						rw_handler result_handler = nullptr) ;
					
					void learn_employer(std::weak_ptr<employer> boss);
					std::weak_ptr<employer> current_employer() const;
					
					void loop_read_packet(bool remember_employer = false, 
						api::optional<std::uint8_t> timeout_factor = api::nullopt);
					
					void schedule_job_after(std::chrono::microseconds dura, std::function<void()> job);
					void cancel_all_jobs();
					void execute_in_file_thread(std::function<void()> job);
					void execute_in_net_thread(std::function<void()> job);
					
					session_context& get_context() ;
			};
		}
	}
}
#endif