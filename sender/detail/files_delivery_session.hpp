#pragma once
#ifndef YA_UFTP_SENDER_DETAIL_FILES_DELIVERY_SESSION_HPP_
#define YA_UFTP_SENDER_DETAIL_FILES_DELIVERY_SESSION_HPP_

#include "sender/adi.hpp"
#include "detail/common.hpp"
#include "detail/message.hpp"
#include "sender/detail/worker.hpp"

namespace ya_uftp{
	namespace sender{
		namespace detail{
			class files_delivery_session : 
				public std::enable_shared_from_this<files_delivery_session>, 
				public worker::employer {
				struct private_ctor_tag{};
			public:
				class file_send_task;
				
				using monitor = std::function<void (const task::progress&)>;
				class visa{
					visa();
					friend class file_send_task;
				};
				
				files_delivery_session(
					boost::asio::io_context& net_io_ctx, 
					boost::asio::io_context& file_io_ctx,
					const task::parameters& params,  
					private_ctor_tag tag);
				void start();
				void on_file_send_complete(visa key);
				void on_file_send_error(visa key);
				static std::shared_ptr<files_delivery_session> 
					create(
						boost::asio::io_context& net_io_ctx,
						boost::asio::io_context& file_io_ctx,
						const task::parameters& params);
				
				// because the session typically end when all files have been sent(be it success or not)
				// and are self managed(by shared_ptr) during the whole run,
				// in order to prematurely end it, we need to force it
				void force_end();
				
				std::uint32_t id() const;
				~files_delivery_session();
			private:
				enum class phase : std::uint8_t{
					stop,
					announcing,
					authenticating,
					running_transfer_task,
					complete
				};
				
				void do_announce();
				
				bool do_send_registered_confirm();
				void enter_transfer_phase();
				void do_send_next_file();
				bool do_notify_session_completed();
				bool do_send_done_conf();
				api::fs::path compute_file_remote_name(api::fs::path& fpath, 
					const api::optional<api::fs::path>& base_dir);
				
				void on_worker_bucket_freed() override;
				void on_message_received(message::validated_packet valid_packet) override;
				void on_register_msg_received(api::blob_span packet, message::member_id source_id);
				
				std::unique_ptr<worker>			m_worker;
				session_context&				m_context;
				task::parameters::file_list		m_files;
				api::optional<api::fs::path>	m_base_dir;
				message::file_id_type			m_current_file_id = 1u;
				phase							m_phase = phase::stop;
				bool							m_is_first_file = true;
				api::fs::recursive_directory_iterator	m_next_entity;
				std::uint32_t					m_rounds = 0u;
				std::map<message::member_id, session_context::receiver_properties>	m_receivers_states;
				std::uint32_t					m_last_round_response_count = 0u;
				
				api::optional<worker::send_args>	m_blocked_msg_args;
				std::function<void()>				m_blocked_task;
			};
		}
	}
}

#endif