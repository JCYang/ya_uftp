// ADI is short for Application Data Interface
#pragma once
#ifndef YA_UFTP_SENDER_ADI_HPP_
#define YA_UFTP_SENDER_ADI_HPP_

#include "api_binder.hpp"
#include "boost/asio.hpp"

namespace ya_uftp {
	namespace sender {
		namespace task {
			using token = std::size_t;
			enum class initiate_result{
				already_syncing,
				just_started,
				already_queueing,
				just_queued
			};
			
			enum class status{
				announcing,
				confirming_registration,
				authenticating,
				transferring,
				restransferring,
				sending_done_nofitication,
				complete,
				forced_end
			};
			
			struct progress{
                std::uint32_t	session_id;
				status 			current_status;
                api::fs::path	current_file;
				using listener = std::function<void (const progress&)>;
			};
			
			struct launch_result{
				initiate_result	result;
				token			task_token;
			};
			
			struct parameters{
				using interface_name = api::variant<int, std::string, boost::asio::ip::address>;
				struct client_info{
					std::uint32_t	id;
					api::optional<std::vector<std::uint8_t>>	finger_print;			  
				};
				
				struct single_file{
					api::fs::path					source_path;
					api::optional<api::fs::path>	source_base_dir;
					api::optional<api::fs::path>	dest_path;
				};
				
				using file_list = api::variant<api::fs::path, std::vector<single_file>>;
				file_list					files;
				api::optional<api::fs::path>	base_dir;
				bool						force_sync = true;
				boost::asio::ip::address	public_multicast_addr = boost::asio::ip::make_address_v4("230.4.4.1");
				boost::asio::ip::address	private_multicast_addr = boost::asio::ip::make_address_v4("230.5.5.8"); 
				std::size_t					udp_buffer_size = 256 * 1024;
				std::uint16_t				destination_port = 1044;
				api::optional<std::uint16_t>		source_port;
				std::uint8_t				packets_ttl = 1;
				api::optional<std::uint32_t>		server_id;
				api::optional<interface_name>	out_interface_id;
				std::chrono::milliseconds	grtt = std::chrono::milliseconds(500);
				std::chrono::microseconds	min_grtt = std::chrono::milliseconds(100);
				std::chrono::milliseconds	max_grtt = std::chrono::seconds(15);
				std::uint8_t				robust_factor = 20u;
				std::uint16_t				block_size = 1300u;
				bool						follow_symbolic_link = false;
				bool						quit_on_error = false;
				api::optional<std::uint64_t>		max_speed;
				// ------ start of Not-Yet-Supported features ------
				bool						need_authenticate_clients = false;
				api::optional<std::vector<client_info>>	allowed_clients;
				api::optional<std::uint32_t>		task_id;
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