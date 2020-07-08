#include "sender/detail/files_delivery_session.hpp"
#include "sender/detail/worker.hpp"
#include "sender/detail/file_send_task.hpp"

#include "detail/progress_notification.hpp"

#include "boost/endian/conversion.hpp"
#include <iostream>

namespace ya_uftp{
	namespace sender{
		namespace detail{
			files_delivery_session::visa::visa() = default;
			
			files_delivery_session::files_delivery_session(
				boost::asio::io_context& net_io_ctx, 
				boost::asio::io_context& file_io_ctx,
				const task::parameters& params, 
				private_ctor_tag tag)
				: m_worker(std::make_unique<worker>(net_io_ctx, file_io_ctx, params)), 
				m_context(m_worker->get_context()),
				m_files(std::move(params.files)),
				m_base_dir(std::move(params.base_dir))
            {
					// ToDo: consider accept user specify task_id to support resumable task
					if (params.allowed_clients){
						for(auto& receiver : params.allowed_clients.value())
							m_context.receivers_properties.emplace(receiver.id, session_context::receiver_properties{session_context::receiver_properties::status::mute});
					}
				}
			
			void files_delivery_session::start() {
				m_worker->learn_employer(shared_from_this());
				if (m_context.transfer_speed)
					m_worker->loop_do_rc_send();
				do_announce();
				m_worker->loop_read_packet();
			}
			
			std::shared_ptr<files_delivery_session> 
				files_delivery_session::create(boost::asio::io_context& net_io_ctx, 
				boost::asio::io_context& file_io_ctx,
				const task::parameters& params){
				return std::make_shared<files_delivery_session>(net_io_ctx, file_io_ctx, params, private_ctor_tag{});
			}
			
			void files_delivery_session::force_end(){
				m_worker->cancel_all_jobs();
			}
			
			std::uint32_t files_delivery_session::id() const
            {
                return boost::endian::big_to_native(m_context.session_id);
            }

            files_delivery_session::~files_delivery_session()
            {
			}
			
			void files_delivery_session::do_announce(){
				// no protocol_header extension counted yet
				auto body_length = m_context.receivers_properties.size() * sizeof(message::member_id);

				if (body_length > m_context.block_size)
					body_length = m_context.block_size;
				auto target_is_v4 = m_context.public_mcast_dest.address().is_v4();
				const auto msg_length = sizeof(message::protocol_header) + sizeof(message::announce) +
					(target_is_v4 ? 8 : 32) + // public + private mcast ip
					body_length;

				auto msg = make_message_blob(msg_length);

				auto uftp_hdr = new (msg->data()) message::protocol_header;
				m_worker->setup_header(*uftp_hdr, message::role::announce);

				auto announce_hdr = new (msg->data() + sizeof(message::protocol_header)) message::announce;
				// ToDo: support specified clients
				announce_hdr->header_length = (msg_length - sizeof(message::protocol_header) - body_length) / message::header_length_unit;
				announce_hdr->sync_mode = 1u;
				announce_hdr->ipv6 = not target_is_v4;

				announce_hdr->robust_factor = m_context.robust_factor;
				// ToDo: support congestion control
				announce_hdr->cc_type = message::congestion_control_mode::none;

				announce_hdr->block_size = m_context.block_size;
				message::set_timestamp(announce_hdr->msg_timestamp_usecs_high, announce_hdr->msg_timestamp_usecs_low);
				if (target_is_v4) {
					auto pub_mcast_addr_field = msg->data() + sizeof(message::protocol_header) + sizeof(message::announce);
					auto pub_mcast_v4_addr = m_context.public_mcast_dest.address().to_v4().to_bytes();
					std::copy(pub_mcast_v4_addr.begin(), pub_mcast_v4_addr.end(), pub_mcast_addr_field);

					auto priv_mcast_addr_field = pub_mcast_addr_field + 4;
					auto priv_mcast_v4_addr = m_context.private_mcast_dest.address().to_v4().to_bytes();
					std::copy(priv_mcast_v4_addr.begin(), priv_mcast_v4_addr.end(), priv_mcast_addr_field);
				}
				else {
					auto pub_mcast_addr_field = msg->data() + sizeof(message::protocol_header) + sizeof(message::announce);
					auto pub_mcast_v6_addr = m_context.public_mcast_dest.address().to_v6().to_bytes();
					std::copy(pub_mcast_v6_addr.begin(), pub_mcast_v6_addr.end(), pub_mcast_addr_field);

					auto priv_mcast_addr_field = pub_mcast_addr_field + 16;
					auto priv_mcast_v6_addr = m_context.private_mcast_dest.address().to_v6().to_bytes();
					std::copy(priv_mcast_v6_addr.begin(), priv_mcast_v6_addr.end(), priv_mcast_addr_field);
				}
				// ToDo: add support for closed group clients

				announce_hdr->make_transfer_ready();
				m_phase = phase::announcing;
				

				//std::cout << "Do announcing the " << m_rounds << "th times\n";
                core::detail::progress_notification::get().post_progress(
                    {id(), task::status::announcing, {}});
				auto after_sent = [this_session = shared_from_this()]
					(const boost::system::error_code ec, std::size_t bytes_sent){
					if (this_session->m_rounds++ < this_session->m_context.robust_factor){
						auto do_resend = [this_session](){
							this_session->m_worker->refine_grtt([](session_context::receiver_properties::status s){
								return s == session_context::receiver_properties::status::registered;
							});
							
							if (this_session->do_send_registered_confirm()){
								if (this_session->m_phase == phase::announcing)
									this_session->do_announce();
							}
						};
						//std::cout << "Schedule announce in " << (this_session->m_context.grtt * 3).count() << " ms\n";
						this_session->m_worker->schedule_job_after(this_session->m_context.grtt * 3, std::move(do_resend));
					}
					else{
						auto start_transfer = [this_session](){
							this_session->enter_transfer_phase();
						};
						this_session->m_worker->schedule_job_after(this_session->m_context.grtt * 3, std::move(start_transfer));
					}
				};
				if (not m_context.is_open_group){
					auto only_mute = [](session_context::receiver_properties& s)
						{
							auto need_include = (s.current_status == session_context::receiver_properties::status::mute);
							return need_include;
						};
					auto [all_sent, bytes_sent] = m_worker->send_to_targeted_receivers(msg, 
						m_context.public_mcast_dest, only_mute);
					if (not all_sent){
						assert(not m_blocked_task);
						m_blocked_task = [this](){
							do_announce();
						};
					}
				}
				else{
					auto [sent, msg_len] = m_worker->send_packet(msg, m_context.public_mcast_dest, nullptr, api::nullopt, after_sent);
					if (not sent){
						assert(not m_blocked_msg_args);
						m_blocked_msg_args.emplace(msg, msg_len, 
							std::ref(m_context.public_mcast_dest), after_sent);
					}
				}
			}
			
