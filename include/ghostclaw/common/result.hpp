#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace ghostclaw::common {

class Status {
public:
  static Status success() { return Status(true, ""); }
  static Status error(std::string message) { return Status(false, std::move(message)); }

  [[nodiscard]] bool ok() const { return ok_; }
  [[nodiscard]] const std::string &error() const { return error_; }

private:
  Status(bool ok, std::string error) : ok_(ok), error_(std::move(error)) {}

  bool ok_;
  std::string error_;
};

template <typename T> class Result {
public:
  static Result success(T value) { return Result(true, std::move(value), ""); }
  static Result failure(std::string message) {
    return Result(false, std::nullopt, std::move(message));
  }

  [[nodiscard]] bool ok() const { return ok_; }

  [[nodiscard]] const T &value() const {
    if (!ok_) {
      throw std::logic_error("Result has no value: " + error_);
    }
    return *value_;
  }

  [[nodiscard]] T &value() {
    if (!ok_) {
      throw std::logic_error("Result has no value: " + error_);
    }
    return *value_;
  }

  [[nodiscard]] const std::string &error() const { return error_; }

private:
  Result(bool ok, std::optional<T> value, std::string error)
      : ok_(ok), value_(std::move(value)), error_(std::move(error)) {}

  bool ok_;
  std::optional<T> value_;
  std::string error_;
};

template <> class Result<void> {
public:
  static Result success() { return Result(true, ""); }
  static Result failure(std::string message) { return Result(false, std::move(message)); }

  [[nodiscard]] bool ok() const { return ok_; }
  [[nodiscard]] const std::string &error() const { return error_; }

private:
  Result(bool ok, std::string error) : ok_(ok), error_(std::move(error)) {}

  bool ok_;
  std::string error_;
};

} // namespace ghostclaw::common
