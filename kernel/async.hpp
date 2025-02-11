#ifndef _MOS_ASYNC_
#define _MOS_ASYNC_

#include <map>
#include <vector>
#include <coroutine>
#include <functional>

#include "task.hpp"
#include "utils.hpp"

namespace MOS::Kernel::Async
{
	using Global::os_ticks;
	using Tick_t = Task::Tick_t;
	using lambda = std::function<void()>;

	// Responsible for managing and executing coroutines
	struct Executor
	{
		// Poll the executor for once
		void poll()
		{
			clean_sleepers();
			auto run_list = std::move(ready_tasks);
			for (auto& task: run_list) {
				task();
			}
		}

		// Post a task to the executor
		void post(lambda fn)
		{
			ready_tasks.push_back(std::move(fn));
		}

		// Get the static instance of the executor
		static auto& get_executor()
		{
			static Executor exector;

			// Static variable to indicate if the waker task has been spawned
			static bool spawn = []() {
				// Continuously polls the executor until empty
				static auto Waker = [] {
					while (true) {
						exector.poll();
					}
				};

				// Create a waker to poll, can be extended
				return nullptr != Task::create(Waker, nullptr, Macro::PRI_MIN, "async/exec");
			}();

			(void) spawn;
			return exector;
		}

		// Delay with a callback
		MOS_INLINE inline void
		add_sleeper(uint32_t ms, lambda fn)
		{
			sleepers.insert({os_ticks + ms, fn});
		}

		void clean_sleepers()
		{
			auto try_wake_up = [this](auto it) {
				auto old_it = it;
				it++;
				ready_tasks.push_back(std::move(old_it->second));
				sleepers.erase(old_it);
				return it;
			};

			Utils::IrqGuard_t guard;

			// Get the current tick count
			auto now = os_ticks;
			// Iterate through the sleepers multimap
			for (auto it = sleepers.begin(); it != sleepers.end();) {
				if (now >= it->first) {
					// Check if the time is hit
					if ((now - it->first) < UINT32_MAX) {
						it = try_wake_up(it);
						continue;
					}
				}
				else if (now < it->first) {
					if ((now - it->first) < UINT32_MAX) {
						it = try_wake_up(it);
						continue;
					}
				}
				it++;
			}
		}

	private:
		std::vector<lambda> ready_tasks;
		std::multimap<uint32_t, lambda> sleepers;
	};

	// Post a task to the executor
	MOS_INLINE static inline void
	post(lambda fn)
	{
		Executor::get_executor().post(std::move(fn));
	}

	// Delay with a callback
	MOS_INLINE static inline void
	delay_ms(const uint32_t ms, lambda fn)
	{
		Executor::get_executor().add_sleeper(ms, fn);
	}

	// Yield to give out
	MOS_INLINE static inline void
	yield(lambda fn)
	{
		Executor::get_executor().post(fn);
	}

	template <typename T>
	struct Future_t;

	template <typename T>
	struct Promise_t;

	template <typename T, typename CallbackFunction>
	struct CallbackAwaiter;

	template <typename T>
	struct PromiseRet_t
	{
		template <typename Type>
		MOS_INLINE inline void return_value(Type&& val) noexcept
		{
			value = val;
		}

		MOS_INLINE inline void unhandled_exception() noexcept {}

		MOS_INLINE inline T get_value() const { return value; }

	private:
		T value;
	};

	template <>
	struct PromiseRet_t<void>
	{
		MOS_INLINE inline constexpr void return_void() noexcept {}
		MOS_INLINE inline constexpr void unhandled_exception() noexcept {}
		MOS_INLINE inline constexpr void get_value() const {}
	};

	template <typename T>
	struct FutureFinal_t
	{
		using PromiseType   = Promise_t<T>;
		using PromiseHandle = std::coroutine_handle<PromiseType>;

		// Pointer to the source promise
		PromiseType* source;

		// Resume the await operation and get the value from the promise
		MOS_INLINE inline constexpr void
		await_resume() noexcept
		{
			// If the coroutine is detached and not co_awaited, rethrow the exception
			source->get_value();
		}

		// Check if the await operation is ready
		MOS_INLINE inline bool
		await_ready() noexcept
		{
			// If next is not empty, return it to wake up the waiting coroutine
			// If next is empty, indicate that the coroutine is the last in the chain
			return !source->next;
		}

		// Suspend the coroutine and return the next coroutine handle
		MOS_INLINE inline auto
		await_suspend(PromiseHandle handle) noexcept
		{
			return handle.promise().next;
		}
	};

	// Promise type implementation
	template <typename T>
	struct Promise_t : public PromiseRet_t<T>
	{
		// Get the return object (future) for the promise
		Future_t<T> get_return_object()
		{
			auto ret = Future_t<T> {std::coroutine_handle<Promise_t<T>>::from_promise(*this)};
			return ret;
		}

