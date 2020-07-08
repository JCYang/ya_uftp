#include "sender/detail/worker.hpp"

#include "boost/endian/conversion.hpp"

#include <iostream>

namespace ya_uftp{
	namespace sender{
		namespace detail{
			std::random_device		worker::m_rd;
			std::uniform_int_distribution<std::uint32_t>		
				worker::m_rd_number_dist = std::uniform_int_distribution<std::uint32_t>	{0, UINT32_MAX};
			
			worker::employer::~employer() = default;
			
			worker::worker(boost::asio::io_context& net_io_ctx, 
						boost::asio::io_context& file_io_ctx,
					const task::parameters& params)
				: m_net_io_ctx(net_io_ctx), m_file_io_ctx(file_io_ctx), 
				m_socket(m_net_io_ctx),
				m_rc_timer(m_net_io_ctx),
				m_session_context((params.allowed_clients and not params.allowed_clients->empty()) ? false : true, 
					params.block_size),
				// set the bucket size to 1.25 times everycycle consumed to avoid drain
				m_bucket_full_size(params.max_speed ? (params.max_speed.value() / m_rc_send_per_second / 4 * 5) : 0){
					m_session_context.quit_on_error = params.quit_on_error;
					m_session_context.public_mcast_dest = boost::asio::ip::udp::endpoint{params.public_multicast_addr, params.destination_port};
					m_session_context.private_mcast_dest = boost::asio::ip::udp::endpoint{params.private_multicast_addr, params.destination_port};
					m_session_context.grtt = params.grtt; 
					m_session_context.transfer_speed = params.max_speed;
					if (m_session_context.transfer_speed)
						m_rc_per_round_bytes_count = m_session_context.transfer_speed.value() / 20;
					
					if (params.public_multicast_addr.is_v4()){
						auto ec = boost::system::error_code{};
						m_socket.open(boost::asio::ip::udp::v4(), ec);
						if (not ec and params.source_port){
							m_socket.bind(boost::asio::ip::udp::endpoint{
								boost::asio::ip::address_v4::any(),
								params.source_port.value()
							}, ec);
						}
					}
					else{
						auto ec = boost::system::error_code{};
						m_socket.open(boost::asio::ip::udp::v6(), ec);
						if (not ec and params.source_port){
							m_socket.bind(boost::asio::ip::udp::endpoint{
								boost::asio::ip::address_v6::any(),
								params.source_port.value()
							}, ec);
						}
					}
					
					// fixup unspecified parameters
					if (not params.server_id){
						auto server_id_set = false;
						if (params.out_interface_id){
							auto usr_specified_intf = decltype(jcy::network::interface::by_index(1)){};
							if (api::holds_alternative<int>(params.out_interface_id.value()))
								usr_specified_intf = jcy::network::interface::by_index(api::get<int>(params.out_interface_id.value()));
							else if (api::holds_alternative<std::string>(params.out_interface_id.value()))
								usr_specified_intf = jcy::network::interface::by_name(api::get<std::string>(params.out_interface_id.value()));
							else if (api::holds_alternative<boost::asio::ip::address>(params.out_interface_id.value())){
								
							}
							
							if (usr_specified_intf){
								for (auto uni_addr : usr_specified_intf.value().get().unicast_addresses()){
									server_id_set = try_init_server_id_from_addr(uni_addr, params);
									if (server_id_set)
										break;
								}
							}
							else{
								// ToDo: report error interface specified???
							}		
						}
						else{
							auto& intf_info = jcy::network::interface::retrieve_all();
							for (auto& intf : intf_info){
								if (not intf.is_loopback() and intf.multicast_enabled()){
									for (auto& uni_addr : intf.unicast_addresses()){
										server_id_set = try_init_server_id_from_addr(uni_addr, params);
										if (server_id_set)
											break;
									}
									if (server_id_set)
										break;
								}
							}
						}
						// ToDo: we need to error out when the server_id is unknown
						if (not server_id_set)
							throw std::runtime_error("server_id unknown!!!");
					}
					else
						m_session_context.in_group_id = boost::endian::native_to_big(params.server_id.value());
					
					m_session_context.session_id = boost::endian::native_to_big(m_rd_number_dist(m_rd));
					m_session_context.task_instance = 0u;
					m_session_context.robust_factor = params.robust_factor;
					m_session_context.follow_symbolic_link = params.follow_symbolic_link;
				}
			
			
			worker::~worker(){
				m_socket.close();
			}
			
