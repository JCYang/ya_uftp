#include "sender/detail/files_delivery_session.hpp"
#include "sender/detail/file_send_task.hpp"
#include "sender/detail/worker.hpp"
#include "detail/progress_notification.hpp"

#include <type_traits>
#include <iostream>
#include "boost/endian/conversion.hpp"

namespace ya_uftp{
	namespace sender{
		namespace detail{
			files_delivery_session::file_send_task::
				file_send_task(const api::fs::path& local_path, 
					const api::fs::path& remote_path, 
					message::file_id_type	file_id,
					std::shared_ptr<files_delivery_session> parent,
					worker& w, private_ctor_tag tag)
					: m_worker(w), m_context(m_worker.get_context()),
					m_local_path(std::move(local_path)), m_remote_path(std::move(remote_path)),
					m_file_id(file_id),
					m_parent_session(std::move(parent)){
						
					}
			
			std::shared_ptr<files_delivery_session::file_send_task> 
				files_delivery_session::file_send_task::
					create(const api::fs::path& local_path, 
						const api::fs::path& remote_path, 
						message::file_id_type	file_id,
						std::shared_ptr<files_delivery_session> parent,
						worker& w){
				return std::make_shared<file_send_task>(local_path, remote_path, file_id, 
					std::move(parent), w, private_ctor_tag{});
			}
			
			void files_delivery_session::file_send_task::run(){
				m_worker.learn_employer(shared_from_this());
				if (m_context.transfer_speed)
					m_worker.loop_do_rc_send();
				do_send_fileinfo();
				m_worker.loop_read_packet();
			}
			
			std::uint32_t files_delivery_session::file_send_task::id() const
            {
                return boost::endian::big_to_native(m_context.session_id);
            }

