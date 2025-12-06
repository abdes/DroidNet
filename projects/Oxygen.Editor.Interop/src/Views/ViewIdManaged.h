//===----------------------------------------------------------------------===//
// Managed wrapper for native oxygen::ViewId
// This header provides a single, self-contained managed value type that
// mirrors the native ViewId and provides FromNative/ToNative helpers.
// It intentionally contains no external converter helpers or engine logic.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <Oxygen/Core/Types/View.h>

namespace Oxygen::Interop {

  namespace native = ::oxygen;

  /// <summary>
  /// Strongly typed managed wrapper for the native <c>oxygen::ViewId</c>.
  /// Mirrors the native identifier (uint64_t) and provides explicit
  /// conversion helpers so managed callers can round-trip values safely.
  /// </summary>
  [System::SerializableAttribute] public value struct ViewIdManaged {
  private:
    System::UInt64 value_;

  public:
    // Construct from a raw integral value.
    ViewIdManaged(System::UInt64 v) : value_(v) {}

    /// <summary>Underlying integral value.</summary>
    property System::UInt64 Value {
      System::UInt64 get() { return value_; }
    }

    /// <summary>Whether this ViewId is valid (not equal to
    /// native::kInvalidViewId).</summary>
    property bool IsValid {
      bool get() {
        // Treat both zero (default value) and the native kInvalidViewId sentinel
        // as invalid.
        auto invalid = static_cast<System::UInt64>(native::kInvalidViewId.get());
        return value_ != 0ULL && value_ != invalid;
      }
    }

    /// <summary>Invalid sentinel.
    /// Use <c>ViewIdManaged::Invalid</c> to obtain the canonical invalid id.
    /// </summary>
    static property ViewIdManaged Invalid {
      ViewIdManaged get() {
        return ViewIdManaged(
          static_cast<System::UInt64>(native::kInvalidViewId.get()));
      }
    }

    /// <summary>Create a managed ViewId from the native type.</summary>
    static ViewIdManaged FromNative(const native::ViewId& nativeId) {
      return ViewIdManaged(static_cast<System::UInt64>(nativeId.get()));
    }

    /// <summary>Convert this managed ViewId to the native
    /// <c>oxygen::ViewId</c>.</summary>
    native::ViewId ToNative() {
      return native::ViewId{ static_cast<uint64_t>(value_) };
    }

    virtual System::String^ ToString() override {
      try {
        auto n = ToNative();
        return gcnew System::String(::oxygen::to_string(n).c_str());
      }
      catch (...) {
        return System::String::Format("ViewId({0})", value_);
      }
    }

    virtual bool Equals(System::Object^ obj) override {
      if (obj == nullptr)
        return false;
      if (obj->GetType() != ViewIdManaged::typeid)
        return false;
      return safe_cast<ViewIdManaged>(obj).value_ == value_;
    }

    virtual int GetHashCode() override {
      // XOR the upper and lower 32-bit halves for a reasonable hash.
      System::UInt64 v = value_;
      return static_cast<int>((v & 0xffffffffULL) ^ (v >> 32));
    }
  };

} // namespace Oxygen::Interop
