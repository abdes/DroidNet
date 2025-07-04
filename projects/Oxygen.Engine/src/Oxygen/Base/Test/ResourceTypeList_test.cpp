//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Base/TypeList.h>

using oxygen::IndexOf;
using oxygen::ResourceHandle;
using oxygen::TypeList;

namespace {

template <typename T, typename ResourceTypeList>
constexpr auto GetResourceTypeId() noexcept -> ResourceHandle::ResourceTypeT
{
  return static_cast<ResourceHandle::ResourceTypeT>(
    IndexOf<T, ResourceTypeList>::value);
}

//=== ResourceTypeId Tests ===----------------------------------------------//

//! Test correct ID assignment for types in the list.
NOLINT_TEST(ResourceTypeIdTest, CorrectIdAssignment)
{
  // Arrange
  class A { };
  class B { };
  class C { };
  using MyTypeList = oxygen::TypeList<A, B, C>;

  // Act & Assert
  EXPECT_EQ((GetResourceTypeId<A, MyTypeList>()), 0);
  EXPECT_EQ((GetResourceTypeId<B, MyTypeList>()), 1);
  EXPECT_EQ((GetResourceTypeId<C, MyTypeList>()), 2);
}

//! Test ID stability when appending new types.
NOLINT_TEST(ResourceTypeIdTest, IdStabilityOnAppend)
{
  // Arrange
  class A { };
  class B { };
  class C { };
  class D { };
  using MyTypeList = oxygen::TypeList<A, B, C>;
  using ExtendedList = oxygen::TypeList<A, B, C, D>;

  // Act & Assert
  EXPECT_EQ((GetResourceTypeId<A, MyTypeList>()),
    (GetResourceTypeId<A, ExtendedList>()));
  EXPECT_EQ((GetResourceTypeId<B, MyTypeList>()),
    (GetResourceTypeId<B, ExtendedList>()));
  EXPECT_EQ((GetResourceTypeId<C, MyTypeList>()),
    (GetResourceTypeId<C, ExtendedList>()));
  EXPECT_EQ((GetResourceTypeId<D, ExtendedList>()), 3);
}

//! Test constexpr usability of GetResourceTypeId.
NOLINT_TEST(ResourceTypeIdTest, ConstexprUsability)
{
  // Arrange
  class A { };
  class B { };
  using MyTypeList = oxygen::TypeList<A, B>;

  // Act
  constexpr auto id_b = GetResourceTypeId<B, MyTypeList>();

  // Assert
  static_assert(
    id_b == 1, "GetResourceTypeId must be usable in constexpr context");
  EXPECT_EQ(id_b, 1);
}

//! Test GetResourceTypeId works with forward-declared types.
NOLINT_TEST(ResourceTypeIdTest, WorksWithForwardDeclarations)
{
  // Arrange
  class Fwd;
  using FwdList = oxygen::TypeList<Fwd>;

  // Act & Assert
  EXPECT_EQ((GetResourceTypeId<Fwd, FwdList>()), 0);
}

//! Test that only exact types in the list are accepted (not derived types).
NOLINT_TEST(ResourceTypeIdTest, OnlyExactTypeAccepted)
{
  // Arrange
  class Base { };
  class Derived : public Base { };
  using MyTypeList = oxygen::TypeList<Base>;

  // Act & Assert
  EXPECT_EQ((GetResourceTypeId<Base, MyTypeList>()), 0);
  // The following line should fail to compile if uncommented:
  // EXPECT_EQ(GetResourceTypeId<Derived, MyTypeList>(), 0);
}

//! Test that requesting an ID for a type not in the list fails to compile
//! (documented).
/*
NOLINT_TEST(ResourceTypeIdTest, CompileErrorForMissingType)
{
  // Arrange
  class A { };
  class B { };
  using MyTypeList = oxygen::TypeList<A>;

  // Act & Assert
  // The following line should fail to compile:
  GetResourceTypeId<B, MyTypeList>();
}
*/

} // namespace