            void files_delivery_session::file_send_task::do_send_fileinfo()
            {
				auto body_length = m_context.receivers_properties.size() * sizeof(message::member_id);
				if (body_length > m_context.block_size)
					body_length = m_context.block_size;
				auto name = to_u8string(m_remote_path);
				auto name_len = name.length();
				if (name_len % message::header_length_unit != 0)
					name_len = ((name_len / message::header_length_unit) + 1) * message::header_length_unit;
					
				auto ec = api::error_code{};
				auto link_len = 0u;
				auto link_name = std::string{};
				// make the m_local_path point to wherever it should point
				if (api::fs::is_symlink(m_local_path, ec) && !ec){
					if (m_context.follow_symbolic_link){
						auto target = api::fs::read_symlink(m_local_path, ec);
						if (!ec)
							m_local_path = std::move(target);
					}
					else{
						link_name = to_u8string(m_local_path);
						link_len = link_name.length();
						if (link_len % message::header_length_unit != 0)
							link_len = ((link_len / message::header_length_unit) + 1) * message::header_length_unit;
					}
				}
					
				//auto header_length = sizeof(message::file_info) + name_len + link_len + sizeof(message::extension::file_hash);
				auto header_length = sizeof(message::file_info) + name_len + link_len;
				auto msg_length = sizeof(message::protocol_header) + header_length + body_length;
				auto msg = make_message_blob(msg_length, 0u);
				auto uftp_hdr = new (msg->data()) message::protocol_header;
				m_worker.setup_header(*uftp_hdr, message::role::file_info);
				auto finfo = new (msg->data() + sizeof(message::protocol_header)) message::file_info;
				finfo->header_length = header_length / message::header_length_unit;
				finfo->id = m_file_id;
				if (link_len > 0){
					finfo->type = message::file_info::subtype::symbolic_link;
					finfo->size_high_word = 0u;
					finfo->size_low_dword = 0u;
				}
				else if (api::fs::is_directory(m_local_path, ec) && !ec){
					finfo->type = message::file_info::subtype::directory;
					finfo->size_high_word = 0u;
					finfo->size_low_dword = 0u;
				}
				else{
					finfo->type = message::file_info::subtype::regular_file;
					auto file_size = api::fs::file_size(m_local_path, ec);
					if (!ec){
						on_file_size_learned(file_size, m_context.block_size, m_context.max_block_count_per_section);
							
						finfo->size_high_word = m_file_size & 0xffff00000000;
						finfo->size_low_dword = m_file_size & 0xffffffff;
					}
				}
					
				auto ft = api::fs::last_write_time(m_local_path, ec);
				if (!ec){
					finfo->set_file_timestamp(api::convert_file_time(ft));
				}
						
				finfo->name_length = name_len / message::header_length_unit;
				finfo->link_length = link_len / message::header_length_unit;
				message::set_timestamp(finfo->msg_timestamp_usecs_high, finfo->msg_timestamp_usecs_low);
				auto name_buf = reinterpret_cast<char*>(msg->data() + sizeof(message::protocol_header) + sizeof(message::file_info));
				std::copy(name.begin(), name.end(), name_buf);
				if (link_len > 0){
					auto link_buf = reinterpret_cast<char*>(msg->data() + sizeof(message::protocol_header) + sizeof(message::file_info) + link_len);
					std::copy(link_name.begin(), link_name.end(), link_buf);
				}
					
				finfo->make_transfer_ready();
				
				auto all_living = [](session_context::receiver_properties& s) {
					auto should_include = s.current_status == session_context::receiver_properties::status::registered and not s.is_proxy;
					return should_include;
				};
				
				auto next_step = [this, msg](){
					auto all_members_responsed = true;
					auto no_members_responsed = true;
				
					auto all_members_done = true;
					for (auto& [id, s]  : m_context.receivers_properties){
						// clients maybe ready to accept data or
						// has the complete file already, either way means they are ready 
						if (s.current_status != session_context::receiver_properties::status::active and
							s.current_status != session_context::receiver_properties::status::done){
							all_members_responsed = false;
						}
						if (s.current_status != session_context::receiver_properties::status::done)
							all_members_done = false;
						if (s.current_status != session_context::receiver_properties::status::registered)
							no_members_responsed = false;
					}
					
					if (m_rounds++ < m_context.robust_factor and not all_members_responsed){
						m_worker.refine_grtt([](auto s){return s == session_context::receiver_properties::status::active;});
						auto do_resend = [this_task = shared_from_this()](){
							this_task->do_send_fileinfo();
						};
						m_worker.schedule_job_after(m_context.grtt * 3, std::move(do_resend));
					}
					// nothing to further transfer for symbolic link and directory 
					else if (not no_members_responsed and not all_members_done and 
						api::fs::is_regular_file(m_local_path) and m_file_size > 0){
						m_worker.refine_grtt([](auto s) {return s == session_context::receiver_properties::status::active; });
						// we no longer announce the file_info, so reset the round count
						m_rounds = 0u;
						m_phase = phase::sending;
						// we should also mark all non-responded clients as lost now
						for (auto [id, prop] : m_context.receivers_properties){
							if (prop.current_status == session_context::receiver_properties::status::registered)
								prop.current_status = session_context::receiver_properties::status::lost;
						}
						auto transfer_content = [this_task = shared_from_this()](){ 
						this_task->m_worker.execute_in_file_thread(
						[this_task](){this_task->do_transfer(); });};
						m_worker.schedule_job_after(m_context.grtt * 3, std::move(transfer_content));
					}
					else {
						for (auto [id, prop] : m_context.receivers_properties){
							if (prop.current_status == session_context::receiver_properties::status::registered)
								prop.current_status = session_context::receiver_properties::status::lost;
						}
						m_worker.cancel_all_jobs();
						m_parent_session->on_file_send_complete(files_delivery_session::visa{});
					}
				};

				core::detail::progress_notification::get().post_progress({id(), task::status::announcing, m_local_path});
				auto [all_sent, bytes_sent] = m_worker.send_to_targeted_receivers(msg, m_context.private_mcast_dest, all_living);
				if (all_sent){
					next_step();
				}
				else{
					assert(not m_blocked_task);
					m_blocked_task = std::move(next_step);
				}
			}
			