		// Final suspension point of the coroutine
		MOS_INLINE inline auto
		final_suspend() noexcept
		{
			return FutureFinal_t<T> {this};
		}

		// Initial suspension point of the coroutine
		MOS_INLINE inline auto
		initial_suspend()
		{
			return std::suspend_always {};
		}

		// The next coroutine handle to resume after this one is done
		std::coroutine_handle<> next;
	};

	// Coroutine awaitable wrapper
	template <typename T = void>
	struct Future_t
	{
		using promise_type  = Promise_t<T>;
		using PromiseHandle = std::coroutine_handle<promise_type>;

		// The current coroutine handle
		PromiseHandle current_handle;

		// Initialize the future with a coroutine handle
		explicit Future_t(PromiseHandle handle): current_handle(handle) {}

		~Future_t()
		{
			if (current_handle) {
				if (current_handle.done()) {
					// Destroy the coroutine if it is done
					current_handle.destroy();
				}
				else {
					// Resume the coroutine if it is not done
					current_handle.resume();
				}
			}
		}

		Future_t(Future_t&& t) noexcept
		    : current_handle(t.current_handle) { t.current_handle = nullptr; }

		// Move assignment operator
		Future_t& operator=(Future_t&& t) noexcept
		{
			if (&t != this) {
				if (current_handle) {
					// Destroy the current coroutine handle
					current_handle.destroy();
				}
				current_handle   = t.current_handle;
				t.current_handle = nullptr;
			}
			return *this;
		}

		Future_t(const Future_t&)            = delete;
		Future_t(Future_t&)                  = delete;
		Future_t& operator=(const Future_t&) = delete;
		Future_t& operator=(Future_t&)       = delete;

		// Check if the await operation is ready
		MOS_INLINE inline constexpr bool
		await_ready() const noexcept
		{
			return false;
		}

		// Resume the await operation and get the value from the promise
		MOS_INLINE inline T
		await_resume() noexcept
		{
			return current_handle.promise().get_value();
		}

		// Suspend the coroutine and set the next coroutine handle
		template <typename PromiseType>
		MOS_INLINE inline auto
		await_suspend(std::coroutine_handle<PromiseType> next)
		{
			current_handle.promise().next = next;
			return current_handle;
		}

		// Detach the future and launch the coroutine to free execution
		MOS_INLINE inline auto
		detach()
		{
			auto launched_coro = [](Future_t<T> lazy) mutable -> Future_t<T> {
				co_return co_await std::move(lazy);
			}(std::move(*this));

			return launched_coro;
		}
	};

	// Template deduction guide for Future_t
	template <typename T = void>
	Future_t(T) -> Future_t<T>;

	// Template struct for callback awaiter
	template <typename T, typename CallbackFunc>
	struct CallbackAwaiter
	{
	public:
		// Constructor to initialize the callback awaiter with a callback function
		CallbackAwaiter(CallbackFunc fn): callback(std::move(fn)) {}

		// Check if the await operation is ready
		MOS_INLINE inline bool await_ready() noexcept { return false; }

		// Suspend the coroutine and call the callback function
		MOS_INLINE inline void
		await_suspend(std::coroutine_handle<> handle)
		{
			callback([handle = std::move(handle), this](T t) mutable {
				result = std::move(t);
				handle.resume();
			});
		}

		// Resume the await operation and get the result
		MOS_INLINE inline T
		await_resume() noexcept { return std::move(result); }

	private:
		CallbackFunc callback;
		T result;
	};

	// Specialization of CallbackAwaiter for void return type
	template <typename CallbackFunc>
	struct CallbackAwaiter<void, CallbackFunc>
	{
	public:
		CallbackAwaiter(CallbackFunc fn): callback(std::move(fn)) {}

		// Check if the await operation is ready
		MOS_INLINE inline bool
		await_ready() noexcept { return false; }

		// Suspend the coroutine and call the callback function
		MOS_INLINE inline void
		await_suspend(std::coroutine_handle<> handle)
		{
			callback(handle);
		}

		// Resume the await operation (no-op for void)
		MOS_INLINE inline void await_resume() noexcept {}

	private:
		// The callback function
		CallbackFunc callback;
	};

	// Wrap a callback function into a callback awaiter
	template <typename T, typename CallbackFunc>
	CallbackAwaiter<T, CallbackFunc>
	callback_wrapper(CallbackFunc callback)
	{
		return CallbackAwaiter<T, CallbackFunc> {callback};
	}

	// Delay a coroutine with ticks
	Future_t<> delay(const Tick_t ticks)
	{
		co_return co_await callback_wrapper<void>(
		    [ticks](std::coroutine_handle<> handle) {
			    // Delay the execution of the coroutine handle
			    delay_ms(ticks, handle);
		    }
		);
	}
}

#endif