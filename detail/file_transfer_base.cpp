#include "detail/file_transfer_base.hpp"
#include <iostream>

namespace ya_uftp{
	namespace detail{
		file_transfer_base::file_transfer_base() = default;
		void file_transfer_base::on_file_size_learned(const std::uintmax_t file_size,
			const std::uint16_t block_size, const std::uint16_t max_block_count_per_section){
			m_file_size = file_size;
			m_block_count = m_file_size / block_size + (m_file_size % block_size ? 1 : 0);
			m_section_count = m_block_count / max_block_count_per_section + (m_block_count % max_block_count_per_section ? 1 : 0);
			m_per_small_section_block_count = m_block_count / m_section_count;
			m_per_big_section_block_count = m_per_small_section_block_count + 
				(m_block_count % m_section_count ? 1 : 0); 
			m_big_section_count = m_block_count - (m_per_small_section_block_count * m_section_count);
			
			/*
			std::cout << "File size is " << file_size << ", block size is " << block_size << 
				"\nblock count is " << m_block_count << ", section count is " << m_section_count <<
				"\nper small section block count is " << m_per_small_section_block_count << 
				"\nper big section block count is " << m_per_big_section_block_count << 
				"\nbig section count is " << m_big_section_count << '\n';
			*/
		}
		
		std::pair<message::section_index, message::block_index> 
			file_transfer_base::abs_block_idx_to_sect_blk(std::uintmax_t block_idx){
			if (block_idx >= m_per_big_section_block_count * m_big_section_count){
				return { (block_idx - m_big_section_count * m_per_big_section_block_count) / 
					m_per_small_section_block_count + m_big_section_count,
				(block_idx - m_big_section_count * m_per_big_section_block_count) %
				m_per_small_section_block_count};
			}
			else{
				return {block_idx / m_per_big_section_block_count,
				block_idx % m_per_big_section_block_count};
			}	
		}
		
		std::uintmax_t file_transfer_base::sect_blk_to_abs_block_idx(message::section_index sect_idx, message::block_index block_idx){
			if (sect_idx >= m_big_section_count){
				return ((static_cast<std::uintmax_t>(sect_idx) - m_big_section_count) * m_per_small_section_block_count + block_idx) +
						m_big_section_count * m_per_big_section_block_count;
			}
			else
				return static_cast<std::uintmax_t>(sect_idx) * m_per_big_section_block_count + block_idx;
		}
		message::block_index file_transfer_base::section_block_count(message::section_index sect_idx){
			if (sect_idx >= m_big_section_count)
				return m_per_small_section_block_count;
			else
				return m_per_big_section_block_count;
		}
	}
}