			void files_delivery_session::file_send_task::do_transfer(){
				auto rewind = true;
				while(rewind){
					rewind = false;
					std::unique_lock state_lock(m_state_mutex);
					auto blocked = false;
					if (m_phase == phase::sending){
                        core::detail::progress_notification::get().post_progress({id(), task::status::transferring, m_local_path});
						while (not m_reach_eof){
							state_lock.unlock();
							auto blk_idx = m_current_block_idx++;
							// only when we are sending section by section, the eof() can indicate
							// we've completely send the whole file(modul).
							// we can reach eof() during resend lost blocks too, don't do it there
							if (m_current_block_idx >= m_block_count){
								m_reach_eof = true;
								//std::cout << "Last block sent is " << blk_idx << '\n';
							}
							if (do_send_one_block(blk_idx)){
								state_lock.lock();
								if (m_phase == phase::sending)
									continue;
								else
									break;
							}
							else{
								state_lock.lock();
								blocked = true;
								break;
							}
						}
						
						if (m_reach_eof){
							if (not blocked){
								m_phase = phase::waiting_client_status;
								state_lock.unlock();
								m_worker.execute_in_net_thread([this_task = shared_from_this()](){
									this_task->do_send_done();
								});
							}
						}
					}
					else if (m_phase == phase::sending_lost){
                        core::detail::progress_notification::get().post_progress(
                            {id(), task::status::restransferring, m_local_path});
						if (not m_current_retrans_block_iter){
							m_current_retrans_block_iter = m_nak_records.cbegin();
						}
						while (m_current_retrans_block_iter.value() != m_nak_records.cend()){
							auto blk_idx = *m_current_retrans_block_iter.value();
							
							m_current_retrans_block_iter.value()++;
							state_lock.unlock();
							if (do_send_one_block(blk_idx)){
								state_lock.lock();
								if (m_phase == phase::sending_lost)
									continue;
								else
									break;
							}
							else{
								state_lock.lock();
								blocked = true;
								break;
							}
						}
						if (m_current_retrans_block_iter.value() == m_nak_records.cend()){
							m_current_retrans_block_iter = api::nullopt;
							
							m_nak_records = std::move(m_not_yet_merged_nak_records);
							m_not_yet_merged_nak_records.clear();
							if (m_nak_records.empty()){
								if (m_reach_eof){
									m_phase = phase::waiting_client_status;
									// reset all clients status to active
									for (auto [rid, s] : m_context.receivers_properties){
										if (not s.is_proxy)
											s.current_status = session_context::receiver_properties::status::active;
									}
									if (not blocked){
										m_worker.execute_in_net_thread([this_task = shared_from_this()](){
											this_task->do_send_done();
										});
									}
								}
								else{
									m_phase = phase::sending;
									rewind = true;
									continue;
								}
							}
							else{
								rewind = true;
								continue;
							}
						}
					}
				}
			}
			
			bool files_delivery_session::file_send_task::do_send_one_block(
				std::uintmax_t block_idx, 
				message_blob old_msg){
				auto msg = std::move(old_msg);
				
				if (not msg){
					const auto msg_length = sizeof(message::protocol_header) + sizeof(message::file_seg) +
						m_context.block_size;
					msg = make_message_blob(msg_length);
					auto uftp_hdr = new (msg->data()) message::protocol_header;
					m_worker.setup_header(*uftp_hdr, message::role::file_seg);
					auto fseg_hdr = new (msg->data() + sizeof(message::protocol_header)) message::file_seg;
					fseg_hdr->header_length = sizeof(message::file_seg) / message::header_length_unit;
					fseg_hdr->file_id = m_file_id;
					auto [sect_idx, blk_idx] = abs_block_idx_to_sect_blk(block_idx);
					fseg_hdr->section_idx = sect_idx;
					fseg_hdr->block_idx = blk_idx;
					
					fseg_hdr->make_transfer_ready();
				}
				else{
					auto uftp_hdr = reinterpret_cast<message::protocol_header *>(msg->data());
					uftp_hdr->sequence_number = boost::endian::native_to_big(m_context.msg_seq_num++);
					auto fseg_hdr = reinterpret_cast<message::file_seg*>(msg->data() + sizeof(message::protocol_header));
					fseg_hdr->file_id = m_file_id;
					auto [sect_idx, blk_idx] = abs_block_idx_to_sect_blk(block_idx);
					assert(block_idx == sect_blk_to_abs_block_idx(sect_idx, blk_idx));
					fseg_hdr->section_idx = sect_idx;
					fseg_hdr->block_idx = blk_idx;
					
					fseg_hdr->make_transfer_ready();
				}
				
				auto write_data = [this, block_idx]
					(api::blob_span buf) -> std::size_t {
					assert(buf.size() == m_context.block_size);
					if (not m_file_stream.is_open())
						m_file_stream.open(m_local_path.string(), std::ios_base::in | std::ios_base::binary);
					auto pos = m_context.block_size * block_idx;
					if (m_file_stream.tellg() != pos){
						if (m_file_stream.eof())
							m_file_stream.clear();
						m_file_stream.seekg(pos); 
					}
					m_file_stream.read(reinterpret_cast<char*>(buf.data()), buf.size());
					return m_file_stream.gcount();
				};
				
				auto next_step = [this_task = shared_from_this(), old_msg = msg]
					(const boost::system::error_code ec, std::size_t bytes_sent){
						if (ec){
							this_task->m_worker.cancel_all_jobs();
							this_task->m_parent_session->on_file_send_error(files_delivery_session::visa{});
						}
					};
				
				auto [sent, msg_len] = m_worker.send_packet(msg, m_context.private_mcast_dest, std::move(write_data), 
					api::nullopt, next_step);
				if (not sent){
					assert(not m_blocked_msg_args and not m_blocked_task);
					m_blocked_msg_args.emplace(msg, msg_len, 
							std::ref(m_context.private_mcast_dest), next_step);
					m_blocked_task = [this_task = shared_from_this()](){
						this_task->m_worker.execute_in_file_thread([this_task]()
						{ this_task->do_transfer();});
					};
				}
				return sent;
			}
			
