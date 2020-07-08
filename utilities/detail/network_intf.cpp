#include "utilities/network_intf.hpp"

namespace jcy{
namespace network{
namespace detail{
	struct interface_info{
		int				index;
		std::string 	name;
		std::vector<boost::asio::ip::address>	unicast_addrs;
		bool			is_loopback;
		bool			multicast_enabled;
	};
}}
}

#ifdef _WIN32
#include "iphlpapi.h"

#pragma comment(lib, "IPHLPAPI.lib")

namespace jcy{
namespace network{
namespace detail{
	std::vector<interface_info> do_get_interfaces_info(){
		auto bufsize = 16ul * 1024ul;
		auto buf = std::vector<std::uint8_t>(bufsize);
		auto intf_info = std::vector<interface_info>{};
		auto result = 0u, retries = 0u;
		do {
			auto adapter_addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
			result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST, nullptr, adapter_addrs, &bufsize);
			if (result == ERROR_SUCCESS){
				auto current_adapter_info = adapter_addrs;
				while(current_adapter_info){
					auto current_intf_info = interface_info{};
					current_intf_info.name = std::string{current_adapter_info->AdapterName};
					current_intf_info.index = current_adapter_info->IfIndex;
					current_intf_info.is_loopback = (current_adapter_info->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
					current_intf_info.multicast_enabled = not current_adapter_info->NoMulticast;
					auto unicast_addr = current_adapter_info->FirstUnicastAddress;
					while (unicast_addr != nullptr){
						switch (unicast_addr->Address.lpSockaddr->sa_family){
						case AF_INET:
							{
								auto uaddr = reinterpret_cast<sockaddr_in*>(unicast_addr->Address.lpSockaddr);
								auto the_addr = boost::asio::ip::address_v4::bytes_type{};
								auto pa = reinterpret_cast<std::uint8_t *>(&uaddr->sin_addr);
								std::copy(pa, pa + the_addr.size(), the_addr.begin());
								current_intf_info.unicast_addrs.emplace_back(
									boost::asio::ip::make_address_v4(the_addr));
								break;
							}
						case AF_INET6:
							{
								auto uaddr = reinterpret_cast<sockaddr_in6*>(unicast_addr->Address.lpSockaddr);
								auto the_addr = boost::asio::ip::address_v6::bytes_type{};
								auto pa = reinterpret_cast<std::uint8_t *>(&uaddr->sin6_addr);
								std::copy(pa, pa + the_addr.size(), the_addr.begin());
								current_intf_info.unicast_addrs.emplace_back(
									boost::asio::ip::make_address_v6(the_addr));
								break;
							}
						default:
							break;
						}
						unicast_addr = unicast_addr->Next;
					}
					current_adapter_info = current_adapter_info->Next;
					intf_info.emplace_back(std::move(current_intf_info));
				}
			}
			else if (result == ERROR_BUFFER_OVERFLOW){
				retries++;
				bufsize *= 2;
				buf.resize(bufsize);
			}
		} while(result == ERROR_BUFFER_OVERFLOW && 
				retries < 3);
		return intf_info;
	}
}}}
#else // _WIN32

//#ifdef HAS_GETIFADDRS

#include <sys/types.h>
#include <ifaddrs.h>

namespace jcy{
namespace network{
namespace detail{
	std::vector<interface_info> 
		do_get_interfaces_info(){
		auto got_ifaddrs = static_cast<ifaddrs*>(nullptr);
		auto intf_info = std::vector<interface_info>{};
		if (getifaddrs(&got_ifaddrs) == 0){
			auto current_ifaddrs = got_ifaddrs;
			while (current_ifaddrs){
				if (current_ifaddrs->ifa_addr != nullptr){
					auto current_intf_info = interface_info{};
					current_intf_info.name = current_ifaddrs->ifa_name;
					current_intf_info.index = if_nametoindex(current_ifaddrs->ifa_name);
					current_intf_info.is_loopback = (current_ifaddrs->ifa_flags & IFF_LOOPBACK);
					current_intf_info.multicast_enabled = (current_ifaddrs->ifa_flags & IFF_MULTICAST);
					if (current_ifaddrs->ifa_addr->sa_family == AF_INET){
						auto uaddr = reinterpret_cast<sockaddr_in*>(current_ifaddrs->ifa_addr);
						auto the_addr = boost::asio::ip::address_v4::bytes_type{};
						auto pa = reinterpret_cast<std::uint8_t *>(&uaddr->sin_addr);
						std::copy(pa, pa + the_addr.size(), the_addr.begin());
						current_intf_info.unicast_addrs.emplace_back(
							boost::asio::ip::make_address_v4(the_addr));
					}
					else if (current_ifaddrs->ifa_addr->sa_family == AF_INET6){
						auto uaddr = reinterpret_cast<sockaddr_in6*>(current_ifaddrs->ifa_addr);
						auto the_addr = boost::asio::ip::address_v6::bytes_type{};
						auto pa = reinterpret_cast<std::uint8_t *>(&uaddr->sin6_addr);
						std::copy(pa, pa + the_addr.size(), the_addr.begin());
						current_intf_info.unicast_addrs.emplace_back(
							boost::asio::ip::make_address_v6(the_addr));
					}
					intf_info.emplace_back(std::move(current_intf_info));
				}
				current_ifaddrs = current_ifaddrs->ifa_next;
			}
		}
		return intf_info;
	}
}}}
//#else
#endif
		
