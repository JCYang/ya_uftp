#include "detail/message.hpp"
#include "boost/endian/conversion.hpp"

#include "detail/common.hpp"

namespace ya_uftp{
	namespace message{
		unsigned char quantize_grtt(double grtt){
		   if (grtt > max_grtt)
			   grtt = max_grtt;
		   else if (grtt < min_grtt)
			   grtt = min_grtt;
		   if (grtt < (33*min_grtt))
			   return ((unsigned char)(grtt / min_grtt) - 1);
		   else
			   return ((unsigned char)(ceil(255.0 -
									   (13.0 * log(max_grtt/grtt)))));
		}
		
		double dequantize_grtt(unsigned char qrtt){
		   return ((qrtt <= 31) ?
				   (((double)(qrtt+1))*(double)min_grtt) :
				   (max_grtt/exp(((double)(255-qrtt))/(double)13.0)));
		}
		
		std::uint8_t quantize_group_size(std::uint32_t group_size){
			double M;
			int E;
			int rval;

			M = group_size;
			E = 0;
			while (M >= 10) {
				M /= 10;
				E++;
			}
			rval = ((int)((M * 32.0 / 10.0) + 0.5)) << 3;
			if (rval > 0xFF) {
				M /= 10;
				E++;
				rval = ((int)((M * 32.0 / 10.0) + 0.5)) << 3;
			}
			rval |= E;
			
			return rval;
		}
		
		std::uint32_t dequantize_group_size(std::uint8_t q_group_size){
			int E, i;
			double rval;

			E = q_group_size & 0x7;
			rval = (q_group_size >> 3) * (10.0 / 32.0);
			for (i = 0; i < E; i++) {
				rval *= 10;
			}

			return (int)(rval + 0.5);
		}
		
		void set_timestamp(std::uint32_t& ts_high, std::uint32_t& ts_low){
			auto time_now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch());
			auto ts = static_cast<std::uint64_t>(time_now.count());
			ts_high = static_cast<std::uint32_t>((ts >> 32) & 0xffffffff);
			ts_low = static_cast<std::uint32_t>(ts & 0xffffffff);
		}
		
