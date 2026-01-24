#ifndef _MOS_ALLOC_
#define _MOS_ALLOC_

// #include <stdlib.h>
#include "global.hpp"

namespace MOS::Kernel::Alloc
{
	using Utils::IrqGuard_t;
	using Page_t     = DataType::Page_t;
	using PageRaw_t  = Page_t::Raw_t;
	using PageSize_t = Page_t::Size_t;

	using enum Page_t::Policy;

	// Page Allocator
	inline PageRaw_t // -1(0xFFFFFFFF) as invalid
	palloc(Page_t::Policy policy, PageSize_t page_size = -1)
	{
		IrqGuard_t guard;
		switch (policy) {
			case POOL: {
				using Global::page_pool;

				// Whether a page is used or not
				auto unused = [](PageRaw_t raw) {
					auto ptr = (void*) raw[0]; // ptr = tcb.link.prev
					return (ptr == nullptr) || // Uninit -> never allocated
					       (ptr == raw);       // Deinited -> tcb.link is self-linked, used once and recycled
				};

				for (const auto raw: page_pool) {
					if (unused(raw)) {
						return raw;
					}
				}

				return nullptr;
			}

			case DYNAMIC: {
				MOS_ASSERT(page_size != -1U, "Page Size Error");
				return new uint32_t[page_size];
			}

			default:
				return nullptr;
		}
	}
}

#endif