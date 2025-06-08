//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace oxygen {

//! Concept to detect if a type has a LogSafeCallError method for error logging.
template <typename T>
concept HasLogSafeCallError = requires(T t, const char* msg) {
    { t.LogSafeCallError(msg) } -> std::same_as<void>;
};

//! Core SafeCall function template for validated operation execution.
/*!
 SafeCall is a wrapper template for safely calling a method or executing an
 operation after validating the internal state of the target object. It is not
 intended to be used directly. Instead, classes should define wrapper methods
 that customize validation, logging behavior, and const/non-const handling. The
 wrapper pattern provides several key benefits:

 Classes define private SafeCall wrappers to customize behavior:
 - Specify validation logic (lambdas, member functions, or external functions)
 - Control logging behavior through the HasLogSafeCallError concept
 - Handle const and non-const operations appropriately
 - Provide a consistent safe API for the class's operations

 Validation functions must return std::optional<std::string> where std::nullopt
 indicates success and a string indicates failure with an error message.

 The validation can be implemented as:
 - Lambda functions for inline validation logic
 - Member function pointers for reusable validation methods
 - External function pointers for shared validation across classes

 Optional error logging is automatically enabled when the target class provides
 a LogSafeCallError(const char*) method. This concept-based detection allows
 classes to opt into logging without affecting classes that don't need it.
 Exception safety is guaranteed - all exceptions from the operation function
 are caught and converted to std::nullopt returns, ensuring noexcept behavior.

 __Example implementation pattern:__

 \code
 class MyComponent {
     int value_{0};
     bool is_ready_{true};

 public:
     // Unsafe method for performance-critical paths
     auto IncrementValueUnchecked() -> bool {
         if (value_ == 100) return false;
         ++value_;
         return true;
     }

     // Safe wrapper method
     auto IncrementValueSafe() noexcept -> std::optional<bool> {
         return SafeCallImpl(*this, [](MyComponent& self) -> bool {
             return self.IncrementValueUnchecked();
         });
     }

     // Optional logging support
     void LogSafeCallError(const char* reason) const noexcept {
         std::cerr << "MyComponent error: " << reason << "\n";
     }

 private:
     template <typename Self, typename Func>
     static auto SafeCallImpl(Self&& self, Func&& func) noexcept {
         return oxygen::SafeCall(
             std::forward<Self>(self),
             [](auto&& self_ref) -> std::optional<std::string> {
                 if (!self_ref.is_ready_) {
                     return "Component not ready";
                 }
                 return std::nullopt;
             },
             std::forward<Func>(func));     }
 };
 \endcode

 __Usage examples:__

 \code
 MyComponent component;

 // Basic usage with result checking
 if (auto result = component.IncrementValueSafe()) {
     std::cout << "Increment succeeded: " << *result << "\n";
 } else {
     std::cout << "Increment failed - component not ready\n";
 }

 // Using value_or for fallback behavior
 bool success = component.IncrementValueSafe().value_or(false);

 // Performance-critical path (when you know component is ready)
 if (component.is_ready()) {
     bool result = component.IncrementValueUnchecked();
 }
 \endcode

 __Derivation Challenges and CRTP Solution:__

 When using SafeCall in a class hierarchy, especially with mixins or base
 classes, there are challenges in ensuring that the correct (most-derived) type
 is passed to the validator and operation functions. If a base class defines a
 SafeCall wrapper and uses itself as the target, it may not have access to
 members or overrides in the derived class. This can lead to subtle bugs or
 validation failures, as the validator may not see the full state of the actual
 object.

 The Curiously Recurring Template Pattern (CRTP) is a technique that solves this
 by having the base template take the derived type as a template parameter. This
 allows the base to cast itself to the derived type, ensuring that all
 operations and validations are performed on the most-derived object.

 __Example:__

 \code
 // CRTP base for safe wrappers
 template <typename Derived>
 struct SafeBase {
     template <typename Func>
     auto SafeCall(Func&& func) {
         // Cast to Derived to ensure correct type for validation and operation
         return oxygen::SafeCall(
             *static_cast<Derived*>(this),
             &Derived::Validate,
             std::forward<Func>(func));
     }
 };

 struct MyComponent : SafeBase<MyComponent> {
     std::optional<std::string> Validate() const noexcept { ... }
     // ...other members...
 };
 \endcode

 This pattern ensures that SafeCall always operates on the full derived object,
 avoiding slicing and enabling correct access to overridden methods and members.

 \tparam TargetRef Reference type of the target object (const or non-const)
 \tparam Validator Callable type for validation (lambda, member function
         pointer, or function pointer)
 \tparam Func Callable type for the operation to execute on the target

 \param target The target object to validate and operate on
 \param validate Validation callable returning std::optional<std::string>
 \param func Operation callable to execute if validation passes

 \return std::optional where T is the operation's return type, or
         std::optional<std::monostate> for void operations. Returns std::nullopt
         if validation fails or an exception occurs.
*/
template <typename TargetRef, typename Validator, typename Func>
    requires std::invocable<Func, TargetRef>
    && std::is_nothrow_invocable_v<Func, TargetRef>
    && std::invocable<Validator, TargetRef>
auto SafeCall(TargetRef&& target, Validator&& validate, Func&& func) noexcept
{
    using ReturnType = std::invoke_result_t<Func, TargetRef>;
    using OptionalReturn = std::conditional_t<
        std::is_void_v<ReturnType>,
        std::optional<std::monostate>,
        std::optional<ReturnType>>;

    // Capture target by reference to call its member function
    auto fail = [&](const char* reason = nullptr) -> OptionalReturn {
        if constexpr (HasLogSafeCallError<std::remove_reference_t<TargetRef>>) {
            if (reason != nullptr) {
                target.LogSafeCallError(reason);
            }
        } else {
            (void)reason; // Avoid unused variable warning if no logging function exists
        }
        return std::nullopt;
    };

    // Support both member function pointers and lambdas
    std::optional<std::string> error;
    if constexpr (std::is_member_function_pointer_v<std::decay_t<Validator>>) {
        error = std::invoke(validate, target);
    } else {
        error = std::invoke(std::forward<Validator>(validate), target);
    }
    if (error.has_value()) {
        return fail(error->c_str());
    }

    try {
        if constexpr (std::is_void_v<ReturnType>) {
            std::invoke(std::forward<Func>(func), std::forward<TargetRef>(target));
            return std::make_optional(std::monostate {});
        } else {
            return std::make_optional(std::invoke(std::forward<Func>(func),
                std::forward<TargetRef>(target)));
        }
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
}

} // namespace oxygen
