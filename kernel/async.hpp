#ifndef _MOS_ASYNC_
#define _MOS_ASYNC_

// Use C++'s Coroutine STL Infrastructure
#include <coroutine>
#include "task.hpp"

// Import ETL Library
#include "etl/vector.h"
#include "etl/priority_queue.h"

// Whether to use pool allocator for async
#if (MOS_CONF_ASYNC_USE_POOL == true)
#include "etl/pool.h"
#endif

// -------------------------- MultiMap vs Priority Queue ----------------------------
// ┌──────────────────┬───────────────────────┬───────────────────────┬─────────────┐
// │ Feature          │ MultiMap              │ Priority Queue        │ Winner      │
// ├──────────────────┼───────────────────────┼───────────────────────┼─────────────┤
// │ RAM Overhead     │ High                  │ Lowest                │             │
// │ (Memory)         │ (Requires node ptrs)  │ (Flat array/vector)   │ PQ Better   │
// ├──────────────────┼───────────────────────┼───────────────────────┼─────────────┤
// │ Cache Locality   │ Poor                  │ Excellent             │             │
// │ (If have cache)  │ (Nodes scattered)     │ (Contiguous memory)   │ PQ Better   │
// ├──────────────────┼───────────────────────┼───────────────────────┼─────────────┤
// │ Insertion Speed  │ Slower                │ Faster                │             │
// │ (Performance)    │ (Tree rebalancing)    │ (Array swapping)      │ PQ Better   │
// ├──────────────────┼───────────────────────┼───────────────────────┼─────────────┤
// │ Stability        │ Stable                │ Unstable              │             │
// │ (Same Timestamp) │ (Strict FIFO)         │ (Order not guaranteed)│ Mmap Better │
// ├──────────────────┼───────────────────────┼───────────────────────┼─────────────┤
// │ Traversal        │ Full Access           │ Top-Only              │             │
// │ (Inspection)     │ (Can iterate all)     │ (Can only see next)   │ Mmap Better │
// ├──────────────────┼───────────────────────┼───────────────────────┼─────────────┤
// │ OVERALL VERDICT  │ Good                  │ BEST                  │             │
// │ (For Scheduler)  │ (If FIFO is critical) │ (For max performance) │ PQ Better   │
// └──────────────────┴───────────────────────┴───────────────────────┴─────────────┘

namespace MOS::Kernel::Async
{
	using namespace Macro;
	using Task::Tick_t;
	using Utils::IrqGuard_t;

	template <typename P = void>
	using CoroHandle_t = std::coroutine_handle<P>;

	// ==========================================================
	// Custom FixedFunction
	// Replaces std::function, stores Lambda in a fixed buffer.
	// Supports Copy/Move semantics, compatible with ETL containers.
	// ==========================================================
	// Added 'noexcept' to reduce stack overhead for exception handling.
	// Explicit memory alignment added for cross-platform stability.
	template <size_t MAX_SIZE>
	class FixedFn_t
	{
	public:
		using Invoker_t = void (*)(void*);
		using Cloner_t  = void (*)(void* dest, const void* src);
		using Buffer_t  = char[MAX_SIZE];

		FixedFn_t() noexcept: invoker(nullptr), cloner(nullptr) {}

		template <typename F>
		FixedFn_t(F f) noexcept
		{
			static_assert(sizeof(F) <= MAX_SIZE, "Lambda too large for Async!");
			new (buffer) F(std::move(f)); // Use placement new to construct object
			invoker = [](void* data) { (*reinterpret_cast<F*>(data))(); };

			// Cloner is only generated if copying is needed; most async tasks only require move.
			cloner = [](void* dest, const void* src) { new (dest) F(*reinterpret_cast<const F*>(src)); };
		}

		FixedFn_t(const FixedFn_t& other) noexcept { copy_from(other); }
		FixedFn_t(FixedFn_t&& other) noexcept { move_from(std::move(other)); }

		FixedFn_t& operator=(const FixedFn_t& other) noexcept
		{
			if (this != &other) copy_from(other);
			return *this;
		}

		FixedFn_t& operator=(FixedFn_t&& other) noexcept
		{
			if (this != &other) move_from(std::move(other));
			return *this;
		}

		explicit operator bool() const { return invoker != nullptr; }

		void operator()()
		{
			if (invoker) invoker(buffer);
		}

	private:
		Buffer_t buffer MOS_DEFAULT_ALIGN;
		Invoker_t invoker;
		Cloner_t cloner;

		void copy_from(const FixedFn_t& other)
		{
			if (other.invoker) {
				other.cloner(buffer, other.buffer);
				invoker = other.invoker;
				cloner  = other.cloner;
			}
			else {
				invoker = nullptr;
				cloner  = nullptr;
			}
		}

		void move_from(FixedFn_t&& other)
		{
			if (other.invoker) {
				// Use memcpy for POD data movement, which is faster than cloner
				Utils::memcpy(buffer, other.buffer, MAX_SIZE);
				invoker       = other.invoker;
				cloner        = other.cloner;
				other.invoker = nullptr;
				other.cloner  = nullptr;
			}
			else {
				invoker = nullptr;
				cloner  = nullptr;
			}
		}
	};

