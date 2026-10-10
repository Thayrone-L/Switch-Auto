#pragma once

#include <type_traits>
#include <optional>
#include <utility>
#include <variant>

namespace switch_auto::base {

template <typename Value, typename Error>
class [[nodiscard]] Result {
    static_assert(!std::is_same_v<Value, Error>,
                  "Result value and error types must be distinct");
    static_assert(std::is_nothrow_move_constructible_v<Value>,
                  "Result values must be nothrow move constructible");
    static_assert(std::is_nothrow_move_constructible_v<Error>,
                  "Result errors must be nothrow move constructible");

public:
    [[nodiscard]] static Result success(Value value) noexcept(
        std::is_nothrow_move_constructible_v<Value>) {
        return Result{SuccessTag{}, std::move(value)};
    }

    [[nodiscard]] static Result failure(Error error) noexcept(
        std::is_nothrow_move_constructible_v<Error>) {
        return Result{FailureTag{}, std::move(error)};
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<Value>(storage_);
    }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] Value* valueIf() noexcept {
        return std::get_if<Value>(&storage_);
    }
    [[nodiscard]] const Value* valueIf() const noexcept {
        return std::get_if<Value>(&storage_);
    }
    [[nodiscard]] Error* errorIf() noexcept {
        return std::get_if<Error>(&storage_);
    }
    [[nodiscard]] const Error* errorIf() const noexcept {
        return std::get_if<Error>(&storage_);
    }

private:
    struct SuccessTag {};
    struct FailureTag {};

    Result(SuccessTag, Value value) noexcept(
        std::is_nothrow_move_constructible_v<Value>)
        : storage_(std::in_place_type<Value>, std::move(value)) {}
    Result(FailureTag, Error error) noexcept(
        std::is_nothrow_move_constructible_v<Error>)
        : storage_(std::in_place_type<Error>, std::move(error)) {}

    std::variant<Value, Error> storage_;
};

template <typename Error>
class [[nodiscard]] Result<void, Error> {
    static_assert(std::is_nothrow_move_constructible_v<Error>,
                  "Result errors must be nothrow move constructible");

public:
    [[nodiscard]] static constexpr Result success() noexcept {
        return Result{};
    }

    [[nodiscard]] static constexpr Result failure(Error error) noexcept(
        std::is_nothrow_move_constructible_v<Error>) {
        return Result{std::move(error)};
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept {
        return !error_.has_value();
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return hasValue();
    }
    [[nodiscard]] constexpr Error* errorIf() noexcept {
        return error_ ? &*error_ : nullptr;
    }
    [[nodiscard]] constexpr const Error* errorIf() const noexcept {
        return error_ ? &*error_ : nullptr;
    }

private:
    constexpr Result() noexcept = default;
    explicit constexpr Result(Error error) noexcept(
        std::is_nothrow_move_constructible_v<Error>)
        : error_(std::move(error)) {}

    std::optional<Error> error_;
};

}  // namespace switch_auto::base