			bool files_delivery_session::file_send_task::do_send_done(message_blob old_msg){
				auto msg = std::move(old_msg);
				auto sect_idx = 0u;
				if (not msg){
					auto msg_length = sizeof(message::protocol_header) + sizeof(message::done) + m_context.block_size;
					msg = make_message_blob(msg_length, 0u);
					auto uftp_hdr = new (msg->data()) message::protocol_header;
					m_worker.setup_header(*uftp_hdr, message::role::done);
					auto done_hdr = new (msg->data() + sizeof(message::protocol_header)) message::done;
					done_hdr->header_length = sizeof(message::done) / message::header_length_unit;
					done_hdr->file_id = m_file_id;
					done_hdr->section_idx = m_section_count > 0u ? (m_section_count - 1) : 0u;
					sect_idx = done_hdr->section_idx;
					done_hdr->make_transfer_ready();
				}
				else{
					auto uftp_hdr = reinterpret_cast<message::protocol_header*>(msg->data());
					uftp_hdr->sequence_number = boost::endian::native_to_big(m_context.msg_seq_num++);
					auto done_hdr = reinterpret_cast<message::done*>(msg->data() + sizeof(message::protocol_header));
					sect_idx = boost::endian::big_to_native(done_hdr->section_idx);
				}
				
				auto only_active = [](session_context::receiver_properties& s) {
					return s.current_status == session_context::receiver_properties::status::active or 
						s.current_status == session_context::receiver_properties::status::active_nak;
				};
				//std::cout << "Send done for section " << sect_idx << '\n';
				auto next_step = [this, old_msg = msg](){
					m_worker.schedule_job_after(m_context.grtt * 3, 
						[this_task = shared_from_this(), old_msg = std::move(old_msg)](){
							this_task->on_wait_receivers_status_end(std::move(old_msg));
						});
				};
				
				core::detail::progress_notification::get().post_progress({
					id(), task::status::sending_done_nofitication, m_local_path});
				auto [all_sent, bytes_sent] = m_worker.send_to_targeted_receivers(msg, m_context.private_mcast_dest, 
					std::move(only_active));
				if (all_sent)
					next_step();
				else{
					assert(not m_blocked_task);
					m_blocked_task = std::move(next_step);
				}
				return all_sent;
			}
			
