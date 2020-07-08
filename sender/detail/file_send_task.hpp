#pragma once
#ifndef YA_UFTP_SENDER_DETAIL_FILE_SEND_TASK_HPP_
#define YA_UFTP_SENDER_DETAIL_FILE_SEND_TASK_HPP_

#include "detail/file_transfer_base.hpp"
#include "sender/detail/session_context.hpp"
#include <fstream>
#include <mutex>

namespace ya_uftp{
	namespace sender{
		namespace detail{
			class files_delivery_session::file_send_task : 
				public std::enable_shared_from_this<file_send_task>, 
				public ya_uftp::detail::file_transfer_base,
				public worker::employer {
				struct private_ctor_tag{};
				enum class phase {
					announcing,
					sending, // initial sending the files block by block from the beginning to eof
					waiting_client_status, // wait for the clients status
					sending_lost,
					complete
				};
				
				worker&											m_worker;
				session_context&								m_context;
				api::fs::path									m_local_path;
				api::fs::path									m_remote_path;
				message::file_id_type							m_file_id;
				std::uintmax_t									m_current_block_idx = 0u;
				std::ifstream									m_file_stream;
				std::shared_ptr<files_delivery_session>			m_parent_session;
				
				bool											m_reach_eof = false;
						
				std::set<std::uintmax_t>						m_nak_records;
				api::optional<std::set<std::uintmax_t>::const_iterator>		m_current_retrans_block_iter;
				// when in sending_lost phase, we use iterator to resend losted block one by one, 
				// yet we should continue remember any naks received, so here it is
				std::set<std::uintmax_t>						m_not_yet_merged_nak_records;
				std::uint32_t									m_rounds = 0u;
				std::mutex										m_state_mutex;
				phase											m_phase = phase::announcing;
				api::optional<worker::send_args>				m_blocked_msg_args;
				std::function<void()>							m_blocked_task;
			public:
				file_send_task(const api::fs::path& local_path, 
					const api::fs::path& remote_path, 
					message::file_id_type	file_id,
					std::shared_ptr<files_delivery_session> parent,
					worker& w, private_ctor_tag tag);
				
				static std::shared_ptr<file_send_task> 
					create(const api::fs::path& local_path, 
						const api::fs::path& remote_path, 
						message::file_id_type	file_id,
						std::shared_ptr<files_delivery_session> parent, worker& w);
						
				void run();
				void on_worker_bucket_freed() override;
				void on_message_received(message::validated_packet valid_packet) override;
				~file_send_task();
			private:
                std::uint32_t id() const;
				void do_send_fileinfo();
				void do_transfer();
				
				bool do_send_one_block(std::uintmax_t block_idx, 
					message_blob old_msg = nullptr);
				bool do_send_done(message_blob old_msg = nullptr);
				
				//void schedule_next_round_resend(message_blob msg);
				
				void on_wait_receivers_status_end(message_blob old_done_msg);
				void on_file_info_ack_received(api::blob_span packet, message::member_id source_id);
				void on_status_msg_received(api::blob_span packet, message::member_id receiver_id);
				void on_complete_msg_received(api::blob_span packet, message::member_id receiver_id);
				void on_abort_msg_received(api::blob_span packet, message::member_id receiver_id);
			};
		}
	}
}

#endif