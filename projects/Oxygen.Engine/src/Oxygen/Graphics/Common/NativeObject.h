//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include <Oxygen/Composition/TypeSystem.h>

namespace oxygen::graphics {

//! Represents a native object handle or pointer.
/*!
 The `NativeObject` class is a utility for managing native object handles or
 pointers in a type-safe manner. It stores either an integer handle or a
 pointer, along with the type ID of the owning graphics object for additional
 safety and debugging.
*/
class NativeObject {
    //! The native object handle, either as an integer or as a pointer.
    union {
        uint64_t integer;
        void* pointer;
    };

    //! The type ID of the graphics object that owns this handle. Typically
    //! requires that the owner class is derived from `oxygen::Object` and
    //! implements the required methods of the oxygen type system.
    TypeId owner_type_id_;

    //! Indicates whether the stored value is a pointer.
    bool is_pointer_ { false };

public:
    //! Constructs a `NativeObject` with an integer handle.
    /*!
     \param handle The integer handle of the native object.
     \param type_id The type ID of the owning graphics object.
    */
    explicit NativeObject(const uint64_t handle, const TypeId type_id)
        : integer(handle)
        , owner_type_id_(type_id)
    {
    }

    //! Constructs a `NativeObject` with a pointer.
    /*!
     \param pointer The pointer to the native object.
     \param type_id The type ID of the owning graphics object.
    */
    explicit NativeObject(void* pointer, const TypeId type_id)
        : pointer(pointer)
        , owner_type_id_(type_id)
        , is_pointer_(true)
    {
    }

    //! Retrieves the integer handle of the native object.
    [[nodiscard]] auto AsInteger() const { return integer; }

    //! Retrieves the pointer to the native object.
    /*!
     \tparam T The type of the native object being pointed to (depending on the
     graphics backend).
     \return A pointer to the native object.
     \throws std::runtime_error If the `NativeObject` was created with an
     integer handle.
    */
    template <typename T>
    [[nodiscard]] auto AsPointer() const
    {
        if (!is_pointer_) {
            throw std::runtime_error("Cannot convert a NativeObject created with integer handle to a pointer");
        }
        return static_cast<T*>(pointer);
    }

    //! Retrieves the type ID of the owning graphics object.
    [[nodiscard]] auto OwnerTypeId() const { return owner_type_id_; }

    //! Compares two `NativeObject` instances for equality.
    /*!
     \param other The other `NativeObject` to compare with.
     \return `true` if the two objects have the same owner type id and the same
     handle value, `false` otherwise.
    */
    auto operator==(const NativeObject& other) const -> bool
    {
        return (owner_type_id_ == other.owner_type_id_) && (integer == other.integer);
    }
};

// C++20 concept to identify resources that can have barriers
template <typename T>
concept HoldsNativeResource = requires(T obj) {
  { obj.GetNativeResource() } -> std::convertible_to<NativeObject>;
};

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
        // Combine the hash of owner_type_id_ and integer
        const size_t owner_type_hash = std::hash<oxygen::TypeId> {}(obj.OwnerTypeId());
        const size_t native_handle_hash = std::hash<uint64_t> {}(obj.AsInteger());

        // Combine the two hashes using a common hash combining technique
        return owner_type_hash ^ (native_handle_hash + 0x9e3779b9 + (owner_type_hash << 6) + (owner_type_hash >> 2));
    }
};
