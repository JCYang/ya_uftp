#include "receiver/detail/files_accept_session.hpp"
#include "receiver/detail/file_receive_task.hpp"
#include "receiver/detail/worker.hpp"

#include "detail/progress_notification.hpp"
#include <type_traits>
#include <iostream>
#include "boost/endian/conversion.hpp"

namespace ya_uftp {
	namespace receiver {
		namespace detail {
			bool target_is_children_of(api::fs::path parent, api::fs::path target) {
				auto [first, last] = std::mismatch(parent.begin(), parent.end(), target.begin());
				return first == parent.end();
			}

			files_accept_session::file_receive_task::file_receive_task(
				std::shared_ptr<files_accept_session> parent, worker& w, private_ctor_tag tag) :
			m_worker(w), m_context(m_worker.get_context()),
			m_parent_session(std::move(parent)),
			m_phase(phase::waiting_file_info){}

			std::shared_ptr<files_accept_session::file_receive_task>
				files_accept_session::file_receive_task::create(std::shared_ptr<files_accept_session> parent, worker& w){
				return std::make_shared<file_receive_task>(std::move(parent), w, private_ctor_tag{});
			}

			files_accept_session::file_receive_task::~file_receive_task(){
				auto current_boss = m_worker.current_employer().lock();
				if (current_boss.get() == this)
					m_worker.learn_employer(m_parent_session);
			}

			void files_accept_session::file_receive_task::run(
				message_blob next_file_info){
				m_worker.learn_employer(shared_from_this());
				m_worker.loop_read_packet(true);
				if (next_file_info) {
					auto msg_span = api::blob_span{next_file_info->data(), next_file_info->size()};
					on_file_info_received(msg_span, m_context.sender_id);
				}
			}

			std::uint32_t files_accept_session::file_receive_task::id() const
            {
                return boost::endian::big_to_native(m_context.session_id);
            }

            std::uint8_t files_accept_session::file_receive_task::on_message_received(
                message::validated_packet valid_packet)
            {
				auto grtt_factor = std::uint8_t(5);
				switch (valid_packet.msg_header.message_role) {
				case message::role::file_info:
					on_file_info_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					grtt_factor = 5;
					break;
				case message::role::file_seg:
					on_data_block_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					grtt_factor = 3;
					break;
				case message::role::done:
					on_done_received(valid_packet.msg_body, valid_packet.msg_header.source_id);
					grtt_factor = 4;
					break;
				default:
					break;
				}
				return grtt_factor;
			}

