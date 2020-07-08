#include "ya_uftp.hpp"
#include <iostream>

int main(int argc, char *argv[]){
	if (argc > 1){
		ya_uftp::set_max_threads_count(4);
		auto uftp_server = ya_uftp::sender::server(3);
		uftp_server.run();
		auto params = ya_uftp::sender::task::parameters{};
		params.files = api::fs::path{argv[1]};
		//params.max_speed = 1024 * 128;
		params.robust_factor = 10u;
		auto result = uftp_server.sync_files(params);
		if (api::holds_alternative<ya_uftp::sender::task::launch_result>(result)){
			std::cout << "Task successful launch..." << std::endl;
			std::this_thread::sleep_for(std::chrono::hours(1));
		}
		else
			std::cout << "failed launch rask..." << std::endl;
	}
	return 0;
}