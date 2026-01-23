//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <functional>
#include <type_traits>

namespace oxygen::graphics {

//! Template free interface to access ShadeByteCode instances. Provides better
//! ABI compatibility.
class IShaderByteCode {
public:
  IShaderByteCode() noexcept = default;
  virtual ~IShaderByteCode() noexcept = default;

  // Default copy constructor and copy assignment operator
  IShaderByteCode(const IShaderByteCode&) = default;
  auto operator=(const IShaderByteCode&) -> IShaderByteCode& = default;

  // Default move constructor and move assignment operator
  IShaderByteCode(IShaderByteCode&&) noexcept = default;
  auto operator=(IShaderByteCode&&) noexcept -> IShaderByteCode& = default;

  [[nodiscard]] virtual auto Size() const noexcept -> size_t = 0;
  [[nodiscard]] virtual auto Data() const noexcept -> const uint32_t* = 0;
};

//! Concept to check if T has GetBufferPointer(), GetBufferSize(), and
//! Release() methods, all noexcept. This is the typical case of managed
//! resources from graphics APIs.
/*!
 \note `GetBufferSize()` should return the size of the buffer in bytes.
*/
template <typename T>
concept ManagedBuffer = std::movable<T> && requires(T t) {
  { t->GetBufferPointer() } -> std::convertible_to<void*>;
  { t->GetBufferSize() } -> std::convertible_to<size_t>;
  { static_cast<bool>(t) } -> std::convertible_to<bool>;
};

//! Concept to specify a basic buffer with `size` and `data` members. The
//! ownership of the data is transferred when such buffer is used, and its use
//! requires a `deleter` function to be provided if the data is to be freed.
/*!
 \note `size` should contain the size of the buffer in bytes.
*/
template <typename T>
concept BasicBufferWithOwnershipTransfer = std::movable<T> && requires(T t) {
  { t.size } noexcept -> std::convertible_to<size_t>;
  { t.data } noexcept -> std::convertible_to<const uint32_t*>;
};

//! Concept to specify a buffer implemented with a contiguous container,
//! typically std::vector, std::array and std::string and derivatives.
/*!
 \note size() should return the size of the buffer in bytes.
*/
template <typename T>
concept IsContiguousContainer = std::movable<T> && requires(T t) {
  typename T::value_type; // Require that T has a value_type
  { t.data() } noexcept -> std::convertible_to<const uint32_t*>;
  { t.size() } noexcept -> std::convertible_to<size_t>;
} && std::contiguous_iterator<decltype(std::declval<T>().data())>;

//! A class to hold shader byte code. It is a wrapper around a buffer of type
//! `T`.
/*!
  \tparam T The type of the buffer to hold the shader byte code.

  The class is specialized for three cases:
  - Managed Buffer: Types with GetBufferPointer, GetBufferSize, and Release
    methods. Memory management for the data is handled through the `Release`
    method, and may or may not be handled automatically by the managed buffer
    itself.
  - Contiguous Container: Buffers using the standard library contiguous
    containers or equivalent. Automatic memory management for the data is
    handled by the container itself.
  - Basic Buffer With Ownership Transfer: Types with size and data members,
    and optional data memory management with a custom deleter. Ownership of
    the data is transferred when such buffer is wrapped.
*/
template <typename T> class ShaderByteCode;

//! Specialization for `ManagedBuffer` types.
/*!
  \tparam T The type of the buffer to hold the shader byte code.

  This specialization is for types that have a Release method, used for the
  buffer data memory management. The original buffer is __moved__ into the
  ShaderByteCode wrapper.
*/
template <ManagedBuffer T> class ShaderByteCode<T> : public IShaderByteCode {
public:
  explicit ShaderByteCode(T&& buffer) noexcept
    : buffer_(std::move(buffer))
  {
  }

  [[nodiscard]] auto Size() const noexcept -> size_t override
  {
    return buffer_->GetBufferSize();
  }

  [[nodiscard]] auto Data() const noexcept -> const uint32_t* override
  {
    return static_cast<uint32_t*>(buffer_->GetBufferPointer());
  }

  // Disable copy constructor and copy assignment operator
  ShaderByteCode(const ShaderByteCode&) = delete;
  auto operator=(const ShaderByteCode&) -> ShaderByteCode& = delete;

  // Enable move constructor and move assignment operator
  ShaderByteCode(ShaderByteCode&& other) noexcept
    : buffer_(std::move(other.buffer_))
  {
  }
  auto operator=(ShaderByteCode&& other) noexcept -> ShaderByteCode&
  {
    if (this != &other) {
      buffer_ = std::move(other.buffer_);
    }
    return *this;
  }

  //! Destructor, releases the buffer data.
  ~ShaderByteCode() noexcept override = default;

private:
  T buffer_;
};

//! Specialization for contiguous containers
/*!
 \tparam T The type of the buffer to hold the shader byte code.

 This specialization is for types that are contiguous containers, such as
 std::vector, std::array, and std::string. The original buffer is __moved__
 into the ShaderByteCode wrapper if the underlying type supports move
 semantics.

\note std::array does not move its data, so the buffer is copied into the new
      object. This is by the standard design of std::array.
*/
template <IsContiguousContainer T>
class ShaderByteCode<T> : public IShaderByteCode {
public:
  // Constructor that takes ownership of the buffer
  explicit ShaderByteCode(T&& buffer) noexcept
    : buffer_(std::move(buffer))
  {
  }

