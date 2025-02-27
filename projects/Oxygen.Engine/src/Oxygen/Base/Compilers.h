//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/*!
 * \file
 *
 * \brief Compiler detection, diagnostics and feature helpers.
 */

#pragma once

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

// ----------------------------------------------------------------------------
// Compiler detection
// ----------------------------------------------------------------------------

#if !defined(DOXYGEN_DOCUMENTATION_BUILD)

#  if defined(OXYGEN_VERSION_ENCODE_INTERNAL_)
#    undef OXYGEN_VERSION_ENCODE_INTERNAL_
#  endif
#  define OXYGEN_VERSION_ENCODE_INTERNAL_(major, minor, revision) \
      (((major) * 1000000) + ((minor) * 1000) + (revision))

#  if defined(OXYGEN_CLANG_VERSION)
#    undef OXYGEN_CLANG_VERSION
#  endif
#  if defined(__clang__)
#    define OXYGEN_CLANG_VERSION         \
        OXYGEN_VERSION_ENCODE_INTERNAL_( \
            __clang_major__, __clang_minor__, __clang_patchlevel__)
#  endif

#else // DOXYGEN_DOCUMENTATION_BUILD
/*!
 * \brief Clang compiler detection macro.
 *
 * This macro is only defined if the compiler in use is a Clang compiler
 * (including Apple Clang).
 *
 * Example
 * ```
 * #if defined(OXYGEN_CLANG_VERSION)
 * // Do things for clang
 * #endif
 * ```
 *
 * \see OXYGEN_CLANG_VERSION_CHECK
 */
#  define OXYGEN_CLANG_VERSION 0
#endif // DOXYGEN_DOCUMENTATION_BUILD

#if defined(OXYGEN_CLANG_VERSION_CHECK)
#  undef OXYGEN_CLANG_VERSION_CHECK
#endif
#if defined(OXYGEN_CLANG_VERSION)
#  define OXYGEN_CLANG_VERSION_CHECK(major, minor, patch) \
      (OXYGEN_CLANG_VERSION                               \
          >= OXYGEN_VERSION_ENCODE_INTERNAL_(major, minor, patch))
#else
#  define OXYGEN_CLANG_VERSION_CHECK(major, minor, patch) (0)
#endif
/*!
 * \def OXYGEN_CLANG_VERSION_CHECK
 *
 * \brief Clang compiler version check macro.
 *
 * This macro is always defined, and provides a convenient way to check for
 * features based on the version number.
 *
 * \note In most cases for clang, you should not test its version number, you
 * should use the feature checking macros
 * (https://clang.llvm.org/docs/LanguageExtensions.html#feature-checking-macros).
 *
 * Example
 * ```
 * #if OXYGEN_CLANG_VERSION_CHECK(14,0,0)
 * // Do a thing that only clang-14+ supports
 * #endif
 * ```
 *
 * \return true (1) if the current compiler corresponds to the macro name, and
 * the compiler version is greater than or equal to the provided values.
 *
 * \see OXYGEN_CLANG_VERSION
 */

#if !defined(DOXYGEN_DOCUMENTATION_BUILD)

#  if defined(OXYGEN_MSVC_VERSION)
#    undef OXYGEN_MSVC_VERSION
#  endif
#  if defined(_MSC_FULL_VER) && (_MSC_FULL_VER >= 140000000) && !defined(__ICL)
#    define OXYGEN_MSVC_VERSION                                   \
        OXYGEN_VERSION_ENCODE_INTERNAL_(_MSC_FULL_VER / 10000000, \
            (_MSC_FULL_VER % 10000000) / 100000,                  \
            (_MSC_FULL_VER % 100000) / 100)
#  elif defined(_MSC_FULL_VER) && !defined(__ICL)
#    define OXYGEN_MSVC_VERSION                                  \
        OXYGEN_VERSION_ENCODE_INTERNAL_(_MSC_FULL_VER / 1000000, \
            (_MSC_FULL_VER % 1000000) / 10000,                   \
            (_MSC_FULL_VER % 10000) / 10)
#  elif defined(_MSC_VER) && !defined(__ICL)
#    define OXYGEN_MSVC_VERSION \
        OXYGEN_VERSION_ENCODE_INTERNAL_(_MSC_VER / 100, _MSC_VER % 100, 0)
#  endif

