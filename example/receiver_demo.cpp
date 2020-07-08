#include "ya_uftp.hpp"
#include <iostream>

int main(int argc, char *argv[]){
	if (argc > 1){
		auto uftp_server = ya_uftp::receiver::server(3);
		uftp_server.start_run_in_background();
		auto params = ya_uftp::receiver::task::parameters{};
		params.destination_dirs.emplace_back(argv[1]);
		auto result = uftp_server.monitor(params);
		if (api::holds_alternative<ya_uftp::receiver::task::initiate_result>(result)){
			std::cout << "Task successful launch..." << std::endl;
			std::this_thread::sleep_for(std::chrono::hours(1));
		}
		else
			std::cout << "failed launch rask..." << std::endl;
	}
	return 0;
}
