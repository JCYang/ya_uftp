#include "receiver/detail/worker.hpp"

#include "boost/endian/conversion.hpp"

namespace ya_uftp {
	namespace receiver {
		namespace detail {
			worker::employer::~employer() = default;

			worker::worker(boost::asio::io_context& net_io_ctx,
				boost::asio::io_context& file_io_ctx,
				boost::asio::ip::address private_mcast_addr,
				boost::asio::ip::udp::endpoint	sender_ep,
				bool open_group, std::uint32_t session_id, 
				message::member_id sender_id,
				std::uint16_t blk_size, 
				std::uint8_t robust,
				task::parameters& params) :
				m_net_io_ctx(net_io_ctx), m_file_io_ctx(file_io_ctx),
				m_socket(m_net_io_ctx), m_sender_endpoint(sender_ep),
				m_timeout_timer(m_net_io_ctx),
				m_session_context(private_mcast_addr, open_group, session_id, sender_id, blk_size, robust) {

				m_session_context.quit_on_error = params.quit_on_error;
				auto ec = boost::system::error_code{};
				
				if (private_mcast_addr.is_v4()) {
					m_socket.open(boost::asio::ip::udp::v4(), ec);
				}
				else {
					m_socket.open(boost::asio::ip::udp::v6(), ec);
				}

				m_socket.set_option(boost::asio::socket_base::reuse_address(true));
				m_socket.set_option(boost::asio::socket_base::receive_buffer_size(params.udp_buffer_size));

				if (params.public_multicast_addr.is_v4()) {
					m_socket.bind(
						boost::asio::ip::udp::endpoint{
							boost::asio::ip::address_v4::any(),
							params.listen_port,
						}, ec);
				}
				else {
					m_socket.bind(
						boost::asio::ip::udp::endpoint{
							boost::asio::ip::address_v6::any(),
							params.listen_port,
						}, ec);
				}

				auto& active_interfaces = jcy::network::interface::retrieve_all();
				for (auto& intf_info : active_interfaces) {
					if (not intf_info.is_loopback()) {
						for (auto& interface_addr : intf_info.unicast_addresses()) {
							if (m_session_context.private_mcast_addr.is_v4()) {
								auto mcast_addr = m_session_context.private_mcast_addr.to_v4();
								if (interface_addr.is_v4() and not interface_addr.is_loopback()) {
									m_socket.set_option(boost::asio::ip::multicast::join_group(
										mcast_addr,
										interface_addr.to_v4()), ec);
									if (ec) {
										// ToDo: part of proper logging
										//std::cout << "Failed to join multicast group for interface " <<
											//interface_addr.to_v4().to_string() << ", reason : " << ec.message() << '\n';
									}
									else {
										//std::cout << "Successfully join multicast group for interface " <<
											//interface_addr.to_v4().to_string() << '\n';
									}
								}
							}
							else {
								auto mcast_addr = m_session_context.private_mcast_addr.to_v6();
								if (interface_addr.is_v6() and not interface_addr.is_loopback()) {
									m_socket.set_option(boost::asio::ip::multicast::join_group(
										mcast_addr,
										intf_info.index()), ec);
									if (ec) {
										//std::cout << "Failed to join multicast group for interface " <<
											//interface_addr.to_v6().to_string() << ", reason : " << ec.message() << '\n';
									}
									else {
										//std::cout << "Successfully join multicast group for interface " <<
											//interface_addr.to_v6().to_string() << '\n';
									}
								}
							}
						}
					}
				}
				if (not params.client_id) {
					auto client_id_set = false;
					if (not params.interfaces_ids.empty()) {
						auto usr_specified_intf = decltype(jcy::network::interface::by_index(1)){};
						if (api::holds_alternative<int>(params.interfaces_ids[0]))
							usr_specified_intf = jcy::network::interface::by_index(api::get<int>(params.interfaces_ids[0]));
						else if (api::holds_alternative<std::string>(params.interfaces_ids[0]))
							usr_specified_intf = jcy::network::interface::by_name(api::get<std::string>(params.interfaces_ids[0]));
						else if (api::holds_alternative<boost::asio::ip::address>(params.interfaces_ids[0])) {

						}

						if (usr_specified_intf) {
							for (auto uni_addr : usr_specified_intf.value().get().unicast_addresses()) {
								client_id_set = try_init_in_group_id_from_addr(uni_addr, params);
								if (client_id_set)
									break;
							}
						}
						else {
							// ToDo: report error interface specified???
						}
					}
					else {
						auto& intf_info = jcy::network::interface::retrieve_all();
						for (auto& intf : intf_info) {
							if (not intf.is_loopback() and intf.multicast_enabled()) {
								for (auto& uni_addr : intf.unicast_addresses()) {
									client_id_set = try_init_in_group_id_from_addr(uni_addr, params);
									if (client_id_set)
										break;
								}
								if (client_id_set)
									break;
							}
						}
					}
					// ToDo: we need to error out when the server_id is unknown
					if (not client_id_set)
						throw std::runtime_error("client_id unknown!!!");
				}
				else
					m_session_context.in_group_id = boost::endian::native_to_big(params.client_id.value());

				for (auto& dir : params.destination_dirs) {
					auto norm_dir = dir.lexically_normal();
					if (norm_dir.is_relative())
						m_session_context.destination_dirs.emplace_back(api::fs::current_path() / norm_dir);
					else
						m_session_context.destination_dirs.emplace_back(norm_dir);
				}

				if (params.temp_dirs) {
					m_session_context.temp_dirs.emplace();
					for (auto& dir : params.temp_dirs.value()) {
						auto norm_dir = dir.lexically_normal();
						if (norm_dir.is_relative())
							m_session_context.temp_dirs->emplace_back(api::fs::current_path() / norm_dir);
						else
							m_session_context.temp_dirs->emplace_back(norm_dir);
					}
				}
			}

