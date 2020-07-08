#include "receiver/detail/announcement_monitor.hpp"
#include "detail/core.hpp"

#include "utilities/network_intf.hpp"
#include "boost/endian/conversion.hpp"

namespace ya_uftp::receiver::detail{
	
	announcement_monitor::announcement_monitor(boost::asio::io_context& net_io_ctx, 
			boost::asio::io_context& file_io_ctx,
			const task::parameters& params,  
			private_ctor_tag tag) :
			m_socket(net_io_ctx),
			m_net_io_ctx(net_io_ctx),
			m_file_io_ctx(file_io_ctx),
			m_params(params){
		auto ec = boost::system::error_code{};
		if (m_params.public_multicast_addr.is_v4()){
			m_socket.open(boost::asio::ip::udp::v4(), ec);
		}
		else{
			m_socket.open(boost::asio::ip::udp::v6(), ec);
		}
		if (ec){
			std::cout << "Failed to open udp socket\n";
			return;
		}
		m_socket.set_option(boost::asio::ip::udp::socket::reuse_address(true), ec);
		if (ec){
			std::cout << "Failed to set reuse address. \nReason: " << ec.message() << '\n';
			return;
		}
		if (m_params.public_multicast_addr.is_v4()){
			m_socket.bind(
				boost::asio::ip::udp::endpoint{
					boost::asio::ip::address_v4::any(),
					m_params.listen_port,
				}, ec);
		}
		else{
			m_socket.bind(
				boost::asio::ip::udp::endpoint{
					boost::asio::ip::address_v6::any(),
					m_params.listen_port,
				}, ec);
		}
		
		if (ec){
			std::cout << "Failed to bind to multicast addr. \nReason: " << ec.message() << '\n';
			return;
		}
		auto& active_interfaces = jcy::network::interface::retrieve_all();
		for (auto& intf_info : active_interfaces){
			if (not intf_info.is_loopback()){
				for (auto& interface_addr : intf_info.unicast_addresses()){
					if (m_params.public_multicast_addr.is_v4()){
						auto mcast_addr = m_params.public_multicast_addr.to_v4();
						if (interface_addr.is_v4() and not interface_addr.is_loopback()){
							m_socket.set_option(boost::asio::ip::multicast::join_group(
								mcast_addr, 
								interface_addr.to_v4()), ec);
							if (ec){
								//std::cout << "Failed to join multicast group for interface " << 
									//interface_addr.to_v4().to_string() << ", reason : " << ec.message() << '\n' ;
							}
							else{
								//std::cout << "Successfully join multicast group for interface " << 
									//interface_addr.to_v4().to_string() << '\n' ;
							}
						}
					}
					else{
						auto mcast_addr = m_params.public_multicast_addr.to_v6();
						if (interface_addr.is_v6() and not interface_addr.is_loopback()){
							m_socket.set_option(boost::asio::ip::multicast::join_group(
								mcast_addr, 
								intf_info.index()), ec);
							if (ec){
								//std::cout << "Failed to join multicast group for interface " << 
									//interface_addr.to_v6().to_string() << ", reason : " << ec.message() << '\n' ;
							}
							else{
								//std::cout << "Successfully join multicast group for interface " << 
									//interface_addr.to_v6().to_string() << '\n' ;
							}
						}
					}
				}
			}
		}
	}
	
