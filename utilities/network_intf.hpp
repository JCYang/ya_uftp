#pragma once
#ifndef YA_UFTP_UTILITIES_NETWORK_INTF_HPP_
#define YA_UFTP_UTILITIES_NETWORK_INTF_HPP_

#include <boost/asio.hpp>
#include "api_binder.hpp"

namespace jcy{
	namespace network{
		class interface{
			class impl;
			struct priv_ctor_tag{};
			std::unique_ptr<impl>	m_impl;
		public:
			interface(std::unique_ptr<impl> sub, priv_ctor_tag);
			interface(const interface&) = delete;
			interface(interface&&);
			interface& operator=(const interface&) = delete;
			interface& operator=(interface&&);
			
			int index() const;
			std::string name() const;
			const std::vector<boost::asio::ip::address>&	
				unicast_addresses() const;
			bool is_loopback() const;
			bool multicast_enabled() const;
			~interface();
			static const std::vector<interface>& retrieve_all(bool force_refresh = false);
			static api::optional<std::reference_wrapper<const interface>> by_index(int idx);
			static api::optional<std::reference_wrapper<const interface>> by_name(const std::string& name);
		};
	}
}
#endif