	using Lambda_t = FixedFn_t<ASYNC_TASK_SIZE>;

	// ==========================================================
	// Static Executor Implementation
	// ==========================================================
	// Fully static, uses double-buffering to avoid queue copying.
	struct Executor
	{
		static void get() // Run only once for init
		{
			static bool init_flag = false;
			if (!init_flag) {
				auto async_exec = [] {
					while (true) {
						if (!poll()) {
							Task::yield(); // Yield if idle
						}
					}
				};

				// Create a single task for Executor
				if (Task::create(async_exec, nullptr, (PRI_MIN / 2), "async/exec")) {
					init_flag = true;
				}
				else {
					MOS_ASSERT(false, "Async Spawn Failed!");
				}
			}
		}

		static bool poll()
		{
			clean_sleepers(); // Process the sleeping queue

			// Double Buffering Switch (Ping-Pong)
			// Get the inverse index of the current write buffer (i.e., the one we will now read).
			// Disable interrupts during the switch to prevent 'post' from writing simultaneously.
			uint8_t read_idx;
			{
				IrqGuard_t guard;
				if (task_buffers[write_idx].empty()) return false; // No tasks submitted

				read_idx  = write_idx;  // Prepare to read current accumulated tasks
				write_idx = !write_idx; // Switch write index to the other (empty) buffer
			}

			// Execute tasks
			auto& run_list = task_buffers[read_idx];
			for (auto& task: run_list) {
				if (task) task();
			}

			run_list.clear(); // Clear buffer for its next turn as the write buffer
			return true;
		}

		static void post(Lambda_t fn)
		{
			IrqGuard_t guard;

			// Try to load into buffers, with Happy Path -> O(1)
			if (!task_buffers[write_idx].full()) {
				task_buffers[write_idx].push_back(std::move(fn));
			}
			else {
				MOS_ASSERT(false, "Async Queue Full!");
			}
		}

		static void add_sleeper(uint32_t ms, Lambda_t fn)
		{
			IrqGuard_t guard;
			if (!sleepers.full()) {
				sleepers.push(Sleeper_t {os_ticks + ms, std::move(fn)});
			}
			else {
				MOS_ASSERT(false, "Async Sleeper Full!");
			}
		}

	private:
		struct Sleeper_t
		{
			uint32_t wake_tick;
			Lambda_t task;

			struct Compare
			{
				bool operator()(const Sleeper_t& lhs, const Sleeper_t& rhs) const
				{
					return lhs.wake_tick > rhs.wake_tick;
				}
			};
		};

		// Data structure definitions
		using TaskBuffer_t  = etl::vector<Lambda_t, ASYNC_TASK_MAX>;
		using SleepBuffer_t = etl::priority_queue<
		    Sleeper_t,
		    ASYNC_TASK_MAX,
		    etl::vector<Sleeper_t, ASYNC_TASK_MAX>,
		    Sleeper_t::Compare>;

		// Static members: Stored in .bss/.data segments.
		static inline TaskBuffer_t task_buffers[2]; // Double buffers: Index 0 and 1
		static inline volatile uint8_t write_idx = 0;
		static inline SleepBuffer_t sleepers;

		static void clean_sleepers()
		{
			IrqGuard_t guard;
			const auto now = os_ticks;
			while (!sleepers.empty()) {
				// Access by reference to avoid copying
				const auto& node = sleepers.top();
				if (static_cast<int32_t>(now - node.wake_tick) >= 0) {
					// Wake up task: move it to the current write buffer.
					// Note: const_cast or copy is needed since priority_queue.top() is const.
					// Move is safe here because we pop the node immediately after.
					if (!task_buffers[write_idx].full()) {
						task_buffers[write_idx].push_back(std::move(const_cast<Lambda_t&>(node.task)));
					}
					sleepers.pop();
				}
				else {
					break;
				}
			}
		}
	};

	// ==========================================================
	// Public API
	// ==========================================================
	inline void post(Lambda_t fn)
	{
		Executor::get();
		Executor::post(std::move(fn));
	}

	inline void delay_ms(const uint32_t ms, Lambda_t fn)
	{
		Executor::get();
		Executor::add_sleeper(ms, std::move(fn));
	}

	inline void yield(Lambda_t fn)
	{
		post(std::move(fn));
	}

	// ==========================================================
	// Coroutine Infrastructure & Memory Pool
	// ==========================================================

#if (MOS_CONF_ASYNC_USE_POOL == true)
	// --- Promise Allocator (Static Memory Pool) ---
	// Non-template base class to ensure all Promise_t<T> share the same pool
	struct PromiseAllocator
	{
		struct FrameBlock
		{
			alignas(std::max_align_t) char data[ASYNC_FRAME_SIZE];
		};

		// Static Pool Object
		static inline etl::pool<FrameBlock, ASYNC_POOL_MAX> pool;

