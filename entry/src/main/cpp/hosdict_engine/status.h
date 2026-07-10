#pragma once

#include <string>
#include <utility>

namespace hoskey::engine {

enum class StatusCode {
  kOk = 0,
  kInvalidArgument,
  kNotFound,
  kIoError,
  kParseError,
  kUnsupportedFormat,
  kNotImplemented,
  kInvalidState
};

struct Status {
  StatusCode code = StatusCode::kOk;
  std::string message;

  [[nodiscard]] bool ok() const {
    return code == StatusCode::kOk;
  }

  static Status Ok() {
    return {StatusCode::kOk, {}};
  }

  static Status InvalidArgument(std::string message) {
    return {StatusCode::kInvalidArgument, std::move(message)};
  }

  static Status NotFound(std::string message) {
    return {StatusCode::kNotFound, std::move(message)};
  }

  static Status IoError(std::string message) {
    return {StatusCode::kIoError, std::move(message)};
  }

  static Status ParseError(std::string message) {
    return {StatusCode::kParseError, std::move(message)};
  }

  static Status UnsupportedFormat(std::string message) {
    return {StatusCode::kUnsupportedFormat, std::move(message)};
  }

  static Status NotImplemented(std::string message) {
    return {StatusCode::kNotImplemented, std::move(message)};
  }

  static Status InvalidState(std::string message) {
    return {StatusCode::kInvalidState, std::move(message)};
  }
};

}  // namespace hoskey::engine
