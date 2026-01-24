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
	using Task::os_ticks;
	using Tick_t = Task::Tick_t;

	// ==========================================================
	// Custom FixedFunction
	// Replaces std::function, stores Lambda in a fixed buffer.
	// Supports Copy/Move semantics, compatible with ETL containers.
	// ==========================================================
	template <size_t MAX_SIZE>
	class FixedFunction_t
	{
	public:
		using Invoker_t = void (*)(void*);
		using Cloner_t  = void (*)(void* dest, const void* src);

		FixedFunction_t(): invoker(nullptr), cloner(nullptr) {}

		template <typename F>
		FixedFunction_t(F f)
		{
			static_assert(sizeof(F) <= MAX_SIZE, "Lambda too large for FixedFunction buffer!");
			new (buffer) F(std::move(f));
			invoker = [](void* data) { (*reinterpret_cast<F*>(data))(); };
			cloner  = [](void* dest, const void* src) { new (dest) F(*reinterpret_cast<const F*>(src)); };
		}

		FixedFunction_t(const FixedFunction_t& other) { copy_from(other); }
		FixedFunction_t(FixedFunction_t&& other) noexcept { move_from(std::move(other)); }

		FixedFunction_t& operator=(const FixedFunction_t& other)
		{
			if (this != &other) copy_from(other);
			return *this;
		}

		FixedFunction_t& operator=(FixedFunction_t&& other) noexcept
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
		alignas(void*) char buffer[MAX_SIZE];
		Invoker_t invoker;
		Cloner_t cloner;

		void copy_from(const FixedFunction_t& other)
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

		void move_from(FixedFunction_t&& other)
		{
			if (other.invoker) {
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

	using Lambda_t = FixedFunction_t<Macro::ASYNC_TASK_SIZE>;

	// ==========================================================
	// Executor Implementation
	// ==========================================================
	struct Executor
	{
		void poll()
		{
			clean_sleepers();
			if (ready_tasks.empty()) return;

			run_list = std::move(ready_tasks);
			ready_tasks.clear();

			for (auto& task: run_list) {
				if (task) task();
			}
			run_list.clear();
		}

		void post(Lambda_t fn)
		{
			if (!ready_tasks.full()) {
				ready_tasks.push_back(std::move(fn));
			}
			else {
				MOS_ASSERT(false, "Async Queue Full!");
			}
		}

		void add_sleeper(uint32_t ms, Lambda_t fn)
		{
			if (!sleepers.full()) {
				sleepers.push(Sleeper_t {os_ticks + ms, std::move(fn)});
			}
			else {
				MOS_ASSERT(false, "Async Sleeper Full!");
			}
		}

		static Executor& get()
		{
			static Executor executor;
			static bool initialized = false;
			if (!initialized) {
				auto waker_routine = [] {
					while (true) executor.poll();
				};
				if (Task::create(waker_routine, nullptr, Macro::PRI_MIN, "async/exec")) {
					initialized = true;
				}
				else {
					MOS_ASSERT(false, "Async Spawn Failed!");
				}
			}
			return executor;
		}

	private:
		struct Sleeper_t
		{
			uint32_t wake_tick;
			Lambda_t task;
		};

		struct SleeperCompare
		{
			bool operator()(const Sleeper_t& lhs, const Sleeper_t& rhs) const
			{
				return lhs.wake_tick > rhs.wake_tick;
			}
		};

		void clean_sleepers()
		{
			Utils::IrqGuard_t guard;
			const auto now = os_ticks;
			while (!sleepers.empty()) {
				const auto& node = sleepers.top();
				if (static_cast<int32_t>(now - node.wake_tick) >= 0) {
					if (!ready_tasks.full()) {
						ready_tasks.push_back(node.task);
					}
					sleepers.pop();
				}
				else {
					break;
				}
			}
		}

		using TaskBuffer_t  = etl::vector<Lambda_t, Macro::ASYNC_TASK_MAX>;
		using SleepBuffer_t = etl::priority_queue<
		    Sleeper_t,
		    Macro::ASYNC_TASK_MAX,
		    etl::vector<Sleeper_t, Macro::ASYNC_TASK_MAX>,
		    SleeperCompare>;

		TaskBuffer_t ready_tasks, run_list;
		SleepBuffer_t sleepers;
	};

	// ==========================================================
	// Public API
	// ==========================================================
	inline void post(Lambda_t fn) { Executor::get().post(std::move(fn)); }
	inline void delay_ms(const uint32_t ms, Lambda_t fn) { Executor::get().add_sleeper(ms, std::move(fn)); }
	inline void yield(Lambda_t fn) { Executor::get().post(std::move(fn)); }

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
			alignas(std::max_align_t) char data[Macro::ASYNC_FRAME_SIZE];
		};

		static inline etl::pool<FrameBlock, Macro::ASYNC_POOL_MAX> pool;

		// Overload operator new for Coroutines
		static void* operator new(size_t size)
		{
			// Protect pool access from interrupts
			Utils::IrqGuard_t guard;

			// If "Async: Frame > Pool Block Size" assert, increase Macro::ASYNC_FRAME_SIZE
			if (size > Macro::ASYNC_FRAME_SIZE) {
				MOS_ASSERT(false, "Async: Frame > Pool Block Size");
			}
			if (pool.full()) { // If "Async: Pool Full" assert, increase Macro::ASYNC_MAX_POOL
				MOS_ASSERT(false, "Async: Pool Full");
			}

			return pool.allocate();
		}

		// Overload operator delete for Coroutines
		static void operator delete(void* ptr, size_t)
		{
			Utils::IrqGuard_t guard;
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
		T value;
		template <typename U>
		void return_value(U&& val) noexcept
		{
			value = std::forward<U>(val);
		}
		T get_value() const { return value; }
	};

	template <>
	struct PromiseRet_t<void>
	{
		void return_void() noexcept {}
		void get_value() const {}
	};

	template <typename T>
	struct FutureFinal_t
	{
		Promise_t<T>* source;
		bool await_ready() noexcept { return !source->next; }
		void await_resume() noexcept { source->get_value(); }
		auto await_suspend(std::coroutine_handle<Promise_t<T>> h) noexcept { return h.promise().next; }
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
		std::coroutine_handle<> next;

		Future_t<T> get_return_object();
		auto initial_suspend() { return std::suspend_always {}; }
		auto final_suspend() noexcept { return FutureFinal_t<T> {this}; }
		void unhandled_exception() noexcept {}
	};

	template <typename T = void>
	struct Future_t
	{
		using promise_type = Promise_t<T>;
		std::coroutine_handle<promise_type> handle;

		explicit Future_t(std::coroutine_handle<promise_type> h): handle(h) {}
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
		auto await_suspend(std::coroutine_handle<P> next)
		{
			handle.promise().next = next;
			return handle;
		}

		auto detach()
		{
			return [](Future_t<T> lazy) mutable -> Future_t<T> {
				co_return co_await std::move(lazy);
			}(std::move(*this));
		}
	};

	template <typename T>
	Future_t(T) -> Future_t<T>;

	template <typename T>
	Future_t<T> Promise_t<T>::get_return_object()
	{
		return Future_t<T> {std::coroutine_handle<Promise_t<T>>::from_promise(*this)};
	}

	// ==========================================================
	// Awaiters
	// ==========================================================
	template <typename T, typename CallbackFunc>
	struct CallbackAwaiter
	{
		CallbackFunc callback;
		T result;

		CallbackAwaiter(CallbackFunc fn): callback(std::move(fn)) {}
		bool await_ready() noexcept { return false; }

		void await_suspend(std::coroutine_handle<> handle)
		{
			callback([h = std::move(handle), this](T t) mutable {
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
		CallbackAwaiter(CallbackFunc fn): callback(std::move(fn)) {}
		bool await_ready() noexcept { return false; }

		void await_suspend(std::coroutine_handle<> handle)
		{
			callback([h = handle]() {
				if (!h.done()) h.resume();
			});
		}
		void await_resume() noexcept {}
	};

	template <typename T, typename CallbackFunc>
	auto callback_wrapper(CallbackFunc callback)
	{
		return CallbackAwaiter<T, CallbackFunc> {callback};
	}

	// ==========================================================
	// Delay Coroutine
	// ==========================================================
	struct DelayAction
	{
		Tick_t ticks;
		void operator()(Lambda_t task) const
		{
			delay_ms(ticks, std::move(task));
		}
	};

	inline Future_t<> delay(const Tick_t ticks)
	{
		co_return co_await callback_wrapper<void>(DelayAction {ticks});
	}
}

#endif