#pragma once

#include <list>

#include "asio_with_aliases.h"

namespace traft {

// TODO: no synchronization currently. Is it needed really?
template<typename DataType>
class ConditionAwaiter {
    using HandlerType = asio::async_result<asio::use_awaitable_t<>, void(error_code)>::handler_type;
    using ConditionType = std::function<bool(const DataType &)>;

public:
    ConditionAwaiter(asio::executor executor, DataType initial = DataType())
        : executor_(executor), data_(initial) {
        // Nothing
    }

    asio::awaitable<void> wait(ConditionType condition, const asio::use_awaitable_t<>& token = asio::use_awaitable) {
        return async_initiate<const asio::use_awaitable_t<>, void (error_code)>(
            [this](auto handler, ConditionType condition) {
                ConditionAndHandler waiter(std::move(condition), std::move(handler));
                if (tryWakeUp(&waiter)) {
                    return;
                }
                waiters_.emplace_back(std::move(waiter));
            }, token, std::move(condition));
    }

    const DataType &getData() const {
        return data_;
    }
    void updateData(DataType data) {
        data_ = std::move(data);
        for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
            if (!tryWakeUp(&(*it))) {
                continue;
            }
            it = waiters_.erase(it);
        }
    }

private:
    struct ConditionAndHandler {
        ConditionAndHandler(ConditionAndHandler&&) noexcept = default;
        ConditionAndHandler(ConditionType condition, HandlerType handler) noexcept
            : condition(std::move(condition)), handler(std::move(handler)) {
            // Nothing
        }

        ConditionType condition;
        HandlerType handler;
    };

    bool tryWakeUp(ConditionAndHandler *waiter) {
        if (!waiter->condition(data_)) {
            return false;
        }
        asio::post(executor_, [handler = std::move(waiter->handler)]() mutable {
            handler(errc::make_error_code(errc::success));
        });
        return true;
    }

    asio::executor executor_;
    DataType data_;
    // It's std::list because std::vector::erase requires assignment operator,
    // and HandlerType lacks it.
    std::list<ConditionAndHandler> waiters_;
};

} // namespace traft