			bool files_delivery_session::do_send_registered_confirm(){
				auto need_no_send = true, all_sent = true;
				if (m_last_round_response_count > 0){
					need_no_send = false;
					// FixMe: set it to MTU
					auto msg = make_message_blob(m_context.block_size + 200);
					auto uftp_hdr = new (msg->data()) message::protocol_header;
					m_worker->setup_header(*uftp_hdr, message::role::reg_conf);
					auto reg_conf_hdr = new (msg->data() + sizeof(message::protocol_header)) message::reg_conf;
					reg_conf_hdr->header_length = sizeof(message::reg_conf) / message::header_length_unit;
					
					auto only_registered = [](session_context::receiver_properties& s){
									auto need_include = (s.current_status == session_context::receiver_properties::status::registered
										and not s.confirm_sent and not s.is_proxy);
									if (need_include)
										s.confirm_sent = true;
									return need_include;
								};
					
					core::detail::progress_notification::get().post_progress({id(), task::status::confirming_registration, {}});
					auto [success, bytes_sent] = m_worker->send_to_targeted_receivers(
						msg, m_context.private_mcast_dest, only_registered);
					all_sent = success;
					if (not all_sent){
						assert(not m_blocked_task);
						m_blocked_task = [this](){
							m_last_round_response_count = 0u;
							if (m_phase == phase::announcing)
								do_send_registered_confirm();
						};
					}
					else
						m_last_round_response_count = 0u;
				}
				return need_no_send or all_sent;
			}
			
			void files_delivery_session::on_worker_bucket_freed() {
				if (m_blocked_msg_args){
					auto [msg, len, dest, handler] = m_blocked_msg_args.value();
					auto [sent, sent_len] = m_worker->send_packet(msg, dest, nullptr, len, std::move(handler));
					assert(sent and sent_len == len);
					m_blocked_msg_args = api::nullopt;
				}
				if (m_blocked_task){
					auto task_copy = m_blocked_task;
					m_blocked_task = nullptr;
					task_copy();
				}
			}
			
			void files_delivery_session::on_message_received(message::validated_packet valid_packet){
				switch (valid_packet.msg_header.message_role){
				case message::role::receiver_register:
					{
						on_register_msg_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
						break;
					}
				default:
					break;
				}
			}
			