			bool worker::try_init_server_id_from_addr
				(const boost::asio::ip::address& uni_addr, const task::parameters& params){
				auto success = false;
				if (uni_addr.is_v4() == params.public_multicast_addr.is_v4()){
					success = true;
					if (uni_addr.is_v4()){
						auto src_id = uni_addr.to_v4().to_bytes();
						auto uint_id = 0u;
						std::copy(src_id.begin(), src_id.end(), reinterpret_cast<std::uint8_t*>(&uint_id));
						m_session_context.in_group_id = uint_id;
					}
					else {
						auto src_id = uni_addr.to_v6().to_bytes();
						auto uint_id = 0u;
						std::copy(src_id.begin() + 12, src_id.end(), reinterpret_cast<std::uint8_t*>(&uint_id));
						m_session_context.in_group_id = uint_id;
					}
				}
				return success;
			}
			// FixMe: this is a perfect scenario for the application of coroutines (be it the TS or in the C++20),
			// but the library is not intend to be so *ADVANCED* right *NOW*, C++17 ready compiler should be supported.
			// maybe we can condiser turn it into a coroutine for more readable codes and ease of maintainence in the future
			std::pair<std::size_t, std::map<std::uint32_t, session_context::receiver_properties>::iterator>
				worker::write_receivers_id(api::blob_span buffer,
					std::map<std::uint32_t, session_context::receiver_properties>& receivers_states,
					std::map<std::uint32_t, session_context::receiver_properties>::iterator start,
					std::function<bool (session_context::receiver_properties&)> filter){
				auto ids_buf = api::span<std::uint32_t>{reinterpret_cast<std::uint32_t *>(buffer.data()), 
					static_cast<api::span<std::uint32_t>::size_type>(buffer.size() / sizeof(std::uint32_t))}; 
				auto i = 0u;
				auto iter = start;
				for (; iter != receivers_states.end() && i < ids_buf.size(); iter++){
					if (filter(iter->second) &&
						i < ids_buf.size()){
						ids_buf[i++] = iter->first;
					}
				}
				return {i * sizeof(std::uint32_t), iter};
			}
			
			void worker::
				loop_do_rc_send(){
				auto bytes_sent = 0u;
				m_rc_timer.expires_after(std::chrono::milliseconds(50));
				m_rc_timer.async_wait([this](const boost::system::error_code ec){
					if (not ec)
						loop_do_rc_send();
				});
				
				while (not m_sendout_queue.empty() and
						bytes_sent < m_rc_per_round_bytes_count){
					auto [msg, length, dest, handler] = m_sendout_queue.front();
					m_socket.async_send_to(boost::asio::buffer(msg->data(), length),
						dest, [this, handler = std::move(handler), msg = msg](const boost::system::error_code ec, std::size_t bytes_sent){
							if (handler)
								handler(ec, bytes_sent);
						});
					bytes_sent += length;
					m_queued_packets_total_length -= length;
					m_sendout_queue.pop();
				}
				if (m_queued_packets_total_length < m_bucket_full_size){
					/*
					for (auto employer_weak : m_employers){
						auto boss = employer_weak.lock();
						if (boss)
							boss->on_worker_bucket_freed();
						
					}
					*/
					auto can_tell_boss = true;
					if (m_blocked_packets_params){
						auto [pkts, idx, total_sent_size, handler] = m_blocked_packets_params.value();
						auto [all_sent, bytes_sent] = send_multiple_packets(pkts, handler, idx);
						
						can_tell_boss = all_sent;
					}
					if (can_tell_boss){
						auto boss = m_employer.lock();
						if (boss)
							boss->on_worker_bucket_freed();
					}
				}
			}
			
			std::size_t worker::
				do_complete_message(message_blob msg, std::function<std::size_t (api::blob_span)> write_body){
				
				auto msg_length = *(msg->data() + sizeof(message::protocol_header) + 1) * message::header_length_unit + sizeof(message::protocol_header);
				if (write_body)
					msg_length += write_body(api::blob_span{msg->data() + msg_length, 
						static_cast<api::blob_span::size_type>(msg->size() - msg_length)});
				return msg_length;
			}
			