namespace jcy{
	namespace network{
		class interface::impl{
			int				m_index;
			std::string		m_name;
			std::vector<boost::asio::ip::address>	m_unicast_addrs;
			bool			m_is_loopback;
			bool			m_multicast_enabled;
		public:
			impl(detail::interface_info&& info){
				m_index = info.index;
				m_name = std::move(info.name);
				m_unicast_addrs = std::move(info.unicast_addrs);
				m_is_loopback = info.is_loopback;
				m_multicast_enabled = info.multicast_enabled;
			}

			void learn_index(int idx){
				m_index = idx;
			}
			void learn_name(std::string n){
				m_name = std::move(n);
			}
			void learn_unicast_addrs(std::vector<boost::asio::ip::address> ips){
				m_unicast_addrs = std::move(ips);
			}
			
			int index() const {
				return m_index;
			}
			
			std::string name() const {
				return m_name;
			}
			
			const std::vector<boost::asio::ip::address>&	
				unicast_addresses() const{
				return m_unicast_addrs;
			}
			
			bool is_loopback() const{
				return m_is_loopback;
			}

			bool multicast_enabled() const {
				return m_multicast_enabled;
			}
			
			~impl(){}
		};
		
		interface::interface(std::unique_ptr<impl> sub, priv_ctor_tag) : m_impl(std::move(sub)){}
		
		//interface::interface(const interface&) = delete;
		interface::interface(interface&&) = default;
		//interface& interface::operator=(const interface&) = delete;
		interface& interface::operator=(interface&&) = default;
			
		int interface::index() const{
			return m_impl->index();
		}
		
		std::string interface::name() const{
			return m_impl->name();
		}
		
		const std::vector<boost::asio::ip::address>&	
			interface::unicast_addresses() const{
			return m_impl->unicast_addresses();	
		}
		
		bool interface::is_loopback() const{
			return m_impl->is_loopback();
		}

		bool interface::multicast_enabled() const {
			return m_impl->multicast_enabled();
		}
			
		interface::~interface(){}
		
		const std::vector<interface>& interface::retrieve_all(bool force_refresh){
			static api::optional<std::vector<interface>> interfaces_cache;
			if (not interfaces_cache || force_refresh){
				interfaces_cache.emplace();
				auto infos = detail::do_get_interfaces_info();
				for(auto& intf : infos){
					auto intf_impl = std::make_unique<impl>(std::move(intf));
					interfaces_cache->emplace_back(std::move(intf_impl), priv_ctor_tag{});
				}
			}
			return interfaces_cache.value();
		}
		
		api::optional<std::reference_wrapper<const interface>> 
			interface::by_index(int idx){
			auto result = api::optional<std::reference_wrapper<const interface>>{};
			auto& all_info = retrieve_all();
			for (auto& info : all_info){
				if (info.index() == idx){
					result.emplace(info);
					break;
				}
			}
			return result;
		}
		
		api::optional<std::reference_wrapper<const interface>> 
			interface::by_name(const std::string& name){
			auto result = api::optional<std::reference_wrapper<const interface>>{};
			auto& all_info = retrieve_all();
			for (auto& info : all_info){
				if (info.name() == name){
					result.emplace(info);
					break;
				}
			}
			return result;
		}
	}
}
