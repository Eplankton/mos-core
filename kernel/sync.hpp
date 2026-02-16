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
	using Count_t  = volatile int32_t;

	struct Sema_t
	{
		TcbList_t waiting_list;
		Count_t cnt;

		// The initial value must be set
		MOS_INLINE
		inline Sema_t() = delete;

		MOS_INLINE
		inline Sema_t(int32_t _cnt): cnt(_cnt) {}

		// `P-opr` - Decrease/Wait
		MOS_NO_INLINE void
		down()
		{
			// Assert if irq is disabled (should be enabled before calling)
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

		// `V-opr` - Increase/Signal
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
		inline Lock_t(): sema(1), owner(nullptr) {}

		MOS_INLINE inline void
		acquire()
		{
			// Owner assignment must happen AFTER acquiring the semaphore.
			// Otherwise, if we block in down(), the owner is set to 'this' task,
			// but the lock is actually held by someone else.

			sema.down(); // Wait for the lock

			// Check for recursive locking (not supported here)
			MOS_ASSERT(
			    owner != Task::current(),
			    "Non-recursive lock"
			);

			owner = Task::current(); // Take ownership
		}

		MOS_INLINE inline void
		release()
		{
			MOS_ASSERT(
			    owner == Task::current(),
			    "Lock can only be released by holder"
			);

			// Clear owner BEFORE releasing the semaphore.
			owner = nullptr; // Drop ownership
			sema.up();       // Release the lock
		}
	};

	struct MutexImpl_t
	{
		// P operation (Acquire lock)
		void lock()
		{
			// Ensure interrupts are enabled before entering critical logic,
			// though this check acts more like a sanity check.
			MOS_ASSERT(test_irq(), "Disabled Interrupt");

			// Enter critical section to protect internal state
			IrqGuard_t guard;
			auto cur = Task::current();

			// Recursive Lock Check
			// If the current task already owns the lock, just increment the counter.
			if (owner == cur) {
				recursive += 1;
				return;
			}

			// Priority Inheritance Protocol (PIP) logic
			// If the lock is already held by another task (owner), check if we need to
			// boost the owner's priority to avoid priority inversion.
			if (owner != nullptr) {
				auto owner_pri = owner->get_pri();
				auto cur_pri   = cur->get_pri();

				// In MOS, a smaller priority value means higher priority.
				// If current task has higher priority than the owner:
				if (pri_cmp(cur_pri, owner_pri)) {
					// Temporarily boost the owner's priority to the current task's level.
					// Note: You must ensure TCB_t::store_pri() handles this correctly
					// (i.e., only update if new_pri < old_pri).
					owner->store_pri(cur_pri);
				}
			}

			// Semaphore Wait (Dijkstra's Algorithm)
			sema.cnt -= 1;

			if (sema.cnt < 0) {
				// Lock is contended. Block the current task.
				// Note: The task is added to waiting_list but 'owner' remains unchanged
				// until the current owner releases it.
				Task::block_to_raw(cur, sema.waiting_list);
				return Task::yield();
			}
			else {
				// Lock acquired successfully (uncontended).
				owner     = cur;
				recursive = 1;
			}
		}

		// V operation (Release lock)
		void unlock()
		{
			MOS_ASSERT(test_irq(), "Disabled Interrupt");
			MOS_ASSERT(
			    owner == Task::current(),
			    "Lock can only be released by holder"
			);

			IrqGuard_t guard;
			recursive -= 1;

			// If it's a recursive lock and not fully released yet, just return.
			if (recursive > 0) {
				return;
			}

			// Restore Priority
			// When releasing the lock, the owner returns to its original priority.
			// (Note: This simple logic assumes non-nested PIP. Nested locks require
			// a stack of priorities in TCB, but this suffices for basic usage).
			owner->restore_pri();
			owner = nullptr; // Drop Ownership

			// Wake up waiters (if any)
			if (!sema.waiting_list.empty()) {
				// Pick the highest priority task from the sorted waiting list
				auto tcb = sema.waiting_list.begin();
				Task::resume_raw(tcb, sema.waiting_list);

				// Handoff Ownership Immediately:
				// Directly assign the lock to the woken task. This prevents a race
				// condition where a new medium-priority task could steal the lock
				// before the woken high-priority task gets to run.
				owner     = tcb;
				recursive = 1; // Initialize recursive count for the new owner

				// Semaphore logic:
				// Current State: cnt < 0 (waiters exist).
				// We wake one up, so we theoretically increment cnt.
				// Logic: -1 (1 waiter) -> 0 (lock held by new owner, 0 waiters).
				sema.cnt += 1;

				// If the woken task has a higher priority than the current task
				// (which is likely if we just did priority inheritance), yield now.
				if (Task::any_higher()) {
					return Task::yield();
				}
			}
			else {
				// No waiters. Just release the semaphore resource.
				// Logic: 0 (held) -> 1 (free).
				sema.cnt += 1;
			}
		}

		// Helper for RAII-style locking
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

		// Helper to compare priorities.
		// Returns true if lhs has higher priority (smaller value) than rhs.
		MOS_INLINE static inline bool
		pri_cmp(Prior_t lhs, Prior_t rhs)
		{
			return lhs < rhs;
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
			// Note: There is a small window between unlock and block_this where
			// an interrupt could occur, potentially leading to a lost wakeup if
			// notify() is called in that window.
			// However, since `block_to_raw` disables interrupts internally via
			// IrqGuard logic (in Task::block_to), the critical path is mostly protected.
			// Ideally, unlock and block should be atomic.

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
		const Count_t total;      // Total threads to wait for
		Count_t cnt = 0, gen = 0; // Current count and Generation counter to prevent deadlocks on reuse

		MOS_INLINE
		inline Barrier_t(int32_t _total): total(_total) {}

		inline void wait()
		{
			mtx.hold([&] {
				// Capture current generation
				auto my_gen = gen;
				cnt += 1;

				if (cnt == total) {
					// Last one arrived
					cnt = 0;  // Reset count
					gen += 1; // Next generation
					cv.notify_all();
				}
				else {
					// Wait until generation changes
					cv.wait(mtx, [&] { return gen != my_gen; });
				}
			});
		}
	};
}

#endif