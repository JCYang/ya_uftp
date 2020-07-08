#include "receiver/server.hpp"

#include "detail/message.hpp"
#include "utilities/network_intf.hpp"

#include "receiver/detail/announcement_monitor.hpp"
#include "detail/core.hpp"
#include "detail/progress_notification.hpp"
#include "boost/asio.hpp"

namespace ya_uftp::receiver{
	class server::impl{
		const std::size_t					m_max_concurrent_tasks;
		core::detail::execution_unit&		m_net_exec_unit;
		core::detail::execution_unit&		m_disk_exec_unit;

		static std::error_condition validate_target(const api::fs::path& fp){
			auto ec = api::error_code{};
			auto result = std::error_condition{};
			if (not api::fs::exists(fp, ec))
				result = std::make_error_condition(std::errc::no_such_file_or_directory);
			if (not api::fs::is_regular_file(fp, ec) &&
				not api::fs::is_directory(fp, ec) &&
				not api::fs::is_symlink(fp, ec))
				result = std::make_error_condition(std::errc::not_supported);
			return result;
		}
		
		std::map<boost::asio::ip::address_v4, std::weak_ptr<detail::announcement_monitor>>	m_active_v4_sessions;
		std::map<boost::asio::ip::address_v6, std::weak_ptr<detail::announcement_monitor>>	m_active_v6_sessions;
	public:
		impl(const std::size_t max_concurrent_tasks)
			: m_max_concurrent_tasks(max_concurrent_tasks),
			m_net_exec_unit(core::detail::execution_unit::get_for_next_job(core::detail::execution_unit::type::network_io)),
			m_disk_exec_unit(core::detail::execution_unit::get_for_next_job(core::detail::execution_unit::type::disk_io)) {}
				
		~impl(){
			stop();
		}
		
		void run(){
			m_net_exec_unit.start();
			m_disk_exec_unit.start();
		}
		
		void stop(){
			auto outdated_v4_addrs = std::vector<boost::asio::ip::address_v4>{};
			for (auto [addr, s] : m_active_v4_sessions) {
				auto sp = s.lock();
				if (sp != nullptr)
					sp->stop();
				else {
					outdated_v4_addrs.emplace_back(addr);
				}
			}
			for (auto& addr : outdated_v4_addrs) {
				m_active_v4_sessions.erase(addr);
			}

			auto outdated_v6_addrs = std::vector<boost::asio::ip::address_v6>{};
			for (auto [addr, s] : m_active_v6_sessions) {
				auto sp = s.lock();
				if (sp != nullptr)
					sp->stop();
				else {
					outdated_v6_addrs.emplace_back(addr);
				}
			}
			for (auto& addr : outdated_v6_addrs) {
				m_active_v6_sessions.erase(addr);
			}
		}
		
		api::variant<task::initiate_result, std::error_condition> 
			monitor(task::parameters& params){
			if (m_active_v4_sessions.size() + m_active_v6_sessions.size() >= m_max_concurrent_tasks)
				return task::initiate_result::failed_max_tasks_monitoring;
			if (params.public_multicast_addr.is_v4() and
				params.public_multicast_addr.is_multicast()){
				auto addr = params.public_multicast_addr.to_v4();
				auto siter = m_active_v4_sessions.find(addr);
				if (siter == m_active_v4_sessions.end() or
					siter->second.expired()){
					auto new_monitor = detail::announcement_monitor::create(
						m_net_exec_unit.context(), m_disk_exec_unit.context(), params);
					new_monitor->run();
					m_active_v4_sessions.emplace(addr, new_monitor);
					return task::initiate_result::just_started;
				}
				else
					return task::initiate_result::already_monitoring;
			}
			else if (params.public_multicast_addr.is_v6() and
				params.public_multicast_addr.is_multicast()){
				auto addr = params.public_multicast_addr.to_v6();
				auto siter = m_active_v6_sessions.find(addr);
				if (siter == m_active_v6_sessions.end() or
					siter->second.expired()){
					auto new_monitor = detail::announcement_monitor::create(
						m_net_exec_unit.context(), m_disk_exec_unit.context(), params);
					new_monitor->run();
					m_active_v6_sessions.emplace(addr, new_monitor);
					return task::initiate_result::just_started;
				}
				else
					return task::initiate_result::already_monitoring;
			}
			else
				return std::make_error_condition(std::errc::not_supported);
		}
	};
	
	server::server(const std::size_t max_concurrent_tasks)
		: m_impl(std::make_unique<impl>(max_concurrent_tasks)){}
		
	server::~server() = default;	
	
	void server::start_run_in_background(){
		m_impl->run();
	}
	
	void server::stop(){
		m_impl->stop();
	}
	
	api::variant<task::initiate_result, std::error_condition> 
		server::monitor(task::parameters& params){
		return m_impl->monitor(params);
	}

	api::optional<std::uint32_t> server::install_progress_monitor(task::progress::listener lst)
    {
        return core::detail::progress_notification::get().add_listener(std::move(lst));
    }
    }