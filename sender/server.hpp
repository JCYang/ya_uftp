#pragma once
#ifndef YA_UFTP_SENDER_SERVER_HPP_
#define YA_UFTP_SENDER_SERVER_HPP_


#include "api_binder.hpp"
#include "sender/adi.hpp"

namespace ya_uftp::sender{
	constexpr auto Kilo = 1024u;
	class server{
		class impl;
		std::unique_ptr<impl>			m_impl;
	public:
		server(const std::size_t max_concurrent_tasks);
		server(const server&) = delete;
		server(server&&) = delete;
		server& operator=(const server&) = delete;
		server& operator=(server&&) = delete;
		~server();
		
		void run();
		void stop();
		
		api::variant<task::launch_result, std::error_condition> 
			sync_files(task::parameters& params);

		api::optional<std::uint32_t> install_progress_monitor(task::progress::listener lst);
		void cancel_task(task::token tsk_token);
	};
}

#endif