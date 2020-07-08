#pragma once
#ifndef YA_UFTP_RECEIVER_DETAIL_FILE_RECEIVE_TASK_HPP_
#define YA_UFTP_RECEIVER_DETAIL_FILE_RECEIVE_TASK_HPP_

#include "detail/file_transfer_base.hpp"
#include "receiver/detail/session_context.hpp"
#include <fstream>
#include <mutex>
#include "boost/dynamic_bitset.hpp"

namespace ya_uftp {
	namespace receiver {
		namespace detail {
			class files_accept_session::file_receive_task :
				public std::enable_shared_from_this<file_receive_task>,
				public ya_uftp::detail::file_transfer_base,
				public worker::employer {
				struct private_ctor_tag {};

				enum class phase : std::uint8_t {
					waiting_file_info,
					receiving_blobs,
					completed,
					rejected,
					skipped,
					learn_session_completed
				};

				struct received_record {
					boost::dynamic_bitset<std::uint8_t>	missing_blocks;
					std::uint32_t						count;
				};

				worker& m_worker;
				session_context& m_context;

				message::file_id_type							m_file_id = 0u;
				std::uintmax_t									m_current_block_idx = 0u;
				std::ofstream									m_file_stream;
				
				api::fs::path									m_file_path;
				api::fs::path									m_final_dest_path;
				std::shared_ptr<files_accept_session>			m_parent_session;
				phase											m_phase;
				std::uint32_t									m_last_fileinfo_ts_high;
				std::uint32_t									m_last_fileinfo_ts_low;
                std::uint64_t									m_file_ts = 0u;
				boost::dynamic_bitset<>							m_completed_sections;
				std::map<std::uint16_t, received_record>		m_blocks_per_section_completion_record;
			public:
				file_receive_task(
					std::shared_ptr<files_accept_session> parent,
					worker& w, private_ctor_tag tag);
				static std::shared_ptr<file_receive_task>
					create(std::shared_ptr<files_accept_session> parent, worker& w);

				~file_receive_task();
				void run(message_blob next_file_info = nullptr);
			private:
                std::uint32_t id() const;
				std::uint8_t on_message_received(message::validated_packet valid_packet) override;
				void on_file_info_received(api::blob_span packet, message::member_id source_id);
				void on_data_block_received(api::blob_span packet, message::member_id source_id);
				void on_done_received(api::blob_span packet, message::member_id source_id);
				
				void do_report_file_info_ack();
				void do_report_complete();
				void do_report_status(message::section_index sect_idx);
			};
		}
	}
}
#endif