		// Overload operator new for Coroutines
		static void* operator new(size_t size)
		{
			// Protect pool access from interrupts
			IrqGuard_t guard;
			// If "Async: Frame > Pool Block Size" assert, increase Macro::ASYNC_FRAME_SIZE
			if (size > ASYNC_FRAME_SIZE) {
				MOS_ASSERT(false, "Async: Frame > Pool Block Size");
			}
			if (pool.full()) { // If "Async: Pool Full" assert, increase Macro::ASYNC_POOL_MAX
				MOS_ASSERT(false, "Async: Pool Full");
			}
			return pool.allocate();
		}
		// Overload operator delete for Coroutines
		static void operator delete(void* ptr, size_t) noexcept
		{
			IrqGuard_t guard;
			if (ptr) {
				pool.release(static_cast<FrameBlock*>(ptr));
			}
		}
	};
#endif

	template <typename T>
	struct Future_t;
	template <typename T>
	struct Promise_t;

	template <typename T>
	struct PromiseRet_t
	{
		template <typename U>
		void return_value(U&& val) noexcept
		{
			value = std::forward<U>(val);
		}

		T get_value() const { return value; }

	private:
		T value;
	};

	template <>
	struct PromiseRet_t<void>
	{
		void return_void() noexcept {}
		void get_value() const {}
	};

	// Promise_t definition
	// Inherits from Allocator ONLY if MOS_CONF_ASYNC_USE_POOL is defined
	template <typename T>
	struct Promise_t : public PromiseRet_t<T>
#if MOS_CONF_ASYNC_USE_POOL
	    ,
	                   public PromiseAllocator
#endif
	{
		CoroHandle_t<> next = nullptr;

		void unhandled_exception() noexcept {}
		auto initial_suspend() noexcept { return std::suspend_always {}; }
		auto final_suspend() noexcept { return FutureFinal_t {this}; }
		auto get_return_object()
		{
			return Future_t<T> {CoroHandle_t<Promise_t<T>>::from_promise(*this)};
		}

	private:
		struct FutureFinal_t
		{
			Promise_t<T>* source;

			bool await_ready() noexcept { return !source->next; }
			void await_resume() noexcept { source->get_value(); }

			auto await_suspend(CoroHandle_t<Promise_t<T>> h) noexcept
			{ // Use handle passing to avoid recursive stack
				return h.promise().next;
			}
		};
	};

	template <typename T = void>
	struct Future_t
	{
		// For C++ Coroutine Requirements
		using promise_type = Promise_t<T>; // Must NOT change `promise_type` name

		CoroHandle_t<promise_type> handle;

		explicit Future_t(CoroHandle_t<promise_type> h) noexcept: handle(h) {}
		Future_t(Future_t&& t) noexcept: handle(t.handle) { t.handle = nullptr; }

		Future_t& operator=(Future_t&& t) noexcept
		{
			if (this != &t) {
				if (handle) handle.destroy();
				handle   = t.handle;
				t.handle = nullptr;
			}
			return *this;
		}

		// Disable copying
		Future_t(const Future_t&)            = delete;
		Future_t& operator=(const Future_t&) = delete;

		~Future_t()
		{
			if (handle) {
				if (handle.done()) handle.destroy();
				else
					handle.resume();
			}
		}

		bool await_ready() const noexcept { return false; }
		T await_resume() noexcept { return handle.promise().get_value(); }

		template <typename P>
		auto await_suspend(CoroHandle_t<P> next) noexcept
		{
			handle.promise().next = next;
			return handle;
		}

		// Detach implementation to allow fire-and-forget
		inline void detach()
		{
			// Simple self-destroying coroutine.
			if (handle) {
				if (!handle.done()) {
					handle.resume();
				}

				// Releases handle ownership to prevent destroy() during destructor.
				handle = nullptr;
			}
		}
	};

	template <typename T>
	Future_t(T) -> Future_t<T>;

	// ==========================================================
	// Awaiters
	// ==========================================================
	template <typename T, typename CallbackFunc>
	struct CallbackAwaiter
	{
		CallbackFunc callback;
		T result;

		CallbackAwaiter(CallbackFunc&& fn) noexcept: callback(std::move(fn)) {}
		bool await_ready() noexcept { return false; }

		void await_suspend(std::coroutine_handle<> handle)
		{
			callback([h = handle, this](T t) mutable {
				result = std::move(t);
				h.resume();
			});
		}
		T await_resume() noexcept { return std::move(result); }
	};

	template <typename CallbackFunc>
	struct CallbackAwaiter<void, CallbackFunc>
	{
		CallbackFunc callback;
		CallbackAwaiter(CallbackFunc&& fn) noexcept: callback(std::move(fn)) {}
		bool await_ready() noexcept { return false; }

		void await_suspend(CoroHandle_t<> handle)
		{
			callback([h = handle]() {
				if (!h.done()) h.resume();
			});
		}
		void await_resume() noexcept {}
	};

	template <typename T, typename CallbackFunc>
	auto callback_wrapper(CallbackFunc&& callback)
	{
		return CallbackAwaiter<T, CallbackFunc> {std::move(callback)};
	}

	// Delay Coroutine
	inline Future_t<> delay(const Tick_t ticks)
	{
		co_return co_await callback_wrapper<void>([ticks](Lambda_t task) {
			delay_ms(ticks, std::move(task));
		});
	}
}

#endif