			worker::~worker() = default;

			bool worker::try_init_in_group_id_from_addr(const boost::asio::ip::address& uni_addr, const task::parameters& params) {
				
				auto success = false;
				if (uni_addr.is_v4() == params.public_multicast_addr.is_v4()) {
					success = true;
					if (uni_addr.is_v4()) {
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

			std::size_t worker::do_complete_message(message_blob msg, std::function<std::size_t(api::blob_span)> write_body) {
				auto msg_length = *(msg->data() + sizeof(message::protocol_header) + 1) * message::header_length_unit + sizeof(message::protocol_header);
				if (write_body)
					msg_length += write_body(api::blob_span{ msg->data() + msg_length,
						static_cast<api::blob_span::size_type>(msg->size() - msg_length) });
				return msg_length;
			}

			session_context& worker::get_context() {
				return m_session_context;
			}

			void worker::setup_header(message::protocol_header& uftp_hdr, message::role r) {
				uftp_hdr.message_role = r;
				uftp_hdr.sequence_number = boost::endian::native_to_big(m_session_context.msg_seq_num++);
				uftp_hdr.source_id = m_session_context.in_group_id;
				uftp_hdr.session_id = m_session_context.session_id;
				uftp_hdr.group_instance = m_session_context.task_instance;
				uftp_hdr.grtt = message::quantize_grtt(static_cast<double>(m_session_context.grtt.count()) / 1000000);
				// ToDo: support group size
				uftp_hdr.group_size = 0u;
			}

			std::pair<bool, std::size_t>
				worker::send_packet(message_blob packet,
					std::function<std::size_t(api::blob_span)> write_body,
					api::optional<std::size_t> known_length,
					rw_handler result_handler) {

				assert(message::basic_validate_packet(api::blob_span{ packet->data(),
					static_cast<api::blob_span::size_type>(packet->size()) }));

				auto successful = false;
				auto msg_length = 0u;
				if (known_length)
					msg_length = known_length.value();
				else
					msg_length = do_complete_message(packet, write_body);
				
				m_socket.async_send_to(boost::asio::buffer(packet->data(), msg_length),
					m_sender_endpoint,
					[packet, handler = std::move(result_handler), this]
				(const boost::system::error_code ec, std::size_t bytes_sent){
					if (handler)
						handler(ec, bytes_sent);
				});
				successful = true;
				
				return std::pair(successful, msg_length);
			}

			void worker::learn_employer(std::weak_ptr<employer> boss) {
				if (boss.lock() != m_employer.lock()) {
					cancel_all_jobs();
					m_employer = std::move(boss);
				}
			}

			std::weak_ptr<worker::employer> worker::current_employer() const {
				return m_employer;
			}

			void worker::loop_read_packet(bool remember_employer,
				api::optional<std::uint8_t> timeout_factor) {
				auto the_boss = std::shared_ptr<employer>{};
				if (remember_employer)
					the_boss = m_employer.lock();
				auto buf = make_message_blob(1500);
				m_socket.async_receive_from(boost::asio::buffer(*buf),
					m_source_ep,
					[this, the_boss, remember_employer, buf, timeout_factor](const boost::system::error_code ec,
						std::size_t bytes_read){

					auto tof = timeout_factor;
					if (not ec) {
						auto boss = m_employer.lock();
						if (boss) {
							auto packet_span = api::blob_span{ buf->data(), static_cast<api::blob_span::size_type>(bytes_read) };
							if (auto validated_packet = message::basic_validate_packet(packet_span); validated_packet) {

								if (validated_packet->msg_header.session_id == m_session_context.session_id and
									validated_packet->msg_header.source_id == m_session_context.sender_id) {
									
									m_last_msg_recv_time = std::chrono::steady_clock::now();
									m_session_context.grtt = std::chrono::microseconds{ static_cast<std::uintmax_t>(message::dequantize_grtt(validated_packet->msg_header.grtt) * 1000000) };
									tof = boss->on_message_received(validated_packet.value());
								}
							}
							//else
								//std::cout << "Received malformed packets\n";
							loop_read_packet(remember_employer, tof);
						}
						//else
							//std::cout << "No boss need to read the packets\n";
					}
					else if (ec != boost::asio::error::operation_aborted and
						not m_employer.expired()) {
						std::cout << "received packet but not quite right\n";
						loop_read_packet(remember_employer, tof);
					}
					//else
						//std::cout << "received packet with error " << ec.message() << '\n';
				});

				if (timeout_factor and
					timeout_factor.value() > 0) {
					auto to = timeout_factor.value() * m_session_context.grtt;
					const auto min_timeout = std::chrono::seconds{ 10 };
                    to = to > min_timeout ? to : min_timeout;
					//std::cout << "Setting session timeout as " << to.count() << " microseconds\n";
					m_timeout_timer.expires_after(to);
					m_timeout_timer.async_wait([this, to]
						(const boost::system::error_code ec) {
						if (not ec) {
							if (m_last_msg_recv_time < std::chrono::steady_clock::now() - to) {
								//std::cout << "Session timeout\n";
								m_socket.cancel();
							}
						}
					});
				}
			}

			void worker::
				schedule_job_after(std::chrono::microseconds dura, std::function<void()> job) {
				auto job_timer = std::make_shared<boost::asio::steady_timer>(m_net_io_ctx);
				job_timer->expires_after(dura);
				job_timer->async_wait([job = std::move(job), job_timer]
				(const boost::system::error_code ec){
					if (!ec) {
						job();
					}
				});
				m_job_timers.emplace_back(job_timer);
			}

			void worker::cancel_all_jobs() {
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

				m_timeout_timer.cancel(ec);
			}

			void worker::execute_in_file_thread(std::function<void()> job) {
				m_file_io_ctx.post(std::move(job));
			}

			void worker::execute_in_net_thread(std::function<void()> job) {
				m_net_io_ctx.post(std::move(job));
			}
		}
	}
}