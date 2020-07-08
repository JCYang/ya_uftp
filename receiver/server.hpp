#pragma once
#ifndef YA_UFTP_RECEIVER_SERVER_HPP_
#define YA_UFTP_RECEIVER_SERVER_HPP_

#include "receiver/adi.hpp"
#include <memory>

namespace ya_uftp{
	namespace receiver{
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
			
			api::variant<task::initiate_result, std::error_condition>
				monitor(task::parameters& params);
			void start_run_in_background();
			void stop();

			api::optional<std::uint32_t> install_progress_monitor(task::progress::listener lst);
		};
	}
}

#endif