			void files_accept_session::file_receive_task::on_file_info_received(
				api::blob_span packet, message::member_id source_id){
				auto packet_copy = make_message_blob(packet.begin(), packet.end());
				
				auto file_info_msg = message::file_info::parse_packet(packet);
				if (file_info_msg) {
					auto pos = file_info_msg->receiver_ids.find(m_context.in_group_id);
					if (pos != api::basic_string_view<message::member_id>::npos){
						switch (file_info_msg->main.type) {
							case message::file_info::subtype::regular_file:
								{
								if (m_phase == phase::waiting_file_info) {
									auto file_size = (static_cast<std::uintmax_t>(file_info_msg->main.size_high_word) << 32) +
										file_info_msg->main.size_low_dword;
									on_file_size_learned(file_size, m_context.block_size, m_context.max_block_count_per_section);
									m_file_id = file_info_msg->main.id;
									m_last_fileinfo_ts_high = file_info_msg->main.msg_timestamp_usecs_high;
									m_last_fileinfo_ts_low = file_info_msg->main.msg_timestamp_usecs_low;
                                    m_file_ts = (static_cast<std::uint64_t>(file_info_msg->main.timestamp_high) << 32) +
                                                file_info_msg->main.timestamp_low;
									m_completed_sections.resize(m_section_count);
									auto target_path = api::fs::path{ std::string{file_info_msg->name.data(), file_info_msg->name.size()} }.lexically_normal();

									auto ec = api::error_code{};
									auto acceptable = false;
									
									for (auto& dir : m_context.destination_dirs) {
										const auto dir_space = api::fs::space(dir, ec).available;
										if (not ec) {
											if (target_path.is_absolute()) {
												acceptable = target_is_children_of(dir, target_path) and
													dir_space > m_file_size;
												auto target_parent = target_path.parent_path();
												if (not api::fs::exists(target_parent))
													api::fs::create_directories(target_parent, ec);
												if (ec)
													acceptable = false;
												if (acceptable)
													break;
											}
											else {
												if (dir_space > m_file_size) {
													acceptable = true;
													target_path = dir / target_path;
													auto target_parent = target_path.parent_path();
													if (not api::fs::exists(target_parent))
														api::fs::create_directories(target_parent, ec);
													if (ec)
														acceptable = false;
												}
												if (acceptable)
													break;
											}
										}
										else {
											std::cout << "Failed to get space of " << dir << '\n';
										}
									}

									if (acceptable) {
										if (m_context.temp_dirs) {
											for (auto tmp_dir : m_context.temp_dirs.value()) {
												auto tmp_space = api::fs::space(tmp_dir, ec).available;
												if (not ec and
													tmp_space > m_file_size) {
													m_file_path = tmp_dir / target_path.filename();

													m_final_dest_path = target_path;
													break;
												}
											}

											if (m_file_path.empty())
												m_file_path = target_path;
										}
										else {
											m_file_path = target_path;
										}

										auto already_ondisk = false;
										auto ondisk_filesize = std::uintmax_t(0u);
                                        auto ondisk_file_ts = std::uint64_t(0u);
                                        if (not m_final_dest_path.empty()) 
										{
                                            if (api::fs::is_regular_file(m_final_dest_path, ec))
											{
                                                ondisk_filesize = api::fs::file_size(m_final_dest_path, ec);
                                                ondisk_file_ts = api::convert_file_time(api::fs::last_write_time(m_final_dest_path, ec));
                                            }
                                        }
                                        else if (not m_file_path.empty())
                                        {
                                            if (api::fs::is_regular_file(m_file_path, ec))
                                            {
                                                ondisk_filesize = api::fs::file_size(m_file_path, ec);
                                                ondisk_file_ts = api::convert_file_time(api::fs::last_write_time(m_file_path, ec));
                                            }
                                        }
                                        if (ondisk_filesize == file_size and 
											ondisk_file_ts == m_file_ts)
                                        {
                                            core::detail::progress_notification::get().post_progress({id(), task::status::complete, m_file_path});
                                            m_phase = phase::completed;
                                            do_report_complete();
                                        }
                                        else 
                                        {
                                            // std::cout << "Openning file " << m_file_path.string() << " for
                                            // download\n";
                                            core::detail::progress_notification::get().post_progress(
                                                {id(), task::status::receiving_data, m_file_path});
                                            m_file_stream.open(m_file_path.string(),
                                                               std::ios_base::binary | std::ios_base::out);
                                            m_phase = phase::receiving_blobs;
                                            do_report_file_info_ack();
                                        }
									}
									else {
										m_phase = phase::rejected;
										do_report_complete();
									}
								}
								else if (m_phase == phase::receiving_blobs) {
									if (file_info_msg->main.id != 0 and
										file_info_msg->main.id == m_file_id) {
										do_report_file_info_ack();
									}
								}
								else if (m_phase == phase::completed) {
									if (file_info_msg->main.id != 0 and
										file_info_msg->main.id != m_file_id) {
										
										m_parent_session->on_file_receive_complete(visa{},
											packet_copy);
									}
								}
								break;
							case message::file_info::subtype::directory:
								{
								if (m_phase == phase::waiting_file_info) {
									m_file_id = file_info_msg->main.id;
									m_last_fileinfo_ts_high = file_info_msg->main.msg_timestamp_usecs_high;
									m_last_fileinfo_ts_low = file_info_msg->main.msg_timestamp_usecs_low;

									auto target_path = api::fs::path{ file_info_msg->name.data() }.lexically_normal();

									auto ec = api::error_code{};
									auto acceptable = false;
									for (auto& dir : m_context.destination_dirs) {
										if (target_path.is_absolute()) {
											acceptable = target_is_children_of(dir, target_path);
											if (acceptable)
												break;
										}
										else {
											acceptable = true;
											target_path = dir / target_path;
											break;
										}
									}
									if (acceptable) {
                                        if (api::fs::exists(target_path, ec) and
											api::fs::is_directory(target_path, ec))
                                        {
                                            m_phase = phase::completed;
                                            do_report_complete();
                                        }
                                        else if (not api::fs::exists(target_path, ec))
                                        {
                                            api::fs::create_directories(target_path, ec);
                                            m_phase = phase::completed;
                                            do_report_complete();
                                        }
                                        else
                                        {
                                            m_phase = phase::rejected;
                                            do_report_complete();
                                        }
									}
									else {
										m_phase = phase::rejected;
										do_report_complete();
									}
								}
								else if (m_phase == phase::completed) {
									if (file_info_msg->main.id != 0 and
										file_info_msg->main.id != m_file_id) {
										m_parent_session->on_file_receive_complete(visa{},
											packet_copy);
									}
								}
								}
								break;
							default:
								break;
							}
						}
					}
					else 
						if (file_info_msg->main.id != 0u and
							file_info_msg->main.id == m_file_id) {
							switch (m_phase) {
							case phase::completed:
							case phase::skipped:
								m_parent_session->on_file_receive_complete(visa{});
								break;
							case phase::rejected:
								m_parent_session->on_file_receive_error(false, visa{});
								break;
							default:
								break;
							}
						}
				}
			}

