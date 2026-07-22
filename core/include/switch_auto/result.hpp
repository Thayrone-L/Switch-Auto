#pragma once

#include <optional>
#include <utility>
#include <variant>

namespace switch_auto {

template <typename T, typename E>
class Result {
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(E error) { return Result(error); }

    bool hasValue() const { return std::holds_alternative<T>(storage_); }
    explicit operator bool() const { return hasValue(); }

    const T& value() const { return std::get<T>(storage_); }
    T& value() { return std::get<T>(storage_); }
    E error() const { return std::get<E>(storage_); }

private:
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(E error) : storage_(error) {}

    std::variant<T, E> storage_;
};

template <typename E>
class Result<void, E> {
public:
    static Result success() { return Result(std::nullopt); }
    static Result failure(E error) { return Result(error); }

    bool hasValue() const { return !error_.has_value(); }
    explicit operator bool() const { return hasValue(); }
    E error() const { return *error_; }

private:
    explicit Result(std::optional<E> error) : error_(error) {}
    std::optional<E> error_;
};

}  // namespace switch_auto