			void files_delivery_session::
				on_register_msg_received(api::blob_span packet, message::member_id source_id){
				auto reg_msg = message::receiver_register::parse_packet(packet);
				if (reg_msg){
					if (m_phase == phase::announcing){
						if (reg_msg->receiver_ids.empty()){
							auto [iter, inserted] = m_context.receivers_properties.emplace(source_id, 
										session_context::receiver_properties{session_context::receiver_properties::status::registered});
							if (not inserted){
								// when a proxy register again without clients id in the body, it should be consider error and ignored
								if (iter->second.is_proxy)
									return;
								iter->second.current_status = session_context::receiver_properties::status::registered;
								// when a register request is received again, it indicate
								// that the receiver hasn't received the confirmation yet.
								// we need to resent confirmation in the next round.
								iter->second.confirm_sent = false;
							}
							m_last_round_response_count++;
							iter->second.rtt = message::calculate_rtt(
								reg_msg->main.msg_timestamp_usecs_high, 
								reg_msg->main.msg_timestamp_usecs_low);
						}
						else{
							auto [iter, inserted] = m_context.receivers_properties.emplace(source_id, 
										session_context::receiver_properties{session_context::receiver_properties::status::registered});
							if (not inserted){
								// when a proxy registered with an already known client id, it should be consider error and ignored
								if (not iter->second.is_proxy)
									return;
								iter->second.current_status = session_context::receiver_properties::status::registered;
								// when a register request is received again, it indicate
								// that the receiver hasn't received the confirmation yet.
								// we need to resent confirmation in the next round.
								iter->second.confirm_sent = false;
							}
							else
								iter->second.is_proxy = true;
							std::for_each(reg_msg->receiver_ids.begin(), reg_msg->receiver_ids.end(),
								[this, &reg_msg](auto receiver){
									// workaround a MSVC 2017.9 bug
									auto [it, emplaced] = m_context.receivers_properties.emplace(receiver, 
										session_context::receiver_properties{session_context::receiver_properties::status::registered});
									if (not emplaced){
										it->second.current_status = session_context::receiver_properties::status::registered;
										// when a register request is received again, it indicate
										// that the receiver hasn't received the confirmation yet.
										// we need to resent confirmation in the next round.
										it->second.confirm_sent = false;
									}
									m_last_round_response_count++;
									it->second.rtt = message::calculate_rtt(
										reg_msg->main.msg_timestamp_usecs_high, 
										reg_msg->main.msg_timestamp_usecs_low);
								});
						}
					}
				}
			}
			
			void files_delivery_session::enter_transfer_phase(){
				auto no_one_registered = true;
				for (auto [rid, state] : m_context.receivers_properties){
					if (state.current_status == session_context::receiver_properties::status::registered and
						state.confirm_sent){
						no_one_registered = false;
						break;
					}
				}
				if (no_one_registered){
					// ToDo: do log here
					m_worker->cancel_all_jobs();
				}
				else{
					m_rounds = 0u;
					m_phase = phase::running_transfer_task;
					do_send_next_file();
				}
			}
			
			void files_delivery_session::do_send_next_file(){
				if (api::holds_alternative<api::fs::path>(m_files)){
					if (m_is_first_file){
						auto fpath = api::get<api::fs::path>(m_files);
						if (api::fs::exists(fpath)){
							if (api::fs::is_regular_file(fpath)){
								auto remote_name = compute_file_remote_name(fpath, m_base_dir);
								
								auto new_task = file_send_task::create(fpath, remote_name,
									m_current_file_id++, shared_from_this(), *m_worker);
								new_task->run();
							}
							else if(api::fs::is_directory(fpath)){
								auto remote_name = compute_file_remote_name(fpath, m_base_dir);
								m_base_dir = fpath;
								auto new_task = file_send_task::create(fpath, remote_name,
									m_current_file_id++, shared_from_this(), *m_worker);
								new_task->run();
								m_next_entity = api::fs::recursive_directory_iterator{fpath};
							}								
						}
						m_is_first_file = false;
					}
					else if (m_next_entity != api::fs::end(m_next_entity)){
						auto fpath = m_next_entity->path();
						auto remote_name = compute_file_remote_name(fpath, m_base_dir);

						// reset the last file done clients to registered status
						for (auto& [id, prop] : m_context.receivers_properties){
							if (prop.current_status == session_context::receiver_properties::status::done){
								prop.current_status = session_context::receiver_properties::status::registered;
							}
						}
						
						auto new_task = file_send_task::create(fpath, remote_name,
							m_current_file_id++, shared_from_this(), *m_worker);
						new_task->run();
						m_next_entity++;
					}
					else{
						m_phase = phase::complete;
						do_notify_session_completed();
					}
				}
			}
			