			void files_delivery_session::file_send_task::
				on_wait_receivers_status_end(message_blob old_done_msg){
				auto blocks_lost = false;
				auto all_members_responsed = true;
				if (m_rounds++ < m_context.robust_factor){
					for (auto [id, state] : m_context.receivers_properties){
						if (state.current_status == session_context::receiver_properties::status::active){
							std::cout << "One receiver in " << m_context.receivers_properties.size() << "found active, no respond to done yet.\n";
							all_members_responsed = false;
						}
						else if (state.current_status == session_context::receiver_properties::status::active_nak)
							blocks_lost = true;
					}
					if (not all_members_responsed)
						do_send_done(std::move(old_done_msg));
					else if (blocks_lost){
						std::unique_lock state_lock(m_state_mutex);
						m_phase = phase::sending_lost;
						state_lock.unlock();
						m_worker.execute_in_file_thread([this_task = shared_from_this()](){
							this_task->do_transfer();
						});
					}
					else{
						m_worker.cancel_all_jobs();
						m_parent_session->on_file_send_complete(files_delivery_session::visa{});
					}
				}
				else{
					m_rounds = 0u;
					auto any_receivers_error = false;
					for (auto& [id, state] : m_context.receivers_properties){
						if (state.current_status == session_context::receiver_properties::status::active_nak){
							blocks_lost = true;
							//break;
						}
						
						if (state.current_status == session_context::receiver_properties::status::active){
							state.current_status = session_context::receiver_properties::status::lost;
							any_receivers_error = true;
						}
					}
					if (any_receivers_error and m_context.quit_on_error){
						m_worker.cancel_all_jobs();
						m_parent_session->on_file_send_error(files_delivery_session::visa{});
						return;
					}
					if (blocks_lost){
						std::unique_lock state_lock(m_state_mutex);
						m_phase = phase::sending_lost;
						state_lock.unlock();
						m_worker.execute_in_file_thread([this_task = shared_from_this()](){
							this_task->do_transfer();
						});
					}
					else {
						m_worker.cancel_all_jobs();
						m_parent_session->on_file_send_complete(files_delivery_session::visa{});
					}
				}
			}
			
			void files_delivery_session::file_send_task::on_worker_bucket_freed(){
				if (m_blocked_msg_args){
					auto [msg, len, dest, handler] = m_blocked_msg_args.value();
					auto [sent, sent_len] = m_worker.send_packet(msg, dest, nullptr, len, std::move(handler));
					assert(sent and sent_len == len);
					m_blocked_msg_args = api::nullopt;
				}
				if (m_blocked_task){
					auto task_copy = m_blocked_task;
					m_blocked_task = nullptr;
					task_copy();
				}
			}
			
			files_delivery_session::file_send_task::~file_send_task(){
				auto current_boss = m_worker.current_employer().lock();
				if (current_boss.get() == this)
					m_worker.learn_employer(m_parent_session);
			}
			
			void files_delivery_session::file_send_task::on_message_received(message::validated_packet valid_packet){
				switch(valid_packet.msg_header.message_role){
				case message::role::file_info_ack:
					on_file_info_ack_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					break;
				case message::role::status:
					on_status_msg_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					break;
				case message::role::complete:
					on_complete_msg_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					break;
				case message::role::abort:
					on_abort_msg_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					break;
				default:
					break;
				}
			}
			
			void files_delivery_session::file_send_task::
				on_file_info_ack_received(api::blob_span packet, message::member_id source_id){
				std::lock_guard state_lock(m_state_mutex);
				if (m_phase == phase::announcing){
					if (auto finfo_ack = message::file_info_ack::parse_packet(packet); finfo_ack){
						if (finfo_ack->main.id == m_file_id){
							if (auto recv_it = m_context.receivers_properties.find(source_id); 
								recv_it != m_context.receivers_properties.end()){
								
								recv_it->second.rtt = message::calculate_rtt(finfo_ack->main.msg_timestamp_usecs_high,
									finfo_ack->main.msg_timestamp_usecs_low);
								if (recv_it->second.is_proxy){
									if (finfo_ack->main.done){
										std::for_each(finfo_ack->receiver_ids.begin(), finfo_ack->receiver_ids.end(),
											[this](auto rid){
												auto iter = m_context.receivers_properties.find(rid);
												if (iter != m_context.receivers_properties.end())
													iter->second.current_status = session_context::receiver_properties::status::done;
											});
									}
									else {
										std::for_each(finfo_ack->receiver_ids.begin(), finfo_ack->receiver_ids.end(),
											[this](auto rid){
												auto iter = m_context.receivers_properties.find(rid);
												if (iter != m_context.receivers_properties.end())
													iter->second.current_status = session_context::receiver_properties::status::active;
											});
									}
								}
								else{
									if (finfo_ack->main.done)
										recv_it->second.current_status = session_context::receiver_properties::status::done;
									else
										recv_it->second.current_status = session_context::receiver_properties::status::active;
								}
							}
						}
					}
				}
			}
			