#else // DOXYGEN_DOCUMENTATION_BUILD
/*!
 * \brief MSVC compiler detection macro.
 *
 * This macro is only defined if the compiler in use is Microsoft Visual Studio
 * C++ compiler.
 *
 * Example
 * ```
 * #if defined(OXYGEN_MSVC_VERSION)
 * // Do things for MSVC
 * #endif
 * ```
 *
 * \see OXYGEN_MSVC_VERSION_CHECK
 */
#  define OXYGEN_MSVC_VERSION 0
#endif // DOXYGEN_DOCUMENTATION_BUILD

#if defined(OXYGEN_MSVC_VERSION_CHECK)
#  undef OXYGEN_MSVC_VERSION_CHECK
#endif
#if !defined(OXYGEN_MSVC_VERSION)
#  define OXYGEN_MSVC_VERSION_CHECK(major, minor, patch) (0)
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
#  define OXYGEN_MSVC_VERSION_CHECK(major, minor, patch) \
      (_MSC_FULL_VER >= (((major) * 10000000) + ((minor) * 100000) + (patch)))
#elif defined(_MSC_VER) && (_MSC_VER >= 1200)
#  define OXYGEN_MSVC_VERSION_CHECK(major, minor, patch) \
      (_MSC_FULL_VER >= ((major * 1000000) + (minor * 10000) + (patch)))
#else
#  define OXYGEN_MSVC_VERSION_CHECK(major, minor, patch) \
      (_MSC_VER >= ((major * 100) + (minor)))
#endif
/*!
 * \def OXYGEN_MSVC_VERSION_CHECK
 * \brief MSVC compiler version check macro.
 *
 * This macro is always defined, and provides a convenient way to check for
 * features based on the version number.
 *
 * Example
 * ```
 * #if OXYGEN_MSVC_VERSION_CHECK(16,0,0)
 * // Do a thing that only MSVC 16+ supports
 * #endif
 * ```
 *
 * \return true (1) if the current compiler corresponds to the macro name, and
 * the compiler version is greater than or equal to the provided values.
 *
 * \see OXYGEN_MSVC_VERSION
 */

#if !defined(DOXYGEN_DOCUMENTATION_BUILD)

/*
 * Note that the GNUC and GCC macros are different. Many compilers masquerade as
 * GCC (by defining * __GNUC__, __GNUC_MINOR__, and __GNUC_PATCHLEVEL__), but
 * often do not implement all the features of the version of GCC they pretend to
 * support.
 *
 * To work around this, the OXYGEN_GCC_VERSION macro is only defined for GCC,
 * whereas OXYGEN_GNUC_VERSION will be defined whenever a compiler defines
 * __GCC__.
 */

