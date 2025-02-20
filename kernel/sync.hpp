#ifndef _MOS_SYNC_
#define _MOS_SYNC_

#include "task.hpp"

namespace MOS::Kernel::Sync
{
	using namespace Macro;
	using namespace Utils;
	using namespace Concepts;

	using DataType::TCB_t;
	using DataType::TcbList_t;

	using TcbPtr_t = TCB_t::TcbPtr_t;
	using Prior_t  = TCB_t::Prior_t;
	using Status   = TCB_t::Status;
	using Count_t  = Atomic_t<int32_t>;

	struct Sema_t
	{
		TcbList_t waiting_list;
		Count_t cnt;

		// The initial value must be set
		MOS_INLINE
		inline Sema_t() = delete;

		MOS_INLINE
		inline Sema_t(int32_t _cnt): cnt(_cnt) {}

		// `P-opr`
		MOS_NO_INLINE void
		down()
		{
			// Assert if irq disabled
			MOS_ASSERT(test_irq(), "Disabled Interrupt");
			IrqGuard_t guard;
			cnt -= 1;
			if (cnt < 0) {
				Task::block_to_raw(
				    Task::current(), waiting_list
				);
				return Task::yield();
			}
		}

		// `V-opr`
		void up()
		{
			// Assert if irq disabled
			MOS_ASSERT(test_irq(), "Disabled Interrupt");
			IrqGuard_t guard;
			up_raw();
			if (Task::any_higher()) {
				return Task::yield();
			}
		}

		// `V-opr` from ISR
		MOS_INLINE inline void
		up_from_isr() { up_raw(); }

	private:
		MOS_INLINE inline void
		up_raw()
		{
			if (cnt < 0) {
				Task::resume_raw(
				    waiting_list.begin(), waiting_list
				);
			}
			cnt += 1;
		}
	};

	struct Lock_t
	{
		Sema_t sema;
		TcbPtr_t owner;

		MOS_INLINE
		inline Lock_t(): owner(nullptr), sema(1) {}

		MOS_INLINE inline void
		acquire()
		{
			MOS_ASSERT(
			    owner != Task::current(),
			    "Non-recursive lock"
			);
			owner = Task::current();
			sema.down();
		}

		MOS_INLINE inline void
		release()
		{
			MOS_ASSERT(
			    owner == Task::current(),
			    "Lock can only be released by holder"
			);
			sema.up();
			owner = nullptr;
		}
	};

	struct MutexImpl_t
	{
		void lock() // P operation
		{
			MOS_ASSERT(test_irq(), "Disabled Interrupt");
			IrqGuard_t guard;
			auto cur = Task::current();

			if (owner == cur) {
				// If task already owns the lock, just increment the recursive count
				recursive += 1;
				return;
			}

			// Compare with ceiling
			if (pri_cmp(ceiling, cur->get_pri())) {
				// Temporarily raise the priority
				cur->store_pri(ceiling);
			}
			else {
				// If current pri is higher, set it as the new ceiling
				ceiling = cur->get_pri();

				// Raise the pri of all tasks
				if (owner != nullptr) {
					owner->store_pri(ceiling);
				}

				update_pri();
			}

			sema.cnt -= 1;

			if (sema.cnt < 0) {
				Task::block_to_raw(cur, sema.waiting_list);
				return Task::yield();
			}
			else {
				owner = cur;
				recursive += 1;
			}
		}

		void unlock() // V operation
		{
			MOS_ASSERT(test_irq(), "Disabled Interrupt");
			MOS_ASSERT(
			    owner == Task::current(),
			    "Lock can only be released by holder"
			);

			IrqGuard_t guard;
			recursive -= 1;

			if (recursive > 0) {
				// Still held by this owner
				return;
			}

			// Restore old_pri for owner
			owner->restore_pri();

			if (!sema.waiting_list.empty()) {
				auto old_ceiling = ceiling;
				find_ceiling();
				if (pri_cmp(old_ceiling, ceiling)) {
					update_pri();
				}

				auto tcb = sema.waiting_list.begin();
				Task::resume_raw(tcb, sema.waiting_list);

				owner = tcb;
				sema.cnt += 1;

				if (Task::any_higher()) {
					return Task::yield();
				}
			}
			else {
				// No owner if no tasks are waiting
				owner = nullptr;
				sema.cnt += 1;

				// Reset the ceiling to the lowest priority
				ceiling = PRI_MIN;

				return;
			}
		}

