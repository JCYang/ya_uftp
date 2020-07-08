#pragma once
#ifndef YA_UFTP_SENDER_DETAIL_WORKER_HPP_
#define YA_UFTP_SENDER_DETAIL_WORKER_HPP_

#include "sender/adi.hpp"
#include <random>
#include <queue>
#include <list>
#include "sender/detail/session_context.hpp"
#include "detail/common.hpp"

namespace ya_uftp{
	namespace sender{
		namespace detail{
			class worker {
				public:
					class employer {
					public:
						virtual void on_worker_bucket_freed() = 0;
						virtual void on_message_received(message::validated_packet valid_packet) = 0; 
						virtual ~employer();
					};
					using rw_handler = std::function<void(const boost::system::error_code, std::size_t)>;
					using send_args = std::tuple<message_blob, std::size_t, 
						const boost::asio::ip::udp::endpoint&, rw_handler>;
				private:
					boost::asio::io_context&		m_net_io_ctx;
					boost::asio::io_context&		m_file_io_ctx;
					boost::asio::ip::udp::socket	m_socket;
					boost::asio::steady_timer		m_rc_timer;
					boost::asio::ip::udp::endpoint	m_sender_endpoint;
					static std::random_device		m_rd;
					static std::uniform_int_distribution<std::uint32_t>		
						m_rd_number_dist;
					session_context					m_session_context;
					
					static constexpr std::size_t	m_rc_send_per_second = 20u;
					const std::uint32_t				m_bucket_full_size;
					std::uint32_t					m_queued_packets_total_length = 0u;
					std::uint64_t					m_rc_per_round_bytes_count;
					
					using blocked_packets_params = std::tuple<
						std::shared_ptr<std::vector<send_args>>, std::size_t, std::size_t, rw_handler>;
					api::optional<blocked_packets_params> 	m_blocked_packets_params;
					
					std::queue<send_args>			m_sendout_queue;
					std::weak_ptr<employer>			m_employer;
					std::list<std::weak_ptr<boost::asio::steady_timer>>
													m_job_timers;
					
					// FixMe: this is a perfect scenario for the application of coroutines (be it the TS or in the C++20),
					// but the library is not intend to be so *ADVANCED* right *NOW*, C++17 ready compiler should be supported.
					// maybe we can condiser turn it into a coroutine for more readable codes and ease of maintainence in the future
					std::pair<std::size_t, std::map<std::uint32_t, session_context::receiver_properties>::iterator>
						write_receivers_id(api::blob_span buffer, 
						std::map<std::uint32_t, session_context::receiver_properties>& receivers_states,
						std::map<std::uint32_t, session_context::receiver_properties>::iterator start,
						std::function<bool (session_context::receiver_properties&)> filter);
					bool try_init_server_id_from_addr(const boost::asio::ip::address& uni_addr, const task::parameters& params);
					
					static std::size_t do_complete_message(message_blob msg, std::function<std::size_t (api::blob_span)> write_body);
					std::pair<bool, std::size_t> send_multiple_packets(std::shared_ptr<std::vector<send_args>> packets,
						rw_handler result_handler = nullptr, std::size_t begin_idx = 0u);
				public:
					
					worker(boost::asio::io_context& net_io_ctx, 
						boost::asio::io_context& file_io_ctx,
						const task::parameters& params);
					worker(const worker&) = delete;
					worker(worker&&) = delete;
					worker& operator=(const worker&) = delete;
					worker& operator=(worker&&) = delete;
					~worker();
					void setup_header(message::protocol_header& uftp_hdr, message::role r);
					[[nodiscard]] 
					std::pair<bool, std::size_t> 
						send_packet(message_blob packet, 
						const boost::asio::ip::udp::endpoint& dest,
						std::function<std::size_t (api::blob_span)> write_body = nullptr, 
						api::optional<std::size_t> known_length = api::nullopt,
						rw_handler result_handler = nullptr) ;
						
					std::pair<bool, std::size_t> send_to_targeted_receivers(message_blob packet,
						const boost::asio::ip::udp::endpoint& dest,
						//std::map<std::uint32_t, session_context::receiver_properties>& receivers_states,
						std::function<bool (session_context::receiver_properties& state)> filter,
						rw_handler result_handler = nullptr);
						
					void learn_employer(std::weak_ptr<employer> boss);
					std::weak_ptr<employer> current_employer() const;
					
					void loop_read_packet();
					void loop_do_rc_send();
					void schedule_job_after(std::chrono::microseconds dura, std::function<void()> job);
					void cancel_all_jobs();
					void execute_in_file_thread(std::function<void()> job);
					void execute_in_net_thread(std::function<void()> job);
					
					void refine_grtt(std::function<bool(session_context::receiver_properties::status )> filter);
					session_context& get_context() ;
			};
		}
	}
}
#endif