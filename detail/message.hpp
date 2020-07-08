#pragma once
#ifndef YA_UFTP_DETAIL_MESSAGE_HPP_
#define YA_UFTP_DETAIL_MESSAGE_HPP_

#include "utilities/network_intf.hpp"
#include "api_binder.hpp"
#include <set>
#include <limits>

namespace ya_uftp{
	namespace message{
		
		// change to compatible with 5.0 protocol
		using member_id = std::uint32_t;
		// consider whether we should support more than 65535 files in one session
		// if we do, we must re-layout the fields
		using file_id_type = std::uint16_t;
		using section_index = std::uint16_t;
		using block_index = std::uint16_t;
		
		constexpr std::uint8_t magic = 0x50;
		constexpr auto max_grtt = 1e3;
		constexpr auto min_grtt = 1e-6;
		constexpr std::uint32_t header_length_unit = 4u;
		constexpr auto max_section_count = std::numeric_limits<section_index>::max();
		constexpr auto max_block_count_per_section = std::numeric_limits<block_index>::max();

		constexpr auto rnd_size = 32u;
		
		enum class role : std::uint8_t {
			announce = 1,
			receiver_register = 2, // REGISTER in protocol, to avoid conflict with the reserved register keyword
			client_key = 3,
			reg_conf = 4,
			key_info = 5,
			key_info_ack = 6,
			file_info = 7,
			file_info_ack = 8,
			file_seg = 9,
			done = 10,
			status = 11,
			complete = 12,
			done_conf = 13,
			hb_req = 14,
			hb_resp = 15,
			key_req = 16,
			proxy_key = 17,
			encrypted = 18,
			abort = 19,
			cong_ctrl = 20,
			cc_ack = 21,
			// New addiction to let client indicated the transfering file is already up-to-date
			file_up_to_date = 22,
			invalid
		};
		
		enum class hash_type : std::uint8_t{
			none = 0,
			md5 = 1,
			sha1 = 2,
			sha256 = 3,
			sha384 = 4,
			sha512 = 5
		};
		
		namespace extension{
			enum class code : std::uint8_t{
				encryption_info	=	1,
				tfmcc_data_info	=	2,
				tfmcc_ack_info	=	3,
				pgmcc_data_info	=	4,
				pgmcc_nak_info	=	5,
				pgmcc_ack_info	=	6,
				freespace_info	=	7,
				file_hash		=	8
			};
			
			struct file_hash{
				const code		the_code = code::file_hash;
				std::uint8_t	reserved0 = 0u;
				std::uint16_t	reserved1 = 0u;
				std::uint8_t	sha1_hash[20]; 
			};
		}
		
		enum class congestion_control_mode : std::uint8_t{
			none	= 0,
			uftp3	= 1,
			tfmcc	= 2,
			pgmcc	= 3
		};
		
		unsigned char quantize_grtt(double grtt);
		
		double dequantize_grtt(unsigned char qrtt);
		
		std::uint8_t quantize_group_size(std::uint32_t group_size);
		
		std::uint32_t dequantize_group_size(std::uint8_t q_group_size);
		
		void set_timestamp(std::uint32_t& ts_high, std::uint32_t& ts_low);
		
		std::chrono::microseconds calculate_rtt(std::uint32_t ts_high, std::uint32_t ts_low);
		
		// Remark One: all the message(header indeed) struct do not utilize explicit constructors to initialize.
		// simply because there are way too many fields in one header and c++ does not yet support named argument in 
		// constructor/function-call, we prioritize clean code over best practices.
		// Note: we do use default value initializer for the reserved fields, which can help introduce cleaner codes.
		
		// Remark Two: 
		#pragma pack(push, 1)
		struct protocol_header{
			const std::uint8_t head_magic = magic;
			role 			message_role;
			std::uint16_t	sequence_number;
			member_id		source_id;
			std::uint32_t	session_id;
			std::uint8_t	group_instance;
			std::uint8_t	grtt;
			std::uint8_t	group_size;
			std::uint8_t	reserved = 0u;
		};
		