			void files_delivery_session::file_send_task::
				on_status_msg_received(api::blob_span packet, message::member_id receiver_id){
				
				if (auto client_status = message::status::parse_packet(packet); client_status){
					if (client_status->main.file_id == m_file_id){
						if (auto rit = m_context.receivers_properties.find(receiver_id); 
							rit != m_context.receivers_properties.end() and rit->second.current_status != session_context::receiver_properties::status::done){
							
							auto nak_blocks = message::extract_lost_blocks_ids(client_status->nak_map);
							if (nak_blocks.empty()){
								std::cout << "Received STATUS without lost from " << std::hex << receiver_id << std::dec << '\n'; 
							}
							else{
								if (rit->second.current_status != session_context::receiver_properties::status::done){
									rit->second.current_status = session_context::receiver_properties::status::active_nak;
									std::cout << "Received STATUS with lost from " << std::hex << receiver_id << std::dec << '\n'; 
									std::lock_guard state_lock(m_state_mutex);
									if (m_phase == phase::waiting_client_status){
										for (auto blk_idx : nak_blocks){
											m_nak_records.emplace(sect_blk_to_abs_block_idx(client_status->main.section_idx, blk_idx));
										}
									}
									else{
										for (auto blk_idx : nak_blocks){
											m_not_yet_merged_nak_records.emplace(sect_blk_to_abs_block_idx(client_status->main.section_idx, blk_idx));
										}
									}
								}
							}
						}
					}
				}
			}
			
			void files_delivery_session::file_send_task::
				on_complete_msg_received(api::blob_span packet, message::member_id receiver_id){
				std::lock_guard state_lock(m_state_mutex);
				if (m_phase == phase::waiting_client_status or
					m_phase == phase::announcing){
					if (auto receiver_complete = message::complete::parse_packet(packet); receiver_complete){
						if (receiver_complete->main.file_id == m_file_id){
							if (auto rit = m_context.receivers_properties.find(receiver_id); 
								rit != m_context.receivers_properties.end()){
								if (rit->second.is_proxy){
									if (not receiver_complete->receiver_ids.empty()){
										for (auto rid : receiver_complete->receiver_ids){
											if (auto cit = m_context.receivers_properties.find(rid); 
												cit != m_context.receivers_properties.end()){
												cit->second.current_status = session_context::receiver_properties::status::done;
												cit->second.confirm_sent = false;
												std::cout << "Received COMPLETE message from " << std::hex << receiver_id << std::dec << '\n';
											}
										}
									}
									// ToDo: when no receiver_ids in a proxy send complete msg, it should be considered an error
								}
								else{
									rit->second.current_status = session_context::receiver_properties::status::done;
									rit->second.confirm_sent = false;
								}
								auto all_done = true;
								for (auto& [id, prop] : m_context.receivers_properties) {
									if (prop.current_status != session_context::receiver_properties::status::done) {
										all_done = false;
										break;
									}
								}
								if (all_done)
									m_parent_session->on_file_send_complete(files_delivery_session::visa{});
							}
						}
					}
					else
						std::cout << "Received wrong COMPLETE message from " << std::hex << receiver_id << std::dec << '\n';
				}
			}
			
			void files_delivery_session::file_send_task::
				on_abort_msg_received(api::blob_span packet, message::member_id receiver_id){
				if (auto abort_signal = message::abort::parse_packet(packet); abort_signal){
					if (auto rit = m_context.receivers_properties.find(receiver_id);
						rit != m_context.receivers_properties.end()){
						auto by_proxy = false;
						if (rit->second.is_proxy){
							if (abort_signal->main.host != 0){
								if (auto cit = m_context.receivers_properties.find(abort_signal->main.host);
									cit != m_context.receivers_properties.end()){
									cit->second.current_status = session_context::receiver_properties::status::abort;
									by_proxy = true;
								}
							}
						}
						if (not by_proxy)
							rit->second.current_status = session_context::receiver_properties::status::abort;
					}
				}
			}
		}
	}
}