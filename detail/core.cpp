#include "detail/core.hpp"

namespace ya_uftp {
	namespace core {
		namespace detail {
			std::vector<std::unique_ptr<execution_unit>> execution_unit::m_net_exec_units;
			std::vector<std::unique_ptr<execution_unit>> execution_unit::m_disk_exec_units;
            std::unique_ptr<execution_unit> execution_unit::m_event_report_unit;
			std::size_t									execution_unit::m_max_thread_count = std::thread::hardware_concurrency();
			execution_unit::execution_unit(private_ctor_tag tag) :
				m_work_guard(boost::asio::make_work_guard<boost::asio::io_context::executor_type>(m_work_ctx.get_executor())) {

			}

			void execution_unit::start() {
				if (not m_work_thread.joinable()) {
					
					std::lock_guard work_guard_lock(m_guard_mutex);
					if (not m_work_guard)
						m_work_guard.emplace(boost::asio::make_work_guard<boost::asio::io_context::executor_type>(m_work_ctx.get_executor()));
					
					m_work_thread = std::thread{ [this]() {
						std::unique_lock work_guard_lock(m_guard_mutex);
						do {
							work_guard_lock.unlock();
							m_work_ctx.run_one();
							m_busy_count++;
							work_guard_lock.lock();
						} while (m_work_guard);
						work_guard_lock.unlock();
						m_busy_count = 0u;
					} };
				}
			}

			void execution_unit::stop() {
				{
					std::lock_guard work_guard_lock(m_guard_mutex);
					m_work_guard = api::nullopt;
				}
				if (m_work_thread.joinable())
					m_work_thread.join();
			}

			boost::asio::io_context& execution_unit::context(){
				return m_work_ctx;
			}

			std::uint64_t execution_unit::busy_count() const {
				return m_busy_count.load();
			}

			execution_unit::~execution_unit() {
				stop();
			}

			bool execution_unit::running() const {
				return m_work_thread.joinable() and not m_work_ctx.stopped();
			}

			void execution_unit::set_max_thread_count(api::optional<std::size_t> count) {
				if (count and count.value() > 0) {
					m_max_thread_count = count.value();
				}
				else
					m_max_thread_count = std::thread::hardware_concurrency();
			}

			execution_unit& execution_unit::get_for_next_job(type t) {
				std::uint64_t min_bc = 0u;
				auto& exec_units = (t == type::disk_io) ? m_disk_exec_units : m_net_exec_units;
				auto winner = static_cast<execution_unit*>(nullptr);
				if (exec_units.size() < m_max_thread_count) {
					exec_units.emplace_back(std::make_unique<execution_unit>(private_ctor_tag{}));
					winner = exec_units.back().get();
				}
				else {
					if (exec_units.size() > 1) {
						for (auto& eu : exec_units) {
							auto bc = eu->busy_count();
							if (min_bc == 0) {
								min_bc = bc;
								winner = eu.get();
								if (min_bc == 0) 
									break;
								
							}
							else if (bc < min_bc) {
								min_bc = bc;
								winner = eu.get();
								break;
							}
						}
					}
					else if (exec_units.size() == 1) {
						winner = exec_units[0].get();
					}
				}
				assert(winner != nullptr);
				return *winner;
            }

            execution_unit& execution_unit::get_for_event_report()
            {
                if (m_event_report_unit == nullptr){
                    m_event_report_unit = std::make_unique<execution_unit>(private_ctor_tag{});
                }
                    
                return *m_event_report_unit;
            }
		}
	}
}