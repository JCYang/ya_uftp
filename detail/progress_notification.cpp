#include "detail/progress_notification.hpp"
#include "detail/core.hpp"

namespace ya_uftp {
    namespace core {
        namespace detail {
            static core::detail::execution_unit &progress_executor = core::detail::execution_unit::get_for_event_report();

            progress_notification::progress_notification()
            {
                progress_executor.start();
            }

            void progress_notification::post_progress(sender::task::progress pg)
            {
                progress_executor.context().post([this, prog = std::move(pg)]() {
                    std::lock_guard lsts_lock(m_listener_mutex);
                    for (auto &[key, lst] : m_sender_listeners)
                    {
                        lst(prog);
                    }
                });
            }

            api::optional<std::uint32_t> progress_notification::add_listener(sender::task::progress::listener lst)
            {
                auto lst_key = api::optional<std::uint32_t>{};
                if (lst)
                {
                    std::lock_guard lsts_lock(m_listener_mutex);
                    auto [iter, inserted] = m_sender_listeners.emplace(m_next_key, std::move(lst));
                    if (inserted)
                        lst_key = m_next_key++;
                }

                return lst_key;
            }

            void progress_notification::post_progress(receiver::task::progress pg)
            {
                progress_executor.context().post([this, prog = std::move(pg)]() {
                    std::lock_guard lsts_lock(m_listener_mutex);
                    for (auto &[key, lst] : m_receiver_listeners)
                    {
                        lst(prog);
                    }
                });
            }
            api::optional<std::uint32_t> progress_notification::add_listener(receiver::task::progress::listener lst)
            {
                auto lst_key = api::optional<std::uint32_t>{};
                if (lst)
                {
                    std::lock_guard lsts_lock(m_listener_mutex);
                    auto [iter, inserted] = m_receiver_listeners.emplace(m_next_key, std::move(lst));
                    if (inserted)
                        lst_key = m_next_key++;
                }

                return lst_key;
            }


            progress_notification &progress_notification::get()
            {
                static progress_notification singleton_progress_notification;
                return singleton_progress_notification;
            }
        } // namespace detail
    } // namespace core
}