#pragma once
#ifndef YA_UFTP_DETAIL_PROGRESS_NOTIFICATION_HPP_
#define YA_UFTP_DETAIL_PROGRESS_NOTIFICATION_HPP_

#include "sender/adi.hpp"
#include "receiver/adi.hpp"
#include <map>

namespace ya_uftp {
    namespace core {
        namespace detail {
            class progress_notification {
                std::mutex m_listener_mutex;
                std::uint32_t m_next_key = 0u;
                std::map<std::uint32_t, sender::task::progress::listener> m_sender_listeners;
                std::map<std::uint32_t, receiver::task::progress::listener> m_receiver_listeners;
                progress_notification();

              public:
                void post_progress(sender::task::progress pg);
                api::optional<std::uint32_t> add_listener(sender::task::progress::listener lst);

                void post_progress(receiver::task::progress pg);
                api::optional<std::uint32_t> add_listener(receiver::task::progress::listener lst);

                static progress_notification &get();
            };
        } // namespace detail
    }
}
#endif