			void files_accept_session::file_receive_task::on_data_block_received(api::blob_span packet, message::member_id source_id){
				if (m_phase == phase::receiving_blobs) {
					auto data_block_msg = message::file_seg::parse_packet(packet);
					if (data_block_msg) {
						if (data_block_msg->main.file_id != 0u and
							data_block_msg->main.file_id == m_file_id) {
							const auto sect_idx = data_block_msg->main.section_idx;
							const auto blk_idx = data_block_msg->main.block_idx;
							const auto sect_blk_count = section_block_count(sect_idx);
							auto block_idx = sect_blk_to_abs_block_idx(sect_idx, blk_idx);

							auto data_copy = make_message_blob(data_block_msg->data_blob.size());
							std::copy(data_block_msg->data_blob.begin(), data_block_msg->data_blob.end(), data_copy->begin());
							m_worker.execute_in_file_thread([data_copy = std::move(data_copy), offset = block_idx * m_context.block_size, this_task = shared_from_this()](){
								this_task->m_file_stream.seekp(offset);
								this_task->m_file_stream.write(reinterpret_cast<const char*>(data_copy->data()),
									data_copy->size());
							});
							
							auto record_it = m_blocks_per_section_completion_record.find(sect_idx);
							if (record_it == m_blocks_per_section_completion_record.end()) {

								auto [it, inserted] = m_blocks_per_section_completion_record.emplace(sect_idx,
									received_record{ boost::dynamic_bitset<std::uint8_t>{sect_blk_count}, 0u });
								assert(inserted);
								
								record_it = it;
								record_it->second.missing_blocks.set();
							}
							record_it->second.missing_blocks[blk_idx] = false;
							record_it->second.count++;
							if (record_it->second.count >= sect_blk_count and
								// avoid completion checks when obviously not all blocks received 
								record_it->second.missing_blocks.none()) {
								m_completed_sections[sect_idx] = true;
							}
						}
					}
				}
			}

			void files_accept_session::file_receive_task::on_done_received(api::blob_span packet, message::member_id source_id){
				
				auto done_msg = message::done::parse_packet(packet);
				if (done_msg) {
					if (done_msg->main.file_id != 0 and
						done_msg->main.file_id == m_file_id) {
						auto id_pos = done_msg->receiver_ids.find(m_context.in_group_id);
						if (id_pos != api::basic_string_view<message::member_id>::npos) {
							if (m_completed_sections.all()) {
                                
                                m_worker.execute_in_file_thread([this_task = shared_from_this()]() {
                                    auto ec = api::error_code{};
                                    this_task->m_file_stream.close();
                                    if (not this_task->m_final_dest_path.empty())
                                    {
                                        api::fs::rename(this_task->m_file_path, this_task->m_final_dest_path, ec);
                                        api::fs::last_write_time(this_task->m_final_dest_path,
                                                                 api::convert_file_time(this_task->m_file_ts), ec);
                                    }
                                    else
                                    {
                                        api::fs::last_write_time(this_task->m_file_path,
                                                                 api::convert_file_time(this_task->m_file_ts), ec);
                                        if (ec)
                                            std::cout << "Failed to set last write time of " << this_task->m_file_path
                                                      << ", reason is " << ec.message() << '\n';
									}
                                });
                                
								m_phase = phase::completed;
								do_report_complete();
							}
							else {
								for (auto sect_idx = 0u; sect_idx <= done_msg->main.section_idx; sect_idx++) {
									if (not m_completed_sections[sect_idx]) {
										do_report_status(sect_idx);
									}
								}
							}
						}
						else {
							switch (m_phase) {
							case phase::completed:
							case phase::skipped:
								m_parent_session->on_file_receive_complete(visa{});
								break;
							case phase::rejected:
								m_parent_session->on_file_receive_error(false, visa{});
								break;
							default:
								break;
							}
						}
					}
					else if (done_msg->main.file_id == 0u) {
						switch (m_phase) {
						case phase::completed:
						case phase::waiting_file_info:
							m_phase = phase::learn_session_completed;
							m_parent_session->on_whole_session_end(visa{});
							break;
						default:
							m_parent_session->on_file_receive_error(true, visa{});
							break;
						}
					}
				}
			}

