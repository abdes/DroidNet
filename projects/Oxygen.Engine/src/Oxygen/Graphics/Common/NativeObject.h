//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Non-owning holder of a native object handle or pointer.
/*!
 The `NativeObject` class is a utility for managing native object handles or
 pointers in a type-safe manner. It stores either an integer handle or a
 pointer, along with the type ID of the owning graphics object for additional
 safety and debugging.

\note This class does not participate in any way or form in the lifecycle of the
      native object handle or pointer it holds. It is the responsibility of the
      user to ensure that the lifetime of a NativeObject is shorter than that of
      the native object handle or pointer it holds.
*/
class NativeObject {
    //! Indicates an invalid handle value, or uninitialized state.
    static constexpr uint64_t kInvalidHandle { 0 };

    //! The native object handle, either as an integer or as a pointer.
    union {
        uint64_t integer { kInvalidHandle };
        void* pointer;
    };

    //! The type ID of the graphics object that owns this handle. Typically
    //! requires that the owner class is derived from `oxygen::Object` and
    //! implements the required methods of the oxygen type system.
    TypeId owner_type_id_ { kInvalidTypeId };

    //! Indicates whether the stored value is a pointer.
    bool is_pointer_ { false };

public:
    //! Default constructor; creates an invalid `NativeObject`.
    constexpr NativeObject() noexcept = default;

    //! Constructs a `NativeObject` with an integer handle.
    /*!
     \param handle The integer handle of the native object.
     \param type_id The type ID of the owning graphics object.
    */
    constexpr NativeObject(const uint64_t handle, const TypeId type_id) noexcept
        : integer(handle)
        , owner_type_id_(type_id)
    {
    }

    //! Constructs a `NativeObject` with a non-const pointer.
    /*!
     \param _pointer The pointer to the native object.
     \param type_id The type ID of the owning graphics object.
    */
    constexpr NativeObject(void* _pointer, const TypeId type_id) noexcept
        : pointer(_pointer)
        , owner_type_id_(type_id)
        , is_pointer_(true)
    {
    }

    //! Constructs a `NativeObject` with a const pointer.
    /*!
     \param _pointer The const pointer to the native object.
     \param type_id The type ID of the owning graphics object.
    */
    constexpr NativeObject(const void* _pointer, const TypeId type_id) noexcept
        : pointer(const_cast<void*>(_pointer))
        , owner_type_id_(type_id)
        , is_pointer_(true)
    {
    }

    OXYGEN_DEFAULT_COPYABLE(NativeObject)
    OXYGEN_DEFAULT_MOVABLE(NativeObject)

    //! Checks if the `NativeObject` holds a valid handle or pointer.
    [[nodiscard]] constexpr auto IsValid() const noexcept { return integer != kInvalidHandle; }

    //! Retrieves the integer handle of the native object.
    [[nodiscard]] constexpr auto AsInteger() const noexcept { return integer; }

    //! Retrieves the pointer to the native object. (const version)
    /*!
     \tparam T The type of the native object being pointed to.
     \return A const pointer to the native object.
     \throws std::runtime_error If the `NativeObject` was created with an
     integer handle.
    */
    template <typename T>
    [[nodiscard]] auto AsPointer() const -> T*
    {
        if (!is_pointer_) {
            throw std::runtime_error(
                "Cannot convert a NativeObject created with integer handle to a pointer");
        }
        return static_cast<T*>(pointer);
    }

    //! Retrieves the pointer to the native object. (non-const version)
    /*!
     \tparam T The type of the native object being pointed to.
     \return A pointer to the native object.
     \throws std::runtime_error If the `NativeObject` was created with an
     integer handle, or if it was initialized with a const pointer.
    */
    template <typename T>
    [[nodiscard]] auto AsPointer() -> T*
    {
        if (!is_pointer_) {
            throw std::runtime_error(
                "Cannot convert a NativeObject created with integer handle to a pointer");
        }
        return static_cast<T*>(pointer);
    }
    //! Retrieves the type ID of the owning graphics object.
    [[nodiscard]] constexpr auto OwnerTypeId() const noexcept { return owner_type_id_; }

    //! Compares two `NativeObject` instances for equality. Only compares the
    //! pointer/handle and the owner type id.
    constexpr auto operator==(const NativeObject& other) const noexcept -> bool
    {
        return (owner_type_id_ == other.owner_type_id_) && (integer == other.integer);
    }

    //! Compares two `NativeObject` instances for inequality. Only compares the
    //! pointer/handle and the owner type id.
    constexpr auto operator!=(const NativeObject& other) const noexcept -> bool
    {
        return !(*this == other);
    }
};

// C++20 concept to identify resources that can have barriers
template <typename T>
concept HoldsNativeResource = requires(T obj) {
    { obj.GetNativeResource() } -> std::convertible_to<NativeObject>;
};

//! Converts a `NativeObject` to a string representation.
OXYGEN_GFX_API auto to_string(const NativeObject& obj) -> std::string;

} // namespace oxygen::graphics

//! Provides a hash function for `NativeObject`.
/*!
 * Combines the hash of the object's owner type id, and the integer handle to
 * produce a unique hash value for the `NativeObject`.
 */
template <>
struct std::hash<oxygen::graphics::NativeObject> {
    auto operator()(const oxygen::graphics::NativeObject& obj) const noexcept -> size_t
    {
        size_t seed = 0;
        oxygen::HashCombine(seed, obj.OwnerTypeId());
        oxygen::HashCombine(seed, obj.AsInteger());
        return seed;
    }
};