	void announcement_monitor::do_monitor_announcement(){
		m_socket.async_receive_from(boost::asio::buffer(m_msg_buffer), m_sender_ep,
			[this_monitor = shared_from_this()]
			(const boost::system::error_code ec, std::size_t bytes_received){
				if (not ec){
					auto packet = api::blob_span{this_monitor->m_msg_buffer.data(), bytes_received};
					auto valid_msg = message::basic_validate_packet(packet);
					if (valid_msg and 
						valid_msg->msg_header.message_role == message::role::announce){
						auto announce_msg = message::announce::parse_packet(valid_msg->msg_body);
						if (announce_msg){
							if (this_monitor->m_params.public_multicast_addr.is_v4() and
								api::holds_alternative<std::reference_wrapper<const message::announce::v4_multicast_addr>>(announce_msg->mcast_addrs)){
								auto& mcast_addrs = api::get<std::reference_wrapper<const message::announce::v4_multicast_addr>>(announce_msg->mcast_addrs).get();
								auto pmaddr = this_monitor->m_params.public_multicast_addr.to_v4();
								if (boost::endian::big_to_native(mcast_addrs.public_one.s_addr) == pmaddr.to_uint()){
									
									if (announce_msg->allowed_clients.empty()){
										auto addr_buf = std::array<std::uint8_t, 4>{};
										auto addr_src = reinterpret_cast<const std::uint8_t*>(&mcast_addrs.private_one.s_addr);
										std::copy(addr_src, addr_src + 4, addr_buf.data());
										auto private_mcast_addr = boost::asio::ip::make_address_v4(addr_buf);

										
										auto iter = this_monitor->m_known_announcements.find(private_mcast_addr);
										if (iter == this_monitor->m_known_announcements.end()) {
											auto ts = session_prop{ announce_msg->main.msg_timestamp_usecs_high, announce_msg->main.msg_timestamp_usecs_low };
											auto [it, inserted] = this_monitor->m_known_announcements.emplace(private_mcast_addr, ts);
											if (inserted)
												iter = it;
										}
										else {
											iter->second.ts_high = announce_msg->main.msg_timestamp_usecs_high;
											iter->second.ts_low = announce_msg->main.msg_timestamp_usecs_low;
										}
										
										if (iter->second.pointer.expired()) {
											auto& ne = core::detail::execution_unit::get_for_next_job(core::detail::execution_unit::type::network_io);
											if (not ne.running())
												ne.start();
											auto& de = core::detail::execution_unit::get_for_next_job(core::detail::execution_unit::type::disk_io);
											if (not de.running())
												de.start();

											auto new_session = files_accept_session::create(
												ne.context(),
												de.context(),
												private_mcast_addr,
												this_monitor->m_sender_ep, true, announce_msg->main.block_size,
												announce_msg->main.robust_factor, valid_msg->msg_header.session_id,
												valid_msg->msg_header.source_id,
												iter->second.ts_high, iter->second.ts_low,
												this_monitor->m_params);

											new_session->start();
											iter->second.pointer = new_session;
										}
										
									}
								}
							}
							else if (this_monitor->m_params.public_multicast_addr.is_v6() and
								api::holds_alternative<std::reference_wrapper<const message::announce::v6_multicast_addr>>(announce_msg->mcast_addrs)){
								auto& mcast_addrs = api::get<std::reference_wrapper<const message::announce::v6_multicast_addr>>(announce_msg->mcast_addrs).get();
								auto pmaddr = this_monitor->m_params.public_multicast_addr.to_v6();
								if (std::memcmp(reinterpret_cast<const std::uint8_t *>(&mcast_addrs.public_one), pmaddr.to_bytes().data(), 16) == 0){
									if (announce_msg->allowed_clients.empty()){
										auto addr_buf = std::array<std::uint8_t, 16>{};
										auto addr_src = reinterpret_cast<const std::uint8_t*>(&mcast_addrs.private_one);
										std::copy(addr_src, addr_src + 16, addr_buf.data());
										auto private_mcast_addr = boost::asio::ip::make_address_v6(addr_buf);

										
										auto iter = this_monitor->m_known_announcements.find(private_mcast_addr);
										if (iter == this_monitor->m_known_announcements.end()) {
											auto ts = session_prop{ announce_msg->main.msg_timestamp_usecs_high, announce_msg->main.msg_timestamp_usecs_low };
											auto [it, inserted] = this_monitor->m_known_announcements.emplace(private_mcast_addr, ts);
											if (inserted)
												iter = it;
										}
										else {
											iter->second.ts_high = announce_msg->main.msg_timestamp_usecs_high;
											iter->second.ts_low = announce_msg->main.msg_timestamp_usecs_low;
										}

										auto& ne = core::detail::execution_unit::get_for_next_job(core::detail::execution_unit::type::network_io);
										if (not ne.running())
											ne.start();
										auto& de = core::detail::execution_unit::get_for_next_job(core::detail::execution_unit::type::disk_io);
										if (not de.running())
											de.start();

										if (iter->second.pointer.expired()) {
											auto new_session = files_accept_session::create(
												ne.context(),
												de.context(),
												private_mcast_addr,
												this_monitor->m_sender_ep, true, announce_msg->main.block_size,
												announce_msg->main.robust_factor, valid_msg->msg_header.session_id,
												valid_msg->msg_header.source_id,
												iter->second.ts_high, iter->second.ts_low,
												this_monitor->m_params);
											new_session->start();
											iter->second.pointer = new_session;
										}
									}
								}
							}
						}
					}
					this_monitor->do_monitor_announcement();
				}
			});
	}
	
	void announcement_monitor::run(){
		do_monitor_announcement();
	}

	void announcement_monitor::stop() {
		auto ec = boost::system::error_code{};
		m_socket.cancel(ec);
		auto outdated_sessions_addrs = std::vector<boost::asio::ip::address>{};
		for (auto [addr, sp] : m_known_announcements) {
			auto p = sp.pointer.lock();
			if (p != nullptr)
				p->stop();
			else {
				outdated_sessions_addrs.emplace_back(addr);
			}
		}
		for (auto addr : outdated_sessions_addrs) {
			m_known_announcements.erase(addr);
		}
	}
		
	std::shared_ptr<announcement_monitor> 
		announcement_monitor::create(
			boost::asio::io_context& net_io_ctx,
			boost::asio::io_context& file_io_ctx,
			const task::parameters& params){
		return std::make_shared<announcement_monitor>(net_io_ctx, file_io_ctx, params, private_ctor_tag{});
	}
}