			void files_accept_session::file_receive_task::do_report_file_info_ack(){
				const auto msg_length = sizeof(message::protocol_header) + sizeof(message::file_info_ack);
				auto msg = make_message_blob(msg_length);
				auto uftp_hdr = new (msg->data()) message::protocol_header;
				m_worker.setup_header(*uftp_hdr, message::role::file_info_ack);
				auto fileinfo_ack_hdr = new (msg->data() + sizeof(message::protocol_header)) message::file_info_ack;

				fileinfo_ack_hdr->header_length = sizeof(message::file_info_ack) / message::header_length_unit;
				fileinfo_ack_hdr->id = m_file_id;
				fileinfo_ack_hdr->partial_received = 0u;
				fileinfo_ack_hdr->msg_timestamp_usecs_high = m_last_fileinfo_ts_high;
				fileinfo_ack_hdr->msg_timestamp_usecs_low = m_last_fileinfo_ts_low;
				fileinfo_ack_hdr->make_transfer_ready();

				auto [success, bytes_sent] = m_worker.send_packet(msg);
			}

			void files_accept_session::file_receive_task::do_report_complete() {
				const auto msg_length = sizeof(message::protocol_header) + sizeof(message::complete);
				auto msg = make_message_blob(msg_length);
				auto uftp_hdr = new (msg->data()) message::protocol_header;
				m_worker.setup_header(*uftp_hdr, message::role::complete);
				auto complete_hdr = new (msg->data() + sizeof(message::protocol_header)) message::complete;

				complete_hdr->header_length = sizeof(message::complete) / message::header_length_unit;
				complete_hdr->file_id = m_file_id;
				switch (m_phase) {
				case phase::rejected:
					complete_hdr->detail_status = message::complete::sub_status::rejected;
					break;
				case phase::skipped:
					complete_hdr->detail_status = message::complete::sub_status::skipped;
					break;
				case phase::completed:
					complete_hdr->detail_status = message::complete::sub_status::normal;
					break;
				default:
					assert(false);
					break;
				}
				complete_hdr->make_transfer_ready();

				auto [success, bytes_sent] = m_worker.send_packet(msg);
			}
			
			void files_accept_session::file_receive_task::do_report_status(message::section_index sect_idx) {
				const auto msg_length = sizeof(message::protocol_header) + sizeof(message::status) + m_context.block_size;
				auto msg = make_message_blob(msg_length);
				auto uftp_hdr = new (msg->data()) message::protocol_header;
				m_worker.setup_header(*uftp_hdr, message::role::status);
				auto status_hdr = new (msg->data() + sizeof(message::protocol_header)) message::status;
				status_hdr->file_id = m_file_id;
				status_hdr->section_idx = sect_idx;
				status_hdr->header_length = sizeof(message::status) / message::header_length_unit;

				status_hdr->make_transfer_ready();
				auto [success, bytes_sent] = m_worker.send_packet(msg, [this, sect_idx](auto buf) {
					to_block_range(m_blocks_per_section_completion_record[sect_idx].missing_blocks, buf.data());
					return m_blocks_per_section_completion_record[sect_idx].missing_blocks.num_blocks();
				});
			}
		}
	}
}
