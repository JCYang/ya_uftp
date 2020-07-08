#pragma once
#ifndef YA_UFTP_RECEIVER_ADI_HPP_
#define YA_UFTP_RECEIVER_ADI_HPP_

#include "api_binder.hpp"
#include "boost/asio.hpp"

namespace ya_uftp{
	namespace receiver{
		namespace task{
			enum class initiate_result{
				already_monitoring,
				just_started,
				failed_max_tasks_monitoring
			};
			
			enum status
            {
				waiting_registration_confirm,
				waiting_fileinfo,
				receiving_data,
				complete
			};

			struct progress
            {
                std::uint32_t session_id;
                status current_status;
                api::fs::path current_file;
				using listener = std::function<void(progress )>;
            };

			struct parameters{
				using interface_name = api::variant<int, std::string, boost::asio::ip::address>;
				
				std::vector<api::fs::path>					destination_dirs;
				api::optional<std::vector<api::fs::path>>	temp_dirs;
				bool						inplace_tmp_file = false;
				boost::asio::ip::address	public_multicast_addr = boost::asio::ip::make_address_v4("230.4.4.1");
				
				std::size_t					udp_buffer_size = 256 * 1024;
				std::uint16_t				listen_port = 1044;
				
				std::uint8_t				packets_ttl = 1;
				api::optional<std::uint32_t>	client_id;
				std::vector<interface_name>	interfaces_ids;
				std::chrono::milliseconds	grtt = std::chrono::milliseconds(500);
				std::chrono::microseconds	min_grtt = std::chrono::milliseconds(100);
				std::chrono::milliseconds	max_grtt = std::chrono::seconds(15);
				
				bool						follow_symbolic_link = false;
				bool						quit_on_error = false;
				
				// ------ start of Not-Yet-Supported features ------
				bool						enforce_encryption = false;
				api::optional<std::vector<std::uint8_t>>	finger_print;	
				// ------ end of Not-Yet-Supported features ------
				
				parameters();
				parameters(const parameters&);
				parameters(parameters&&);
				parameters& operator=(const parameters&);
				parameters& operator=(parameters&&);
				~parameters();
			};
		}
	}
}

#endif