		struct announce {
			struct v4_multicast_addr{
				struct in_addr	public_one;
				struct in_addr	private_one;
			};
			
			struct v6_multicast_addr{
				struct in6_addr	public_one;
				struct in6_addr	private_one;
			};
			
			const role		the_role = role::announce;
			std::uint8_t	header_length;
			std::uint8_t	sync_mode : 1;
			std::uint8_t	sync_preview : 1;
			std::uint8_t	ipv6 : 1;
			//std::uint8_t	reserved0 : 5 = 0u;
			std::uint8_t	reserved0 : 5;
			std::uint8_t	robust_factor;
			congestion_control_mode	cc_type;
			std::uint8_t	reserved1 = 0u;
			std::uint16_t	block_size;
			std::uint32_t	msg_timestamp_usecs_high;
			std::uint32_t	msg_timestamp_usecs_low;
			struct parsed{
				const announce& 										main;
				api::variant<std::reference_wrapper<const v4_multicast_addr>, 
					std::reference_wrapper<const v6_multicast_addr>>	mcast_addrs;
				api::basic_string_view<member_id>						allowed_clients;
				parsed(const announce& hdr, api::variant<std::reference_wrapper<const v4_multicast_addr>, 
					std::reference_wrapper<const v6_multicast_addr>> mcast);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			void make_transfer_ready();
		};
		
		struct receiver_register{
			const role		the_role = role::receiver_register;
			std::uint8_t	header_length;
			std::uint16_t	ecdh_key_length;
			std::uint32_t	msg_timestamp_usecs_high;
			std::uint32_t	msg_timestamp_usecs_low;
			std::uint8_t	client_rnd_number[rnd_size];
			struct parsed{
				const receiver_register&				main;
				api::blob_view							key_info;
				api::basic_string_view<member_id>		receiver_ids;
				parsed(const receiver_register& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			void make_transfer_ready();
		};
		
		struct reg_conf{
			const role	the_role = role::reg_conf;
			std::uint8_t	header_length;
			std::uint16_t	reserved = 0u;
			struct parsed{
				const reg_conf&						main;
				api::basic_string_view<member_id>	receiver_ids;
				parsed(const reg_conf& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
		};
		
		struct file_info{
			const role	the_role = role::file_info;
			std::uint8_t	header_length;
			file_id_type	id;
			enum class subtype : std::uint8_t{
				regular_file	= 0,
				directory		= 1,
				symbolic_link	= 2,
				delete_req		= 3,
				get_free_space	= 4
			};
			subtype			type;
			//std::uint32_t	reserved : 24 = 0u;
			std::uint8_t	reserved = 0u;
			std::uint16_t	timestamp_high;
			std::uint8_t	name_length;
			std::uint8_t	link_length;
			std::uint16_t	size_high_word;
			std::uint32_t	size_low_dword;
			std::uint32_t	timestamp_low;
			std::uint32_t	msg_timestamp_usecs_high;
			std::uint32_t	msg_timestamp_usecs_low;
			
			struct parsed{
				const file_info&							main;
				api::basic_string_view<member_id>			receiver_ids;
				api::optional<api::blob_view>				content_hash;
				api::basic_string_view<char>				name;
				api::basic_string_view<char>				link;
				parsed(const file_info& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			void make_transfer_ready();
			void set_file_timestamp(std::uint64_t secs_since_epoch);
		};
		