  ~ShaderByteCode() noexcept override = default;

  [[nodiscard]] auto Size() const noexcept -> size_t override
  {
    return buffer_.size() * sizeof(typename T::value_type);
  }

  [[nodiscard]] auto Data() const noexcept -> const uint32_t* override
  {
    return buffer_.data();
  }

  // Copy constructor
  ShaderByteCode(const ShaderByteCode& other) noexcept = default;

  // Copy assignment operator
  auto operator=(const ShaderByteCode& other) noexcept
    -> ShaderByteCode& = default;

  // Move constructor
  ShaderByteCode(ShaderByteCode&& other) noexcept = default;

  // Move assignment operator
  auto operator=(ShaderByteCode&& other) noexcept -> ShaderByteCode& = default;

private:
  T buffer_;
};

//! Specialization for a basic buffer with `size` and `data` members. Usually
//! implemented as a simple struct, with the data ownership transferred to the
//! ShaderByteCode.
/*!
 \tparam T The type of the buffer to hold the shader byte code.

 Memory management for the data buffer can be tricky for such basic data
 structures. This is why they require a custom deleter to be provided if the
 data buffer needs to be freed. That function is called when the
 ShaderByteCode is destroyed.

\note Certain buffers may not require a deleter, such as when the data is
      allocated on the stack or when the data is managed by another object. In
      such cases, the deleter can be set to `nullptr`.

\note When a buffer is wrapped, it's moved into the ShaderByteCode wrapper.
      The original buffer types should have the default move semantics
      implemented so it gets reset when moved.
*/
template <BasicBufferWithOwnershipTransfer T>
class ShaderByteCode<T> : public IShaderByteCode {
public:
  using Deleter = std::function<void(const uint32_t*)>;

  explicit ShaderByteCode(T&& buffer, Deleter deleter = nullptr) noexcept
    : buffer_(std::move(buffer))
    , deleter_(std::move(deleter))
  {
  }

  ~ShaderByteCode() noexcept override
  {
    if (deleter_) {
      deleter_(buffer_.data);
    }
    buffer_.data = nullptr;
    buffer_.size = 0;
  }

  // Disable copy constructor and copy assignment operator
  ShaderByteCode(const ShaderByteCode& other) noexcept = delete;
  auto operator=(const ShaderByteCode& other) noexcept
    -> ShaderByteCode& = delete;

  // Move constructor
  ShaderByteCode(ShaderByteCode&& other) noexcept
    : buffer_(std::move(other.buffer_))
    , deleter_(std::move(other.deleter_))
  {
    other.deleter_ = nullptr;
  }

  // Move assignment operator
  auto operator=(ShaderByteCode&& other) noexcept -> ShaderByteCode&
  {
    if (this != &other) {
      buffer_ = std::move(other.buffer_);
      deleter_ = std::move(other.deleter_);
      other.deleter_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] auto Size() const noexcept -> size_t override
  {
    return buffer_.size;
  }

  [[nodiscard]] auto Data() const noexcept -> const uint32_t* override
  {
    return buffer_.data;
  }

private:
  T buffer_;
  Deleter deleter_;
};

} // namespace oxygen::graphics