		std::chrono::microseconds calculate_rtt(std::uint32_t ts_high, std::uint32_t ts_low){
			auto ts = (static_cast<std::uint64_t>(ts_high) << 32) + ts_low;
			return std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::system_clock::now().time_since_epoch() - std::chrono::microseconds{ts});
		}
		
		// FIX-ME: if you know better way to do this, please help
		std::set<block_index> 
			extract_lost_blocks_ids(const api::blob_view nak_map){
			
			using native_uint = machine_native<>::uint;
			auto loss_blocks_ids = std::set<block_index>{};
			static constexpr auto bits_per_octet = 8u;
			
			auto native_map = api::basic_string_view<native_uint>{
				reinterpret_cast<const native_uint*>(nak_map.data()), nak_map.size() / sizeof(native_uint)};
			auto tail = nak_map.size() % sizeof(native_uint);
			for (auto i = 0u; i < native_map.size(); i++){
				if (native_map[i] != 0){
					for (auto j = i * sizeof(native_uint); 
						j < (i + 1) * sizeof(native_uint);
						j++){
						if (nak_map[j] != 0){
							for (auto k = 0u; k < bits_per_octet; k++){
								if ((nak_map[j] >> k) & 0x1)
									loss_blocks_ids.emplace(j * bits_per_octet + k);
							}
						}
					}
				}
			}
			if (tail){
				auto tail_head = nak_map.size() - tail;
				for (auto j = tail_head; j < nak_map.size(); j++){
					if (nak_map[j] != 0){
							for (auto k = 0u; k < bits_per_octet; k++){
								if ((nak_map[j] >> k) & 0x1)
									loss_blocks_ids.emplace(j * bits_per_octet + k);
							}
						} 
				}
			}
			return loss_blocks_ids;
		}
		
		validated_packet::validated_packet(const protocol_header& hdr, api::blob_span body)
			: msg_header(hdr), msg_body(body){}
			
		api::optional<validated_packet> basic_validate_packet(api::blob_span packet){
			auto result = api::optional<validated_packet>{};
			auto uftp_hdr = reinterpret_cast<const protocol_header*>(packet.data());
			if (uftp_hdr->head_magic == magic &&
				uftp_hdr->message_role >= role::announce &&
				uftp_hdr->message_role < role::invalid &&
				(uftp_hdr->message_role == role::encrypted ||
				static_cast<role>(packet[sizeof(protocol_header)]) == uftp_hdr->message_role)){
				result.emplace(*uftp_hdr, packet.subspan(sizeof(protocol_header)));
			}
			return result;
		}
		
		announce::parsed::parsed(const announce& hdr, api::variant<std::reference_wrapper<const v4_multicast_addr>, 
					std::reference_wrapper<const v6_multicast_addr>> mcast) : main(hdr), mcast_addrs(mcast) {}
		
		api::optional<announce::parsed> 
			announce::parse_packet(api::blob_span packet){
			auto result = api::optional<announce::parsed>{};
			auto announce_hdr = reinterpret_cast<announce*>(packet.data());
			if (std::uint32_t addr_len = (announce_hdr->ipv6 ? 32u : 8u), 
				header_len = announce_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(announce) - addr_len; 
				announce_hdr->the_role == role::announce &&
				header_len >= sizeof(announce) + addr_len &&
				header_len <= packet.size()){
				
				// ToDo: for the sake of security, add more message validations here
				// for example, multicast addresses validation.
				//result.emplace(*announce_hdr);
				
				boost::endian::big_to_native_inplace(announce_hdr->block_size);
				boost::endian::big_to_native_inplace(announce_hdr->msg_timestamp_usecs_high);
				boost::endian::big_to_native_inplace(announce_hdr->msg_timestamp_usecs_low);
				if (announce_hdr->ipv6)
					result.emplace(*announce_hdr, *reinterpret_cast<const announce::v6_multicast_addr*>(packet.data() + sizeof(announce)));
				else
					result.emplace(*announce_hdr, *reinterpret_cast<const announce::v4_multicast_addr*>(packet.data() + sizeof(announce)));
				
				if (packet.size() >= header_len + sizeof(member_id)){
					const auto count = (packet.size() - header_len) / sizeof(member_id);
					const auto member_ids = reinterpret_cast<member_id*>(packet.data() + header_len);
					
					result->allowed_clients = api::basic_string_view<member_id>{
						member_ids, count};
				}
				// ToDo: support parse valid extensions
			}
			return result;
		}
		
		void announce::make_transfer_ready(){
			boost::endian::native_to_big_inplace(block_size);
			boost::endian::native_to_big_inplace(msg_timestamp_usecs_high);
			boost::endian::native_to_big_inplace(msg_timestamp_usecs_low);
		}
		
		receiver_register::parsed::parsed(const receiver_register& hdr) : main(hdr) {}
		
		api::optional<receiver_register::parsed> 
			receiver_register::parse_packet(api::blob_span packet){
			auto result = api::optional<receiver_register::parsed>{};
			auto register_hdr = reinterpret_cast<receiver_register*>(packet.data());
			boost::endian::big_to_native_inplace(register_hdr->ecdh_key_length);
			if (std::uint32_t header_len = register_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(receiver_register) - register_hdr->ecdh_key_length; 
				register_hdr->the_role == role::receiver_register &&
				header_len >= sizeof(receiver_register) &&
				header_len <= packet.size()){
				
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*register_hdr);
				
				boost::endian::big_to_native_inplace(register_hdr->msg_timestamp_usecs_high);
				boost::endian::big_to_native_inplace(register_hdr->msg_timestamp_usecs_low);
				
				if (packet.size() >= header_len + sizeof(member_id)){
					const auto count = (packet.size() - header_len) / sizeof(member_id);
					const auto member_ids = reinterpret_cast<member_id*>(packet.data() + header_len);
					result->receiver_ids = api::basic_string_view<member_id>{
						member_ids, count};
				}
				// ToDo: support parse valid extensions
			}
			return result;
		}
		
		void receiver_register::make_transfer_ready(){
			boost::endian::native_to_big_inplace(ecdh_key_length);
			boost::endian::native_to_big_inplace(msg_timestamp_usecs_high);
			boost::endian::native_to_big_inplace(msg_timestamp_usecs_low);
		}
		
		reg_conf::parsed::parsed(const reg_conf& hdr) : main(hdr) {}
		
		api::optional<reg_conf::parsed>
			reg_conf::parse_packet(api::blob_span packet){
			auto result = api::optional<reg_conf::parsed>{};
			auto regconf_hdr = reinterpret_cast<reg_conf*>(packet.data());
			
			if (std::uint32_t header_len = regconf_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(reg_conf); 
				regconf_hdr->the_role == role::reg_conf &&
				header_len >= sizeof(reg_conf) &&
				header_len <= packet.size()){
				
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*regconf_hdr);
				
				if (packet.size() >= header_len + sizeof(member_id)){
					const auto count = (packet.size() - header_len) / sizeof(member_id);
					const auto member_ids = reinterpret_cast<member_id*>(packet.data() + header_len);
					result->receiver_ids = api::basic_string_view<member_id>{
						member_ids, count};
				}
				// ToDo: support parse valid extensions
			}
			return result;
		}
		
		file_info::parsed::parsed(const file_info& hdr) : main(hdr) {}
		
		api::optional<file_info::parsed>
			file_info::parse_packet(api::blob_span packet){
			auto result = api::optional<file_info::parsed>{};
			auto finfo_hdr = reinterpret_cast<file_info*>(packet.data());
			
			if (std::uint32_t header_len = finfo_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(file_info);
				finfo_hdr->the_role == role::file_info &&
				header_len >= sizeof(file_info) &&
				header_len <= packet.size() and
				finfo_hdr->name_length > 0){
					
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*finfo_hdr);
				result->name = api::basic_string_view{ reinterpret_cast<char*>(finfo_hdr) + sizeof(file_info),
					finfo_hdr->name_length * message::header_length_unit};
				if (finfo_hdr->link_length > 0)
					result->link = api::basic_string_view{ reinterpret_cast<char*>(finfo_hdr) + sizeof(file_info) + finfo_hdr->name_length * header_length_unit,
						finfo_hdr->link_length * message::header_length_unit };
				
				boost::endian::big_to_native_inplace(finfo_hdr->id);
				boost::endian::big_to_native_inplace(finfo_hdr->size_high_word);
				boost::endian::big_to_native_inplace(finfo_hdr->size_low_dword);
				boost::endian::big_to_native_inplace(finfo_hdr->timestamp_high);
				boost::endian::big_to_native_inplace(finfo_hdr->timestamp_low);
				boost::endian::big_to_native_inplace(finfo_hdr->msg_timestamp_usecs_high);
				boost::endian::big_to_native_inplace(finfo_hdr->msg_timestamp_usecs_low);
				
				if (packet.size() >= header_len + sizeof(member_id)){
					const auto count = (packet.size() - header_len) / sizeof(member_id);
					const auto member_ids = reinterpret_cast<member_id*>(packet.data() + header_len);
					result->receiver_ids = api::basic_string_view<member_id>{
						member_ids, count};
					if (ext_length > 0){
						// FIX-ME: currently ignore all other extensions(may be valid??)
						// we should factor out the header extensions parsing 
						auto ext_type = static_cast<extension::code>((packet.data() + sizeof(file_info))[0]);
						switch(ext_type){
						case extension::code::file_hash:
							if (ext_length >= sizeof(extension::file_hash)){
								auto fh = reinterpret_cast<extension::file_hash*>(packet.data() + sizeof(file_info));
								result->content_hash.emplace(fh->sha1_hash, 20);
							}
							break;
						default:
							break;
						}
					}
				}
			}
			return result;
		}
		
		void file_info::make_transfer_ready(){
			boost::endian::native_to_big_inplace(id);
			boost::endian::native_to_big_inplace(size_high_word);
			boost::endian::native_to_big_inplace(size_low_dword);
			boost::endian::native_to_big_inplace(timestamp_high);
			boost::endian::big_to_native_inplace(timestamp_low);
			boost::endian::native_to_big_inplace(msg_timestamp_usecs_high);
			boost::endian::native_to_big_inplace(msg_timestamp_usecs_low);
		}
		
		void file_info::set_file_timestamp(std::uint64_t secs_since_epoch){
			timestamp_high = static_cast<std::uint16_t>((secs_since_epoch >> 32) & 0xffff);
			timestamp_low = static_cast<std::uint32_t>(secs_since_epoch & 0xffffffff);
		}
		
		file_info_ack::parsed::parsed(const file_info_ack& hdr) : main(hdr) {}
		
		api::optional<file_info_ack::parsed>
			file_info_ack::parse_packet(api::blob_span packet){
			auto result = api::optional<file_info_ack::parsed>{};
			auto finfo_ack_hdr = reinterpret_cast<file_info_ack*>(packet.data());
			
			if (std::uint32_t header_len = finfo_ack_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(file_info_ack);
				finfo_ack_hdr->the_role == role::file_info_ack &&
				header_len >= sizeof(file_info_ack) &&
				header_len <= packet.size()){
					
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*finfo_ack_hdr);
				
				boost::endian::big_to_native_inplace(finfo_ack_hdr->id);
				
				boost::endian::big_to_native_inplace(finfo_ack_hdr->msg_timestamp_usecs_high);
				boost::endian::big_to_native_inplace(finfo_ack_hdr->msg_timestamp_usecs_low);
				
				if (packet.size() >= header_len + sizeof(member_id)){
					const auto count = (packet.size() - header_len) / sizeof(member_id);
					const auto member_ids = reinterpret_cast<member_id*>(packet.data() + header_len);
					result->receiver_ids = api::basic_string_view<member_id>{
						member_ids, count};
				}
			}
			return result;
		}
		
		void file_info_ack::make_transfer_ready(){
			boost::endian::native_to_big_inplace(id);
			boost::endian::native_to_big_inplace(msg_timestamp_usecs_high);
			boost::endian::native_to_big_inplace(msg_timestamp_usecs_low);
		}
		
		file_seg::parsed::parsed(const file_seg& hdr) : main(hdr) {}
		
		api::optional<file_seg::parsed> 
			file_seg::parse_packet(api::blob_span packet){
			auto result = api::optional<file_seg::parsed>{};
			auto fseg_hdr = reinterpret_cast<file_seg*>(packet.data());
			
			if (std::uint32_t header_len = fseg_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(file_seg);
				fseg_hdr->the_role == role::file_seg &&
				header_len >= sizeof(file_seg) &&
				header_len <= packet.size()){
					
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*fseg_hdr);
				
				boost::endian::big_to_native_inplace(fseg_hdr->file_id);
				boost::endian::big_to_native_inplace(fseg_hdr->section_idx);
				boost::endian::big_to_native_inplace(fseg_hdr->block_idx);
				
				if (packet.size() > header_len)
					result->data_blob = api::blob_view{packet.data() + header_len, 
						static_cast<std::uint32_t>(packet.size()) - header_len};
			}
			return result;
		}
		
		void file_seg::make_transfer_ready(){
			boost::endian::native_to_big_inplace(file_id);
			boost::endian::native_to_big_inplace(section_idx);
			boost::endian::native_to_big_inplace(block_idx);
		}
		
		done::parsed::parsed(const done& hdr) : main(hdr) {}
		
		api::optional<done::parsed>
			done::parse_packet(api::blob_span packet){
			auto result = api::optional<done::parsed>{};
			auto done_hdr = reinterpret_cast<done*>(packet.data());
			
			if (std::uint32_t header_len = done_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(done);
				done_hdr->the_role == role::done &&
				header_len >= sizeof(done) &&
				header_len <= packet.size()){
					
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*done_hdr);
				
				boost::endian::big_to_native_inplace(done_hdr->file_id);
				boost::endian::big_to_native_inplace(done_hdr->section_idx);
				
				if (packet.size() >= header_len + sizeof(member_id)){
					const auto count = (packet.size() - header_len) / sizeof(member_id);
					const auto member_ids = reinterpret_cast<member_id*>(packet.data() + header_len);
					result->receiver_ids = api::basic_string_view<member_id>{
						member_ids, count};
				}
			}
			return result;
		}
		
		void done::make_transfer_ready(){
			boost::endian::native_to_big_inplace(file_id);
			boost::endian::native_to_big_inplace(section_idx);
		}
		
		status::parsed::parsed(const status& hdr) : main(hdr) {}
		
		api::optional<status::parsed>
			status::parse_packet(api::blob_span packet){
			auto result = api::optional<status::parsed>{};
			auto status_hdr = reinterpret_cast<status *>(packet.data());
			
			if (std::uint32_t header_len = status_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(status);
				status_hdr->the_role == role::status &&
				header_len >= sizeof(status) &&
				header_len <= packet.size()){
					
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*status_hdr);
				
				boost::endian::big_to_native_inplace(status_hdr->file_id);
				boost::endian::big_to_native_inplace(status_hdr->section_idx);
				
				if (packet.size() >= header_len){
					result->nak_map = api::blob_view{
						packet.data() + header_len, static_cast<std::uint32_t>(packet.size()) - header_len};
				}
			}
			return result;
		}
		
		void status::make_transfer_ready(){
			boost::endian::native_to_big_inplace(file_id);
			boost::endian::native_to_big_inplace(section_idx);
		}
		
		complete::parsed::parsed(const complete& hdr) : main(hdr) {}
		
		api::optional<complete::parsed>
			complete::parse_packet(api::blob_span packet){
			auto result = api::optional<complete::parsed>{};
			auto complete_hdr = reinterpret_cast<complete*>(packet.data());
			
			if (std::uint32_t header_len = complete_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(complete);
				complete_hdr->the_role == role::complete &&
				header_len >= sizeof(complete) &&
				header_len <= packet.size()){
					
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*complete_hdr);
				
				boost::endian::big_to_native_inplace(complete_hdr->file_id);
				
				if (packet.size() >= header_len + sizeof(member_id)){
					const auto count = (packet.size() - header_len) / sizeof(member_id);
					const auto member_ids = reinterpret_cast<member_id*>(packet.data() + header_len);
					result->receiver_ids = api::basic_string_view<member_id>{
						member_ids, count};
				}
			}
			return result;
		}
		
		void complete::make_transfer_ready(){
			boost::endian::native_to_big_inplace(file_id);
		}
		
		abort::parsed::parsed(const abort& hdr) : main(hdr) {}
		
		api::optional<abort::parsed>
			abort::parse_packet(api::blob_span packet){
			auto result = api::optional<abort::parsed>{};
			auto abort_hdr = reinterpret_cast<abort*>(packet.data());
			
			if (std::uint32_t header_len = abort_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(abort);
				abort_hdr->the_role == role::abort &&
				header_len >= sizeof(abort) &&
				header_len <= packet.size()){
					
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*abort_hdr);
				
				//boost::endian::big_to_native_inplace(abort_hdr->host);
			}
			return result;
		}
		
		/*
		void abort::make_transfer_ready(){
			boost::endian::native_to_big_inplace(host);
		}
		*/
		
		done_conf::parsed::parsed(const done_conf& hdr) : main(hdr) {}
		
		api::optional<done_conf::parsed>
			done_conf::parse_packet(api::blob_span packet){
			auto result = api::optional<done_conf::parsed>{};
			auto done_conf_hdr = reinterpret_cast<done_conf*>(packet.data());
			
			if (std::uint32_t header_len = done_conf_hdr->header_length * header_length_unit,
				ext_length = header_len - sizeof(done_conf);
				done_conf_hdr->the_role == role::done_conf &&
				header_len >= sizeof(done_conf) &&
				header_len <= packet.size()){
					
				// ToDo: for the sake of security, add more message validations here
				result.emplace(*done_conf_hdr);
				
				if (packet.size() >= header_len + sizeof(member_id)){
					const auto count = (packet.size() - header_len) / sizeof(member_id);
					const auto member_ids = reinterpret_cast<member_id*>(packet.data() + header_len);
					result->receiver_ids = api::basic_string_view<member_id>{
						member_ids, count};
				}
			}
			return result;
		}
		
		
	}
}