		struct file_info_ack{
			const role	the_role = role::file_info_ack;
			std::uint8_t	header_length;
			file_id_type	id;
			std::uint8_t	partial_received : 1;
			// New addiction to let client indicate the file is up-to-date or directory is made.
			// yet in the pragmatic way to implement this feature, this flag might not be too useful since the client are encourage 
			// to compute the file hash asynchronously to minimize the latency.
			// for clients with tracking database for files properties or really fast hash computation power, 
			// this flag might be a better fit.
			std::uint8_t	done : 1;
			//std::uint8_t	reserved0 : 6 = 0u;
			std::uint8_t	reserved0 : 6 ;
			std::uint8_t	reserved1 = 0u;
			std::uint16_t	reserved2 = 0u;
			std::uint32_t	msg_timestamp_usecs_high;
			std::uint32_t	msg_timestamp_usecs_low;
			
			struct parsed{
				const file_info_ack&				main;
				api::basic_string_view<member_id>	receiver_ids;
				parsed(const file_info_ack& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			void make_transfer_ready();
		};
		
		struct file_seg{
			const role	the_role = role::file_seg;
			std::uint8_t	header_length;
			file_id_type	file_id;
			section_index	section_idx;
			block_index		block_idx;
			
			struct parsed{
				const file_seg&				main;
				api::blob_view				data_blob;
				parsed(const file_seg& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			void make_transfer_ready();
		};
		
		struct done{
			const role	the_role = role::done;
			std::uint8_t	header_length;
			file_id_type	file_id;
			section_index	section_idx;
			std::uint16_t	reserved = 0u;
			struct parsed {
				const done&							main;
				api::basic_string_view<member_id>	receiver_ids;
				parsed(const done& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			void make_transfer_ready();
		};
		
		struct status{
			const role		the_role = role::status;
			std::uint8_t	header_length;
			file_id_type	file_id;
			section_index	section_idx;
			std::uint16_t	reserved = 0u;
			
			struct parsed{
				const status&				main;
				api::blob_view				nak_map;
				parsed(const status& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			void make_transfer_ready();
		};
		
		struct complete{
			enum class sub_status : std::uint8_t{
				normal = 0,
				skipped = 1,
				overwrited = 2,
				rejected = 3
			};
			const role	the_role = role::complete;
			std::uint8_t	header_length;
			file_id_type	file_id;
			sub_status		detail_status;
			std::uint8_t	reserved0 = 0u;
			std::uint16_t	reserved1 = 0u;
			
			struct parsed {
				const complete&						main;
				api::basic_string_view<member_id>	receiver_ids;
				parsed(const complete& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			void make_transfer_ready();
		};
		
		struct abort{
			const role		the_role = role::abort;
			std::uint8_t	header_length;
			std::uint8_t	current_file : 1;
			std::uint8_t	reserved0 : 7;
			std::uint8_t	reserved1;
			std::uint32_t	host;
			char 			message[300];
			
			struct parsed {
				const abort&						main;
				parsed(const abort& hdr);
				// ToDo: support parsing valid extensions
			};
			static api::optional<parsed> parse_packet(api::blob_span packet);
			//void make_transfer_ready();
		};
		
		struct done_conf{
			const role 		the_role = role::done_conf;
			std::uint8_t	header_length;
			std::uint16_t	reserved = 0u;
			
			struct parsed {
				const done_conf&					main;
				api::basic_string_view<member_id>	receiver_ids;
				parsed(const done_conf& hdr);
				// ToDo: support parsing valid extensions
			};
			
			static api::optional<parsed> parse_packet(api::blob_span packet);
			//void make_transfer_ready();
		};
		
		struct file_up_to_date{
			const role	the_role = role::file_up_to_date;
			std::uint8_t	header_length;
			file_id_type	file_id;
		};
		
		#pragma pack(pop)
		
		struct validated_packet{
			const protocol_header&		msg_header;
			// this is the blob contain per message header + optional body
			api::blob_span				msg_body;
			validated_packet(const protocol_header&, api::blob_span body);
		};
		
		std::set<block_index> extract_lost_blocks_ids(const api::blob_view nak_map);
		api::optional<validated_packet> basic_validate_packet(api::blob_span packet);
	}
}
	
#endif