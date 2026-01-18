#ifndef _MOS_ASYNC_
#define _MOS_ASYNC_

// Import ETL Library
#include "etl/vector.h"
#include "etl/multimap.h"

// Use C++'s Coroutine Infra
#include <coroutine>

#include "task.hpp"
#include "utils.hpp"

namespace MOS::Kernel::Async
{
	using Global::os_ticks;
	using Tick_t = Task::Tick_t;

	// ==========================================================
	// Custom FixedFunction
	// Replaces std::function, stores Lambda in a fixed buffer.
	// Supports Copy/Move semantics, compatible with ETL containers.
	// ==========================================================
	template <size_t MAX_SIZE>
	class FixedFunction
	{
	public:
		using Invoker_t = void (*)(void*);
		using Cloner_t  = void (*)(void* dest, const void* src);

		// Default Constructor
		FixedFunction(): invoker(nullptr), cloner(nullptr) {}

		// Construct from Lambda or Functor
		template <typename F>
		FixedFunction(F f)
		{
			static_assert(sizeof(F) <= MAX_SIZE, "Lambda too large for FixedFunction buffer!");

			// Construct object in buffer
			new (buffer) F(std::move(f));

			// Set Invoker (Type Erasure)
			invoker = [](void* data) {
				(*reinterpret_cast<F*>(data))();
			};

			// Set Cloner (For Copy Construction)
			cloner = [](void* dest, const void* src) {
				new (dest) F(*reinterpret_cast<const F*>(src));
			};
		}

		// Copy Constructor
		FixedFunction(const FixedFunction& other)
		{
			copy_from(other);
		}

		// Move Constructor
		FixedFunction(FixedFunction&& other) noexcept
		{
			move_from(std::move(other));
		}

		// Copy Assignment
		FixedFunction& operator=(const FixedFunction& other)
		{
			if (this != &other) {
				copy_from(other);
			}
			return *this;
		}

		// Move Assignment
		FixedFunction& operator=(FixedFunction&& other) noexcept
		{
			if (this != &other) {
				move_from(std::move(other));
			}
			return *this;
		}

		// Check if callable
		explicit operator bool() const { return invoker != nullptr; }

		// Invoke
		void operator()()
		{
			if (invoker) invoker(buffer);
		}

	private:
		alignas(void*) char buffer[MAX_SIZE]; // Static storage
		Invoker_t invoker;
		Cloner_t cloner;

		void copy_from(const FixedFunction& other)
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

		void move_from(FixedFunction&& other)
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

	// Generic Lambda type for Async module
	using Lambda_t = FixedFunction<Macro::ASYNC_TASK_SIZE>;

	// ==========================================================
	// Executor Implementation
	// ==========================================================
	struct Executor
	{
		// Poll the executor once
		void poll()
		{
			clean_sleepers();

			if (ready_tasks.empty()) {
				return;
			}

			// Move tasks to run_list to allow re-entry/posting during execution
			run_list = std::move(ready_tasks);
			ready_tasks.clear();

			for (auto& task: run_list) {
				if (task) {
					task();
				}
			}

			run_list.clear();
		}

		// Post a task to the executor
		void post(Lambda_t fn)
		{
			if (!ready_tasks.full()) {
				ready_tasks.push_back(std::move(fn));
			}
			else {
				MOS_ASSERT(false, "Async Queue Full!");
			}
		}

		// Delay with a callback
		// Removed MOS_INLINE to let compiler decide
		void add_sleeper(uint32_t ms, Lambda_t fn)
		{
			if (!sleepers.full()) {
				sleepers.insert({os_ticks + ms, std::move(fn)});
			}
			else {
				MOS_ASSERT(false, "Async Sleeper Full!");
			}
		}

		// Get a static instance, init only once
		static Executor& get()
		{
			static Executor executor;
			static bool initialized = false;

			if (!initialized) {
				// Create a background waker task
				auto waker_routine = [] {
					while (true) {
						executor.poll();
					}
				};

				// Create async-exec only once
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
		// Check sleepers and wake up due tasks
		void clean_sleepers()
		{
			Utils::IrqGuard_t guard;
			const auto now = os_ticks;

			for (auto it = sleepers.begin(); it != sleepers.end();) {
				// Use signed arithmetic for correct wrap-around handling
				if (static_cast<int32_t>(now - it->first) >= 0) {
					if (!ready_tasks.full()) {
						ready_tasks.push_back(std::move(it->second));
					}
					it = sleepers.erase(it);
				}
				else {
					++it;
				}
			}
		}

		using TaskBuffer_t  = etl::vector<Lambda_t, Macro::ASYNC_TASK_MAX>;
		using SleepBuffer_t = etl::multimap<uint32_t, Lambda_t, Macro::ASYNC_TASK_MAX>;

		// Double buffering
		TaskBuffer_t ready_tasks, run_list;
		SleepBuffer_t sleepers;
	};

	// ==========================================================
	// Public API
	// Changed: 'MOS_INLINE static' -> 'inline'
	// ==========================================================
	inline void post(Lambda_t fn)
	{
		Executor::get().post(std::move(fn));
	}

	inline void delay_ms(const uint32_t ms, Lambda_t fn)
	{
		Executor::get().add_sleeper(ms, std::move(fn));
	}

	inline void yield(Lambda_t fn)
	{
		Executor::get().post(std::move(fn));
	}

	// ==========================================================
	// Coroutine Infrastructure
	// ==========================================================
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
		auto await_suspend(std::coroutine_handle<Promise_t<T>> h) noexcept
		{
			return h.promise().next;
		}
	};

	template <typename T>
	struct Promise_t : public PromiseRet_t<T>
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