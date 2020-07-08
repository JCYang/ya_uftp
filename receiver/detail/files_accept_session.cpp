#include "receiver/detail/files_accept_session.hpp"
#include "receiver/detail/file_receive_task.hpp"
//#include "boost/endian/conversion.hpp"

namespace ya_uftp{
	namespace receiver{
		namespace detail{
			files_accept_session::files_accept_session(
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
				private_ctor_tag tag) :
				m_worker(std::make_unique<worker>(net_io_ctx, file_io_ctx, 
					private_mcast_addr, sender_ep, 
					open_group, session_id, sender_id, blk_size, robust, params)),
				m_context(m_worker->get_context()),
				m_last_announce_ts_high(announce_ts_high),
				m_last_announce_ts_low(announce_ts_low){}

			std::shared_ptr<files_accept_session>
				files_accept_session::create(boost::asio::io_context& net_io_ctx,
					boost::asio::io_context& file_io_ctx,
					boost::asio::ip::address private_mcast_addr,
					boost::asio::ip::udp::endpoint	sender_ep,
					bool open_group, std::uint16_t blk_size, std::uint8_t robust,
					const std::uint32_t session_id,
					message::member_id	sender_id,
					const std::uint32_t& announce_ts_high,
					const std::uint32_t& announce_ts_low,
					task::parameters& params) {
				return std::make_shared<files_accept_session>(net_io_ctx, file_io_ctx, private_mcast_addr,
					sender_ep, open_group, blk_size, robust, session_id, sender_id,
					announce_ts_high, announce_ts_low, params, private_ctor_tag{});
			}

			void files_accept_session::start() {
				m_worker->learn_employer(shared_from_this());
				do_register();
				m_worker->loop_read_packet(true);
			}

			void files_accept_session::stop(){
				m_worker->cancel_all_jobs();
			}

			std::uint8_t files_accept_session::on_message_received(message::validated_packet valid_packet) {
				auto grtt_factor = std::uint8_t(3);
				switch (valid_packet.msg_header.message_role) {
				case message::role::reg_conf:
					on_regconf_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					grtt_factor = 5;
					break;
				case message::role::done_conf:
					on_done_conf_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					grtt_factor = 4;
					break;
				default:
					break;
				}
				return grtt_factor;
			}

			void files_accept_session::on_regconf_received(api::blob_span packet, message::member_id source_id) {
				auto reg_conf_msg = message::reg_conf::parse_packet(packet);
				if (reg_conf_msg) {
					for (auto receiver_id : reg_conf_msg->receiver_ids) {
						if (receiver_id == m_context.in_group_id) {
							m_context.register_confirmed = true;
							m_context.retry_count = 0u;
							auto recv_task = file_receive_task::create(shared_from_this(), *m_worker);
							recv_task->run();
							break;
						}
					}
				}
			}

			void files_accept_session::on_done_conf_received(api::blob_span packet, message::member_id source_id){
				auto done_conf_msg = message::done_conf::parse_packet(packet);
				if (done_conf_msg) {
					for (auto receiver_id : done_conf_msg->receiver_ids) {
						if (receiver_id == m_context.in_group_id) {
							m_worker->cancel_all_jobs();
                            std::cout << "Whole accept session complete.\n";
							break;
						}
					}
				}
			}

			void files_accept_session::do_register(){
				auto msg = message_blob{};
				
				if (not m_context.encryption_enabled) {
					const auto msg_length = sizeof(message::protocol_header) + sizeof(message::receiver_register);
					msg = make_message_blob(msg_length);

					auto uftp_hdr = new (msg->data()) message::protocol_header;
					m_worker->setup_header(*uftp_hdr, message::role::receiver_register);

					auto register_hdr = new (msg->data() + sizeof(message::protocol_header)) message::receiver_register;
					register_hdr->header_length = (msg_length - sizeof(message::protocol_header)) / message::header_length_unit;
					register_hdr->ecdh_key_length = 0u;
					register_hdr->msg_timestamp_usecs_high = m_last_announce_ts_high;
					register_hdr->msg_timestamp_usecs_low = m_last_announce_ts_low;
					register_hdr->make_transfer_ready();
				}
				else {
				}

				auto [success, bytes_sent] = m_worker->send_packet(msg, nullptr, api::nullopt, 
					[this_session = shared_from_this()]
					(const boost::system::error_code ec, std::size_t bytes_sent) {
					if (not ec) {
						this_session->m_worker->schedule_job_after(this_session->m_context.grtt * 5, 
							[this_session]() {
							if (not this_session->m_context.register_confirmed and
								this_session->m_context.retry_count < this_session->m_context.robust_factor) {
								this_session->m_context.retry_count++;
								this_session->do_register();
							}
						});
					}
				});
			}

			void files_accept_session::do_report_completed(){
				const auto msg_length = sizeof(message::protocol_header) + sizeof(message::complete);
				auto msg = make_message_blob(msg_length);
				auto uftp_hdr = new (msg->data()) message::protocol_header;
				m_worker->setup_header(*uftp_hdr, message::role::complete);

				auto complete_hdr = new (msg->data() + sizeof(message::protocol_header)) message::complete;
				complete_hdr->header_length = sizeof(message::complete) / message::header_length_unit;
				complete_hdr->file_id = 0;
				complete_hdr->make_transfer_ready();
				auto [success, bytes_sent] = m_worker->send_packet(msg);
			}

			void files_accept_session::on_file_receive_complete(visa v,
				message_blob next_file_info){
				if (m_context.register_confirmed) {
					auto recv_task = file_receive_task::create(shared_from_this(), *m_worker);
					recv_task->run();
				}
			}

			void files_accept_session::on_file_receive_error(bool fatal, visa v){
				if (not fatal and 
					not m_context.quit_on_error and
					m_context.register_confirmed) {
					auto recv_task = file_receive_task::create(shared_from_this(), *m_worker);
					recv_task->run();
				}
			}

			void files_accept_session::on_whole_session_end(visa v){
				m_worker->learn_employer(shared_from_this());
				m_worker->loop_read_packet(true);
				do_report_completed();
			}

			files_accept_session::~files_accept_session() = default;
		}
	}
}
