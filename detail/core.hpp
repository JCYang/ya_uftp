#pragma once
#ifndef YA_UFTP_DETAIL_CORE_HPP_
#define YA_UFTP_DETAIL_CORE_HPP_

#include "api_binder.hpp"
#include "boost/asio.hpp"

namespace ya_uftp {
	namespace core {
		namespace detail {
			class execution_unit {
				boost::asio::io_context	m_work_ctx;
				std::mutex				m_guard_mutex;
				api::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>
					m_work_guard;
				std::atomic<std::uint64_t>	m_busy_count = 0u;
				std::thread					m_work_thread;
				struct private_ctor_tag {};
				static std::vector<std::unique_ptr<execution_unit>> m_net_exec_units;
				static std::vector<std::unique_ptr<execution_unit>> m_disk_exec_units;
                static std::unique_ptr<execution_unit> m_event_report_unit;
				static std::size_t									m_max_thread_count;
			public:
				enum class type {
					disk_io,
					network_io
				};

				execution_unit(private_ctor_tag tag);
				execution_unit(execution_unit&& from) = delete;
				execution_unit(const execution_unit& from) = delete;
				
				void start();
				void stop();

				boost::asio::io_context& context();
				std::uint64_t busy_count() const;

				~execution_unit();

				bool running() const; 
				static void set_max_thread_count(api::optional<std::size_t> count);
				static execution_unit& get_for_next_job(type t);
                static execution_unit& get_for_event_report();
			};
		}
	}
}

#endif