#  if defined(OXYGEN_GNUC_VERSION)
#    undef OXYGEN_GNUC_VERSION
#  endif
#  if defined(__GNUC__) && defined(__GNUC_PATCHLEVEL__)
#    define OXYGEN_GNUC_VERSION          \
        OXYGEN_VERSION_ENCODE_INTERNAL_( \
            __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#  elif defined(__GNUC__)
#    define OXYGEN_GNUC_VERSION \
        OXYGEN_VERSION_ENCODE_INTERNAL_(__GNUC__, __GNUC_MINOR__, 0)
#  endif

#else // DOXYGEN_DOCUMENTATION_BUILD
/*!
 * \brief GNU like compiler detection macro.
 *
 * This macro is only defined if the compiler in use defines `__GNUC__`, which
 * may indicate that it is the real GCC compiler or a compiler masquerading GCC.
 *
 * Example
 * ```
 * #if defined(OXYGEN_GNUC_VERSION)
 * // Do things that work for GNU like compilers
 * #endif
 * ```
 *
 * \see OXYGEN_GNUC_VERSION_CHECK
 */
#  define OXYGEN_GNUC_VERSION 0
#endif // DOXYGEN_DOCUMENTATION_BUILD

#if defined(OXYGEN_GNUC_VERSION_CHECK)
#  undef OXYGEN_GNUC_VERSION_CHECK
#endif
#if defined(OXYGEN_GNUC_VERSION)
#  define OXYGEN_GNUC_VERSION_CHECK(major, minor, patch) \
      (OXYGEN_GNUC_VERSION                               \
          >= OXYGEN_VERSION_ENCODE_INTERNAL_(major, minor, patch))
#else
#  define OXYGEN_GNUC_VERSION_CHECK(major, minor, patch) (0)
#endif
/*!
 * \def OXYGEN_GNUC_VERSION_CHECK
 * \brief GNU like compiler version check macro.
 *
 * This macro is always defined, and provides a convenient way to check for
 * features based on the version number.
 *
 * Example
 * ```
 * #if OXYGEN_GNUC_VERSION_CHECK(9,0,0)
 * // Do a thing that only GNU-like compiler 9+ supports
 * #endif
 * ```
 *
 * \return true (1) if the current compiler corresponds to the macro name, and
 * the compiler version is greater than or equal to the provided values.
 *
 * \see OXYGEN_GNUC_VERSION
 */

#if !defined(DOXYGEN_DOCUMENTATION_BUILD)

#  if defined(OXYGEN_GCC_VERSION)
#    undef OXYGEN_GCC_VERSION
#  endif
#  if defined(OXYGEN_GNUC_VERSION) && !defined(__clang__)
#    define OXYGEN_GCC_VERSION OXYGEN_GNUC_VERSION
#  endif

#else // DOXYGEN_DOCUMENTATION_BUILD
/*!
 * \brief GCC compiler detection macro.
 *
 * This macro is only defined if the compiler in use is GNU C/C++ compiler. Any
 * other compilers that masquerade as `__GNUC__` but define another known
 * compiler symbol are excluded.
 *
 * Example
 * ```
 * #if defined(OXYGEN_GCC_VERSION)
 * // Do things for GCC/G++
 * #endif
 * ```
 *
 * \see OXYGEN_GCC_VERSION_CHECK
 */
#  define OXYGEN_GCC_VERSION 0
#endif // DOXYGEN_DOCUMENTATION_BUILD

#if defined(OXYGEN_GCC_VERSION_CHECK)
#  undef OXYGEN_GCC_VERSION_CHECK
#endif
#if defined(OXYGEN_GCC_VERSION)
#  define OXYGEN_GCC_VERSION_CHECK(major, minor, patch) \
      (OXYGEN_GCC_VERSION >= OXYGEN_VERSION_ENCODE_INTERNAL_(major, minor, patch))
#else
#  define OXYGEN_GCC_VERSION_CHECK(major, minor, patch) (0)
#endif
/*!
 * \def OXYGEN_GCC_VERSION_CHECK
 * \brief GCC/G++ compiler version check macro.
 *
 * This macro is always defined, and provides a convenient way to check for
 * features based on the version number.
 *
 * Example
 * ```
 * #if OXYGEN_GCC_VERSION_CHECK(11,0,0)
 * // Do a thing that only GCC 11+ supports
 * #endif
 * ```
 *
 * \return true (1) if the current compiler corresponds to the macro name, and
 * the compiler version is greater than or equal to the provided values.
 *
 * \see OXYGEN_GCC_VERSION
 */

// -----------------------------------------------------------------------------
// Compiler attribute check
// -----------------------------------------------------------------------------

#if defined(OXYGEN_HAS_ATTRIBUTE)
#  undef OXYGEN_HAS_ATTRIBUTE
#endif
#if defined(__has_attribute)
#  define OXYGEN_HAS_ATTRIBUTE(attribute) __has_attribute(attribute)
#else
#  define OXYGEN_HAS_ATTRIBUTE(attribute) (0)
#endif
/*!
 * \def OXYGEN_HAS_ATTRIBUTE
 * \brief Checks for the presence of an attribute named by `attribute`.
 *
 * Example
 * ```
 * #if OXYGEN_HAS_ATTRIBUTE(deprecated) // Check for an attribute
 * #  define DEPRECATED(msg) [[deprecated(msg)]]
 * #else
 * #  define DEPRECATED(msg)
 * #endif
 *
 * DEPRECATED("foo() has been deprecated") void foo();
 * ```
 *
 * \return non-zero value if the attribute is supported by the compiler.
 */

#if defined(OXYGEN_HAS_CPP_ATTRIBUTE)
#  undef OXYGEN_HAS_CPP_ATTRIBUTE
#endif
#if defined(__has_cpp_attribute) && defined(__cplusplus)
#  define OXYGEN_HAS_CPP_ATTRIBUTE(attribute) __has_cpp_attribute(attribute)
#else
#  define OXYGEN_HAS_CPP_ATTRIBUTE(attribute) (0)
#endif
/*!
 * \def OXYGEN_HAS_CPP_ATTRIBUTE
 * \brief Checks for the presence of a C++ compiler attribute named by
 * `attribute`.
 *
 * Example
 * ```
 * #if OXYGEN_HAS_CPP_ATTRIBUTE(nodiscard)
 * [[nodiscard]]
 * #endif
 * int foo(int i) { return i * i; }
 * ```
 *
 * \return non-zero value if the attribute is supported by the compiler.
 */

// -----------------------------------------------------------------------------
// Compiler builtin check
// -----------------------------------------------------------------------------

#if defined(OXYGEN_HAS_BUILTIN)
#  undef OXYGEN_HAS_BUILTIN
#endif
#if defined(__has_builtin)
#  define OXYGEN_HAS_BUILTIN(builtin) __has_builtin(builtin)
#else
#  define OXYGEN_HAS_BUILTIN(builtin) (0)
#endif
/*!
 * \def OXYGEN_HAS_BUILTIN
 * \brief Checks for the presence of a compiler builtin function named by
 * `builtin`.
 *
 * Example
 * ```
 * #if OXYGEN_HAS_BUILTIN(__builtin_trap)
 *   __builtin_trap();
 * #else
 *   abort();
 * #endif
 * ```
 *
 * \return non-zero value if the builtin function is supported by the compiler.
 */

// -----------------------------------------------------------------------------
// Compiler feature check
// -----------------------------------------------------------------------------

#if defined(OXYGEN_HAS_FEATURE)
#  undef OXYGEN_HAS_FEATURE
#endif
#if defined(__has_feature)
#  define OXYGEN_HAS_FEATURE(feature) __has_feature(feature)
#else
#  define OXYGEN_HAS_FEATURE(feature) (0)
#endif
/*!
 * \def OXYGEN_HAS_FEATURE
 * \brief Checks for the presence of a compiler feature named by `feature`.
 *
 * Example
 * ```
 * #if OXYGEN_HAS_FEATURE(attribute_overloadable) || OXYGEN_HAS_FEATURE(blocks)
 * ...
 * #endif
 * ```
 *
 * \return non-zero value if the feature is supported by the compiler.
 */

// -----------------------------------------------------------------------------
// Pragma
// -----------------------------------------------------------------------------

#if (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)) \
    || defined(__clang__) || OXYGEN_GCC_VERSION_CHECK(3, 0, 0)
#  define OXYGEN_PRAGMA(value) _Pragma(#value)
#elif OXYGEN_MSVC_VERSION_CHECK(15, 0, 0)
#  define OXYGEN_PRAGMA(value) __pragma(value)
#else
#  define OXYGEN_PRAGMA(value)
#endif
/*!
 * \def OXYGEN_PRAGMA
 *
 * \brief Produce a `pragma` directive for the compiler.
 *
 * Pragma directives specify machine-specific or operating system-specific
 * compiler features. There are different ways compilers support the
 * specification of pragmas and this macro will simply abstract these ways in a
 * single generic way.
 */

// -----------------------------------------------------------------------------
// Compiler diagnostics
// -----------------------------------------------------------------------------

#if defined(OXYGEN_DIAGNOSTIC_PUSH)
#  undef OXYGEN_DIAGNOSTIC_PUSH
#endif
#if defined(OXYGEN_DIAGNOSTIC_POP)
#  undef OXYGEN_DIAGNOSTIC_POP
#endif
#if defined(OXYGEN_DIAGNOSTIC_DISABLE)
#  undef OXYGEN_DIAGNOSTIC_DISABLE
#endif

#if defined(__clang__)
#  define OXYGEN_DIAGNOSTIC_PUSH _Pragma("clang diagnostic push")
#  define OXYGEN_DIAGNOSTIC_POP _Pragma("clang diagnostic pop")
#  define OXYGEN_DIAGNOSTIC_DISABLE(id) \
      _Pragma("clang diagnostic ignored \"" #id "\"")
#elif OXYGEN_GCC_VERSION_CHECK(4, 6, 0)
#  define OXYGEN_DIAGNOSTIC_PUSH _Pragma("GCC diagnostic push")
#  define OXYGEN_DIAGNOSTIC_POP _Pragma("GCC diagnostic pop")
#  define OXYGEN_DIAGNOSTIC_DISABLE(id) \
      _Pragma("GCC diagnostic ignored \"" #id "\"")
#elif OXYGEN_MSVC_VERSION_CHECK(15, 0, 0)
#  define OXYGEN_DIAGNOSTIC_PUSH __pragma(warning(push))
#  define OXYGEN_DIAGNOSTIC_POP __pragma(warning(pop))
#  define OXYGEN_DIAGNOSTIC_DISABLE(id) __pragma(warning(disable : id))
#else
#  define OXYGEN_DIAGNOSTIC_PUSH
#  define OXYGEN_DIAGNOSTIC_POP
#  define OXYGEN_DIAGNOSTIC_DISABLE(id)
#endif
/*!
 * \def OXYGEN_DIAGNOSTIC_PUSH
 *
 * \brief Remember the current state of the compiler's diagnostics.
 *
 * Example
 * ```
 * OXYGEN_DIAGNOSTIC_PUSH
 * #if defined(__clang__) && OXYGEN_HAS_WARNING("-Wunused-const-variable")
 * #pragma clang diagnostic ignored "-Wunused-const-variable"
 * #endif
 * const char *const FOO = "foo";
 * OXYGEN_DIAGNOSTIC_POP
 * ```
 *
 * \see OXYGEN_DIAGNOSTIC_POP
 */

/*!
 * \def OXYGEN_DIAGNOSTIC_POP
 *
 * \brief Return the state of the compiler's diagnostics to the value from the
 * last push.
 *
 * Example
 * ```
 * OXYGEN_DIAGNOSTIC_PUSH
 * #if defined(__clang__) && OXYGEN_HAS_WARNING("-Wunused-const-variable")
 * #pragma clang diagnostic ignored "-Wunused-const-variable"
 * #endif
 * const char *const FOO = "foo";
 * OXYGEN_DIAGNOSTIC_POP
 * ```
 *
 * \see OXYGEN_DIAGNOSTIC_PUSH
 */

/*!
 * \def OXYGEN_DIAGNOSTIC_DISABLE
 *
 * \brief Disable a specific diagnostic warning for the compiler.
 *
 * This macro disables a specific diagnostic warning for the supported compilers
 * (Clang, GCC, and MSVC).
 *
 * Example
 * ```
 * OXYGEN_DIAGNOSTIC_PUSH
 * OXYGEN_DIAGNOSTIC_DISABLE(26495) // For MSVC
 * OXYGEN_DIAGNOSTIC_DISABLE(-Wpadded) // For Clang
 * OXYGEN_DIAGNOSTIC_DISABLE(-Wuninitialized) // For GCC
 * // Your code here
 * OXYGEN_DIAGNOSTIC_POP
 * ```
 *
 * \see OXYGEN_DIAGNOSTIC_PUSH
 * \see OXYGEN_DIAGNOSTIC_POP
 */

#if defined(OXYGEN_HAS_WARNING)
#  undef OXYGEN_HAS_WARNING
#endif
#if defined(__has_warning)
#  define OXYGEN_HAS_WARNING(warning) __has_warning(warning)
#else
#  define OXYGEN_HAS_WARNING(warning) (0)
#endif
/*!
 * \def OXYGEN_HAS_WARNING
 * \brief Checks for the presence of a compiler warning named by `warning`.
 *
 * Example
 * ```
 * OXYGEN_DIAGNOSTIC_PUSH
 * #if defined(__clang__) && OXYGEN_HAS_WARNING("-Wunused-const-variable")
 * #pragma clang diagnostic ignored "-Wunused-const-variable"
 * #endif
 * const char *const FOO = "foo";
 * OXYGEN_DIAGNOSTIC_POP
 * ```
 *
 * \return non-zero value if the feature is supported by the compiler.
 */

// -----------------------------------------------------------------------------
// assume, unreachable, unreachable return
// -----------------------------------------------------------------------------

#if defined(OXYGEN_UNREACHABLE)
#  undef OXYGEN_UNREACHABLE
#endif
#if defined(OXYGEN_UNREACHABLE_RETURN)
#  undef OXYGEN_UNREACHABLE_RETURN
#endif
#if defined(OXYGEN_ASSUME)
#  undef OXYGEN_ASSUME
#endif
#if OXYGEN_MSVC_VERSION_CHECK(13, 10, 0)
#  define OXYGEN_ASSUME(expr) __assume(expr)
#elif OXYGEN_HAS_BUILTIN(__builtin_assume)
#  define OXYGEN_ASSUME(expr) __builtin_assume(expr)
#endif
#if OXYGEN_HAS_BUILTIN(__builtin_unreachable) \
    || OXYGEN_GCC_VERSION_CHECK(4, 5, 0)
#  define OXYGEN_UNREACHABLE() __builtin_unreachable()
#elif defined(OXYGEN_ASSUME)
#  define OXYGEN_UNREACHABLE() OXYGEN_ASSUME(0)
#endif
#if !defined(OXYGEN_ASSUME)
#  if defined(OXYGEN_UNREACHABLE)
#    define OXYGEN_ASSUME(expr) \
        OXYGEN_STATIC_CAST(void, ((expr) ? 1 : (OXYGEN_UNREACHABLE(), 1)))
#  else
#    define OXYGEN_ASSUME(expr) OXYGEN_STATIC_CAST(void, expr)
#  endif
#endif
#if defined(OXYGEN_UNREACHABLE)
#  define OXYGEN_UNREACHABLE_RETURN(value) OXYGEN_UNREACHABLE()
#else
#  define OXYGEN_UNREACHABLE_RETURN(value) return (value)
#endif
#if !defined(OXYGEN_UNREACHABLE)
#  define OXYGEN_UNREACHABLE() OXYGEN_ASSUME(0)
#endif

/*!
 * \def OXYGEN_UNREACHABLE
 *
 * \brief Inform the compiler/analyzer that the code should never be reached
 * (even with invalid input).
 *
 * Example
 * ```
 * switch (foo) {
 * 	case BAR:
 * 		do_something();
 * 		break;
 * 	case BAZ:
 * 		do_something_else();
 * 		break;
 * 	default:
 * 		OXYGEN_UNREACHABLE();
 * 		break;
 * }
 * ```
 * \see OXYGEN_UNREACHABLE_RETURN
 */

/*!
 * \def OXYGEN_UNREACHABLE_RETURN
 *
 * \brief Inform the compiler/analyzer that the code should never be reached or,
 * for compilers which don't provide a way to provide such information, return a
 * value.
 *
 * Example
 * ```
 * static int handle_code(enum Foo code) {
 *   switch (code) {
 *   case FOO_BAR:
 *   case FOO_BAZ:
 *   case FOO_QUX:
 *     return 0;
 *   }
 *
 * OXYGEN_UNREACHABLE_RETURN(0);
 * }
 * ```
 * \see OXYGEN_UNREACHABLE
 */

/*!
 * \def OXYGEN_ASSUME
 *
 * \brief Inform the compiler/analyzer that the provided expression should
 * always evaluate to a non-false value.
 *
 * Note that the compiler is free to assume that the expression never evaluates
 * to true and therefore can elide code paths where it does evaluate to true.
 *
 * Example
 * ```
 * unsigned sum_small(unsigned data[], size_t count) {
 *   __builtin_assume(count <= 4);
 *   unsigned sum = 0;
 *   for (size_t i = 0; i < count; ++i) {
 *     sum += data[i];
 *   }
 *   return sum;
 * }
 * ```
 */

// -----------------------------------------------------------------------------
// fallthrough
// -----------------------------------------------------------------------------

#if defined(OXYGEN_FALL_THROUGH)
#  undef OXYGEN_FALL_THROUGH
#endif
#if OXYGEN_HAS_ATTRIBUTE(fallthrough) || OXYGEN_GCC_VERSION_CHECK(7, 0, 0)
#  define OXYGEN_FALL_THROUGH __attribute__((__fallthrough__))
#else
#  define OXYGEN_FALL_THROUGH
#endif

/*!
 * \def OXYGEN_FALL_THROUGH
 *
 * \brief Explicitly tell the compiler to fall through a case in the switch
 * statement. Without this, some compilers may think you accidentally omitted a
 * "break;" and emit a diagnostic.
 *
 * Example
 * ```
 * switch (foo) {
 * 	case FOO:
 * 		handle_foo();
 * 		OXYGEN_FALL_THROUGH;
 * 	case BAR:
 * 		handle_bar();
 * 		break;
 * }
 * ```
 */

// NOLINTEND(cppcoreguidelines-macro-usage)