			std::pair<bool, std::size_t>
				worker::send_multiple_packets(std::shared_ptr<std::vector<send_args>> packets,
						rw_handler result_handler, std::size_t begin_idx){
				auto total_bytes_sent = 0u;
				auto all_sent = false;
				auto idx = begin_idx;
				for (; idx < packets->size();){
					auto success = false;
					auto sent_bytes = 0u;
					auto [msg, len, dest, handler] = packets->at(idx);
					if (idx == packets->size() - 1){
						auto [sent, msg_len] = send_packet(msg, dest, nullptr, len, 
						[total_bytes_sent, result_handler](const boost::system::error_code ec, std::size_t bytes_written){
							if (result_handler)
								result_handler(ec, total_bytes_sent + bytes_written);
						});
						
						success = sent;
						sent_bytes = msg_len;
					}
					else{
						auto [sent, msg_len] = send_packet(msg, dest, nullptr, len, 
						[](const boost::system::error_code ec, std::size_t bytes_written){
							//if (not ec)
								//send_multiple_packets(packets, idx, bytes_sent + bytes_written, res_handler);
							
						});
						success = sent;
						sent_bytes = msg_len;
					}
					if (not success){
						assert(not m_blocked_packets_params);
						m_blocked_packets_params.emplace(packets, idx, total_bytes_sent, result_handler);
						break;
					}
					else{
						total_bytes_sent += sent_bytes;
						idx++;
					}
					
				}
				if (idx == packets->size()){
					all_sent = true;
				}
				return std::pair(all_sent, total_bytes_sent);
			}
			
			void worker::setup_header(message::protocol_header& uftp_hdr, message::role r){
				uftp_hdr.message_role = r;
				uftp_hdr.sequence_number = boost::endian::native_to_big(m_session_context.msg_seq_num++);
				uftp_hdr.source_id = m_session_context.in_group_id;
				uftp_hdr.session_id = m_session_context.session_id;
				uftp_hdr.group_instance = m_session_context.task_instance;
				uftp_hdr.grtt = message::quantize_grtt(static_cast<double>(m_session_context.grtt.count()) / 1000000);
				// ToDo: support group size
				uftp_hdr.group_size = 0u;
			}
			
			// ToDo: support rate control
			[[nodiscard]]
			std::pair<bool, std::size_t> worker::send_packet(message_blob packet, 
				const boost::asio::ip::udp::endpoint& dest,
				std::function<std::size_t (api::blob_span)> write_body, 
				api::optional<std::size_t> known_length,
				//std::function<void(const boost::system::error_code, std::size_t)> result_handler){
				rw_handler result_handler){
				
				assert(message::basic_validate_packet(api::blob_span{packet->data(), 
					static_cast<api::blob_span::size_type>(packet->size())}));
					
				auto successful = false;
				auto msg_length = 0u;
				if (known_length)
					msg_length = known_length.value();
				else
					msg_length = do_complete_message(packet, write_body);
				if (m_session_context.transfer_speed){
					if (m_queued_packets_total_length < m_bucket_full_size){
						m_queued_packets_total_length += msg_length;
						m_sendout_queue.emplace(std::move(packet), msg_length, dest, std::move(result_handler));
						
						successful = true;
					}
				}
				// send directly when no transfer speed specified(i.e. no rate control)
				else{
					m_socket.async_send_to(boost::asio::buffer(packet->data(), msg_length),
						dest,
						[packet, handler = std::move(result_handler), this]
						(const boost::system::error_code ec, std::size_t bytes_sent){
							//m_last_send_done = true;
							if (handler)
								handler(ec, bytes_sent);
						});
					successful = true;
				}
				return std::pair(successful, msg_length);
			}
			
			std::pair<bool, std::size_t> worker::send_to_targeted_receivers(message_blob packet,
				const boost::asio::ip::udp::endpoint& dest,
				//std::map<std::uint32_t, session_context::receiver_properties>& receivers_states,
				std::function<bool (session_context::receiver_properties& state)> filter,
				rw_handler result_handler){
				auto recv_iter = m_session_context.receivers_properties.begin();
				
				auto write_body = [filter = std::move(filter), this, &recv_iter]
					(api::blob_span buffer) -> auto {
						auto [bytes_written, last_iter] = write_receivers_id(buffer, m_session_context.receivers_properties, recv_iter, filter);
						recv_iter = last_iter;
						return bytes_written;
					};
				
				auto packets_buffer = std::make_shared<std::vector<send_args>>();
				auto msg_copy = make_message_blob(*packet);
				while (recv_iter != m_session_context.receivers_properties.end()){
					auto msg_len = do_complete_message(msg_copy, write_body);
					packets_buffer->emplace_back(msg_copy, msg_len, std::ref(dest), nullptr);
					// only when need to send next we should increment the sequence_number
					if (recv_iter != m_session_context.receivers_properties.end()){
						msg_copy = make_message_blob(*msg_copy);
						auto uftp_hdr = reinterpret_cast<message::protocol_header *>(msg_copy->data());
						uftp_hdr->sequence_number = boost::endian::native_to_big(m_session_context.msg_seq_num++);
					}
				}
				
				return send_multiple_packets(packets_buffer, std::move(result_handler));
			}
			
