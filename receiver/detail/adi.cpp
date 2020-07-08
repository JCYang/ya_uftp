#include "receiver/adi.hpp"

namespace ya_uftp::receiver{
	namespace task{
		parameters::parameters() = default;
		parameters::parameters(const parameters&) = default;
		parameters::parameters(parameters&&) = default;
		parameters& parameters::operator=(const parameters&) = default;
		parameters& parameters::operator=(parameters&&) = default;
		parameters::~parameters() = default;
	}
}
