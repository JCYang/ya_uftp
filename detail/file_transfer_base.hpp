#pragma once
#ifndef YA_UFTP_DETAIL_FILE_TRANSFER_BASE_HPP_
#define YA_UFTP_DETAIL_FILE_TRANSFER_BASE_HPP_

#include "detail/message.hpp"

namespace ya_uftp{
	namespace detail{
		class file_transfer_base{
		protected:
			std::uintmax_t									m_file_size = 0u;
			std::uintmax_t									m_block_count = 0u;
			message::section_index							m_section_count = 0u;
			message::block_index							m_per_small_section_block_count = 0u;
			message::block_index							m_per_big_section_block_count = 0u;
			message::section_index							m_big_section_count = 0u;
			
			file_transfer_base();
			void on_file_size_learned(const std::uintmax_t file_size, 
				const std::uint16_t block_size, const std::uint16_t max_block_count_per_section);

			[[nodiscard]] std::pair<message::section_index, message::block_index> abs_block_idx_to_sect_blk(std::uintmax_t block_idx);
			[[nodiscard]] std::uintmax_t sect_blk_to_abs_block_idx(message::section_index sect_idx, message::block_index block_idx);
			message::block_index section_block_count(message::section_index sect_idx);
		};
	}
}
#endif