			void worker::learn_employer(std::weak_ptr<employer> boss){
				if (boss.lock() != m_employer.lock()) {
					cancel_all_jobs();
					m_employer = std::move(boss);
				}
			}
			
			std::weak_ptr<worker::employer> worker::current_employer() const{
				return m_employer;
			}
			
			void worker::loop_read_packet(){
				auto buf = make_message_blob(1500);
				m_socket.async_receive_from(boost::asio::buffer(*buf),
					m_sender_endpoint,
					[this, buf](const boost::system::error_code ec, 
						std::size_t bytes_read){
						if (!ec){
							auto boss = m_employer.lock();
							if (boss) {
								auto packet_span = api::blob_span{ buf->data(), static_cast<api::blob_span::size_type>(bytes_read) };
								if (auto validated_packet = message::basic_validate_packet(packet_span); validated_packet) {
									if (validated_packet.value().msg_header.session_id == m_session_context.session_id) {
										boss->on_message_received(validated_packet.value());
										loop_read_packet();
									}
								}
							}
						}
						else if (ec != boost::asio::error::operation_aborted and
							not m_employer.expired())
							loop_read_packet();
					});
			}
			
			void worker::
				schedule_job_after(std::chrono::microseconds dura, std::function<void()> job){
				auto job_timer = std::make_shared<boost::asio::steady_timer>(m_net_io_ctx);
				job_timer->expires_after(dura);
				job_timer->async_wait([job = std::move(job), job_timer]
					(const boost::system::error_code ec){
					if (!ec){
						job();
					}
				});

				m_job_timers.emplace_back(job_timer);
			}
			
			void worker::cancel_all_jobs(){
				auto ec = boost::system::error_code{};
				
				m_socket.cancel(ec);
				if (ec)
					std::cout << "Failed to cancel socket ops. Reason: " << ec.message() << '\n';
				
				for (auto it = m_job_timers.begin(); it != m_job_timers.end();) {
					auto jt = it->lock();
					if (jt) {
						jt->cancel(ec);
						if (ec)
							std::cout << "Failed to cancel timer ops. Reason: " << ec.message() << '\n';
						it++;
					}
					else {
						auto dit = it;
						it++;
						m_job_timers.erase(dit);
					}
				}
				
				m_rc_timer.cancel(ec);
				if (ec)
					std::cout << "Failed to cancel timer ops. Reason: " << ec.message() << '\n';
			}
			
			void worker::execute_in_file_thread(std::function<void()> job){
				m_file_io_ctx.post(std::move(job));
			}
			
			void worker::execute_in_net_thread(std::function<void()> job){
				m_net_io_ctx.post(std::move(job));
			}
			
			void worker::refine_grtt(std::function<bool(session_context::receiver_properties::status )> filter){
				assert(filter);
				auto last_round_max_rtt = std::chrono::microseconds{0u};
				for (auto& [id, prop] : m_session_context.receivers_properties){
					if (filter(prop.current_status) and
						(prop.rtt and prop.rtt.value() > last_round_max_rtt))
						last_round_max_rtt = prop.rtt.value();
				}
				if (last_round_max_rtt.count() > 0u){
					if (last_round_max_rtt > m_session_context.grtt)
						m_session_context.grtt = last_round_max_rtt;
					else if (last_round_max_rtt < m_session_context.grtt)
						m_session_context.grtt = 
							std::max(std::chrono::duration_cast<std::chrono::microseconds>(m_session_context.grtt * 0.9),
								last_round_max_rtt);
				}
			}
			
			session_context& 
				worker::get_context() {
				return m_session_context;
			}
		}
	}
}