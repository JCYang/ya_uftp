#include "sender/server.hpp"

#include "detail/message.hpp"
#include "detail/core.hpp"
#include "detail/progress_notification.hpp"
#include "utilities/network_intf.hpp"
#include "sender/detail/files_delivery_session.hpp"
#include <map>
#include <random>
#include "boost/asio.hpp"

namespace ya_uftp::sender{
	class server::impl{
		const std::size_t					m_max_concurrent_tasks;
		core::detail::execution_unit&		m_net_exec_unit;
		core::detail::execution_unit&		m_disk_exec_unit;
		static task::token generate_task_token(const task::parameters& params){
			if (api::holds_alternative<api::fs::path>(params.files)){
				auto& fpath = api::get<api::fs::path>(params.files);
				return std::hash<std::string>{}(fpath.string() + '_' + 
					params.public_multicast_addr.to_string() + ':' + std::to_string(params.destination_port));
			}
			else{
				// ToDo: fix all these
				return 0u;
			}
		}
		
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
		
		std::map<task::token, std::weak_ptr<detail::files_delivery_session>>	m_active_sessions;
		std::map<task::token, task::parameters>		m_waiting_queue;
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
			std::for_each(m_active_sessions.begin(), m_active_sessions.end(),
				[](auto kv){ 
					auto ss = kv.second.lock();
					if (ss)
						ss->force_end();
				});
		}
		
		api::variant<task::launch_result, std::error_condition> 
			sync_files(task::parameters& params){
			if (api::holds_alternative<api::fs::path>(params.files)){
				auto& file = api::get<api::fs::path>(params.files);
				auto ec = validate_target(file);
				if (ec)
					return ec;
			}
			else{
				// for list of targets, we silently ignore all the invalid/non-exists entries,
				// unless there's no valid entries left.
				// ToDo: add logs here
				auto& files = api::get<std::vector<task::parameters::single_file>>(params.files);
				auto cleaned_files = std::vector<task::parameters::single_file>{};
				auto include_invalid_entry = false;
				for (auto object : files) {
					auto ec = validate_target(object.source_path);
					if (!ec)
						cleaned_files.emplace_back(object);
					else
						include_invalid_entry = true;
				}
				if (include_invalid_entry){
					if (not cleaned_files.empty())
						params.files = std::move(cleaned_files);
					else
						return std::make_error_condition(std::errc::invalid_argument);
				}
			}
			if (params.public_multicast_addr == params.private_multicast_addr ||
				params.public_multicast_addr.is_v4() != params.private_multicast_addr.is_v4())
				return std::make_error_condition(std::errc::not_supported);
			if (params.allowed_clients)
				return std::make_error_condition(std::errc::not_supported);
			auto tsk_token = generate_task_token(params);
			auto iter = m_active_sessions.find(tsk_token);
			if (iter != m_active_sessions.end()){
				if (not iter->second.expired())
					return task::launch_result{task::initiate_result::already_syncing, tsk_token};
				else
					m_active_sessions.erase(iter);
			}
			auto qiter = m_waiting_queue.find(tsk_token);
			if (qiter != m_waiting_queue.end())
				return task::launch_result{task::initiate_result::already_queueing, tsk_token};
			
			if (m_active_sessions.size() < m_max_concurrent_tasks){
				auto& ne = core::detail::execution_unit::get_for_next_job(core::detail::execution_unit::type::network_io);
				if (not ne.running())
					ne.start();
				auto& de = core::detail::execution_unit::get_for_next_job(core::detail::execution_unit::type::disk_io);
				if (not de.running())
					de.start();

				auto new_session = detail::files_delivery_session::create(
					ne.context(), 
					de.context(), params);
				m_active_sessions.emplace(tsk_token, new_session);
				new_session->start();
				return task::launch_result{task::initiate_result::just_started, tsk_token};
			}
			else{
				m_waiting_queue.emplace(tsk_token, params);
				return task::launch_result{task::initiate_result::just_queued, tsk_token};
			}
		}

		void cancel_task(task::token tsk_token){
			auto iter = m_active_sessions.find(tsk_token);
			if (iter != m_active_sessions.end()){
				auto ss = iter->second.lock();
				if (ss != nullptr)
					ss->force_end();
				else
					m_active_sessions.erase(iter);
			}
		}
	};
	
	server::server(const std::size_t max_concurrent_tasks)
		: m_impl(std::make_unique<impl>(max_concurrent_tasks)){}
		
	server::~server() = default;	
	
	void server::run(){
		m_impl->run();
	}
	
	void server::stop(){
		m_impl->stop();
	}
	
	api::variant<task::launch_result, std::error_condition> 
		server::sync_files(task::parameters& params){
		return m_impl->sync_files(params);
	}
	
	api::optional<std::uint32_t> server::install_progress_monitor(task::progress::listener lst)
    {
        return core::detail::progress_notification::get().add_listener(std::move(lst));
    }
	void server::cancel_task(task::token tsk_token){
		m_impl->cancel_task(tsk_token);
	}
}