		MOS_INLINE inline auto
		hold(auto&& scope)
		{
			lock();
			scope();
			unlock();
		}

	private:
		Sema_t sema       = 1;
		Count_t recursive = 0;
		TcbPtr_t owner    = nullptr;
		Prior_t ceiling   = PRI_MIN;

		MOS_INLINE static inline bool
		pri_cmp(Prior_t lhs, Prior_t rhs)
		{
			return lhs < rhs;
		}

		MOS_INLINE inline void
		update_pri()
		{
			sema.waiting_list.iter_mut(
			    [&](TCB_t& tcb) {
				    tcb.store_pri(ceiling);
			    }
			);
		}

		MOS_INLINE inline void
		find_ceiling()
		{
			ceiling = PRI_MIN;
			sema.waiting_list.iter(
			    [&](const TCB_t& tcb) {
				    auto pri = tcb.get_pri();
				    if (pri_cmp(pri, ceiling)) {
					    ceiling = pri;
				    }
			    }
			);
		}
	};

	template <typename T = void>
	struct Mutex_t : private MutexImpl_t
	{
		using Raw_t    = T;
		using RawRef_t = Raw_t&;
		using MutexImpl_t::hold;

		struct MutexGuard_t
		{
			using Mtx_t = Mutex_t<Raw_t>;

			MOS_INLINE
			inline MutexGuard_t(Mtx_t& _mtx)
			    : mtx(_mtx) { mtx.MutexImpl_t::lock(); }

			MOS_INLINE
			inline ~MutexGuard_t() { mtx.unlock(); }

			// Raw Accessor
			MOS_INLINE inline RawRef_t
			get() { return mtx.raw; }

			MOS_INLINE inline RawRef_t
			operator*() { return get(); }

		private:
			Mtx_t& mtx;
		};

		MOS_INLINE
		inline Mutex_t(Raw_t _raw): raw(_raw) {}

		MOS_INLINE inline auto
		lock() { return MutexGuard_t {*this}; }

	private:
		Raw_t raw;
	};

	template <>
	struct Mutex_t<> : private MutexImpl_t
	{
		using MutexImpl_t::hold;

		struct MutexGuard_t
		{
			MOS_INLINE
			inline MutexGuard_t(Mutex_t& _mtx)
			    : mtx(_mtx) { mtx.MutexImpl_t::lock(); }

			MOS_INLINE
			inline ~MutexGuard_t() { mtx.unlock(); }

		private:
			Mutex_t& mtx;
		};

		MOS_INLINE inline Mutex_t() = default;

		MOS_INLINE inline auto
		lock() { return MutexGuard_t {*this}; }
	};

	// Template Deduction
	Mutex_t() -> Mutex_t<void>;

	template <typename T>
	Mutex_t(T&&) -> Mutex_t<T>;

	template <typename T>
	Mutex_t(T&) -> Mutex_t<T&>;

	struct CondVar_t
	{
		MOS_INLINE inline bool
		has_waiters() const
		{
			return !waiting_list.empty();
		}

		inline void
		wait(
		    MutexImpl_t& mtx,
		    Invocable<bool> auto&& pred
		)
		{
			mtx.unlock();     // Unlock first
			while (!pred()) { // Avoid false wakeup
				block_this();
			}
			mtx.lock();
		}

		inline void notify() // Signal
		{
			IrqGuard_t guard;
			if (has_waiters()) {
				wake_up();
			}
			return Task::yield();
		}

		inline void notify_all() // Broadcast
		{
			IrqGuard_t guard;
			while (has_waiters()) {
				wake_up();
			}
			return Task::yield();
		}

	private:
		TcbList_t waiting_list;

		MOS_INLINE inline void
		block_this()
		{
			IrqGuard_t guard;
			Task::block_to_raw(
			    Task::current(), waiting_list
			);
			return Task::yield();
		}

		MOS_INLINE inline void
		wake_up()
		{
			Task::resume_raw(
			    waiting_list.begin(), waiting_list
			);
		}
	};

	struct Barrier_t
	{
		MutexImpl_t mtx;
		CondVar_t cv;
		Count_t total, cnt = 0;

		MOS_INLINE
		inline Barrier_t(int32_t _total): total(_total) {}

		inline void wait()
		{
			mtx.hold([&] {
				cnt += 1;
				cv.wait(mtx, [&] { return cnt == total; });
				if (!cv.has_waiters()) {
					cnt = 0; // Reuse Barrier
				}
			});
			cv.notify_all();
		}
	};
}

#endif