			bool files_delivery_session::do_notify_session_completed(){
				auto msg = make_message_blob(m_context.block_size + 200);
				auto uftp_hdr = new (msg->data()) message::protocol_header;
				m_worker->setup_header(*uftp_hdr, message::role::done);
				auto done_hdr = new (msg->data() + sizeof(message::protocol_header)) message::done;
				
				auto only_done = [](session_context::receiver_properties& s){
					auto need_include = (s.current_status == session_context::receiver_properties::status::done and
						not s.confirm_sent and not s.is_proxy);
					if (need_include){
						std::cout << "Include one receiver in done message\n";
						//s.confirm_sent = true;
					}
					return need_include;
				};
				
				done_hdr->header_length = sizeof(message::done) / message::header_length_unit;
				done_hdr->file_id = 0;
				done_hdr->section_idx = 0;
				done_hdr->make_transfer_ready();
				
				auto after_sent = [this_session = shared_from_this(), msg]
					(const boost::system::error_code ec, std::size_t bytes_sent){
					auto all_clients_complete = true;
					for (auto [id, prop] : this_session->m_context.receivers_properties){
						if (prop.current_status != session_context::receiver_properties::status::done){
							all_clients_complete = false;
							break;
						}
					}
					if (this_session->m_rounds++ < this_session->m_context.robust_factor and 
						not all_clients_complete){
						auto do_resend_done = [this_session, old_msg = msg](){
							if (this_session->m_phase == phase::complete)
								this_session->do_notify_session_completed();
							
						};
						this_session->m_worker->schedule_job_after(this_session->m_context.grtt * 3, std::move(do_resend_done));
					}
					else{
						auto start_done_conf = [this_session](){
							this_session->m_rounds = 0u;
							this_session->do_send_done_conf();
						};
						this_session->m_worker->schedule_job_after(this_session->m_context.grtt * 3, std::move(start_done_conf));
					}
				};
				
				auto [success, bytes_sent] = m_worker->send_to_targeted_receivers(
						msg, m_context.private_mcast_dest, only_done,
						after_sent);
				
				return success;
			}
			
			bool files_delivery_session::do_send_done_conf(){
				auto msg = make_message_blob(m_context.block_size + 200);
				auto uftp_hdr = new (msg->data()) message::protocol_header;
				m_worker->setup_header(*uftp_hdr, message::role::done_conf);
				auto done_conf_hdr = new (msg->data() + sizeof(message::protocol_header)) message::done_conf;
				
				auto only_done = [](session_context::receiver_properties& s){
					auto need_include = (s.current_status == session_context::receiver_properties::status::done and
						not s.confirm_sent and not s.is_proxy);
					if (need_include){
						std::cout << "Include one receiver in done_conf message\n";
						s.confirm_sent = true;
					}
					return need_include;
				};
				
				done_conf_hdr->header_length = sizeof(message::done_conf) / message::header_length_unit;
				
				auto [success, bytes_sent] = m_worker->send_to_targeted_receivers(
						msg, m_context.private_mcast_dest, only_done);
				return success;
			}
			
			api::fs::path files_delivery_session::
				compute_file_remote_name(api::fs::path& fpath, 
					const api::optional<api::fs::path>& base_dir){
				auto remote_name = fpath;
				
				if (base_dir and api::fs::exists(base_dir.value()) and
					api::fs::is_directory(base_dir.value())){
					fpath.make_preferred();
					if (auto np = fpath.native(); *np.rbegin() == api::fs::path::preferred_separator)
						fpath = np.substr(0, np.length() - 1);
					auto fp_str = api::fs::absolute(fpath).native(); 
					auto bp_str = api::fs::absolute(base_dir.value()).native();
					if (bp_str.length() < fp_str.length() and
						fp_str.substr(bp_str.length()) == bp_str)
						remote_name = api::fs::relative(fpath, base_dir.value());
				}
				else if (api::fs::is_regular_file(fpath) or api::fs::is_symlink(fpath))
					remote_name = fpath.filename();
				else if (api::fs::is_directory(fpath)){
					fpath.make_preferred();
					if (auto np = fpath.native(); *np.rbegin() == api::fs::path::preferred_separator)
						fpath = np.substr(0, np.length() - 1);
					remote_name = fpath.filename();
				}
				
				return remote_name;
			}
			
			void files_delivery_session::on_file_send_complete(visa key){
				m_worker->execute_in_net_thread(
				[this_session = shared_from_this()](){
					this_session->do_send_next_file();
				});
			}
			
			void files_delivery_session::on_file_send_error(visa key){
				if (not m_context.quit_on_error){
					m_worker->execute_in_net_thread(
					[this_session = shared_from_this()](){
						this_session->do_send_next_file();
					});
				}
			}
		}
	}
}