//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Composition/TypeSystem.h"

#include <ranges>
#include <thread>
#include <unordered_set>

#include <Oxygen/Testing/GTest.h>

extern "C" bool initialize_called;

namespace {

class TypeSystemTests : public testing::Test {
public:
    TypeSystemTests()
        : registry_(oxygen::TypeRegistry::Get())
    {
    }

protected:
    oxygen::TypeRegistry& registry_;
};

NOLINT_TEST_F(TypeSystemTests, UsesMainInitializer)
{
    EXPECT_TRUE(initialize_called);
}

NOLINT_TEST_F(TypeSystemTests, CanRegisterAndGetTypes)
{
    const auto id = registry_.RegisterType("test::MyType");
    EXPECT_EQ(id, registry_.GetTypeId("test::MyType"));
}

NOLINT_TEST_F(TypeSystemTests, HandlesBadInput)
{
    EXPECT_THROW(registry_.RegisterType(nullptr), std::invalid_argument);
    EXPECT_THROW(registry_.RegisterType(""), std::invalid_argument);
}

NOLINT_TEST_F(TypeSystemTests, DoubleRegistrationReturnsSameId)
{
    const auto id = registry_.RegisterType("test::MyType");
    EXPECT_EQ(id, registry_.RegisterType("test::MyType"));
}

NOLINT_TEST_F(TypeSystemTests, TypeNotRegistered)
{
    EXPECT_THROW(registry_.GetTypeId("NotThere"), std::invalid_argument);
}

NOLINT_TEST_F(TypeSystemTests, ThreadSafety)
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this, i] {
            const auto typeName = "test::Type" + std::to_string(i);
            const auto id = registry_.RegisterType(typeName.c_str());
            EXPECT_EQ(id, registry_.GetTypeId(typeName.c_str()));
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
}

NOLINT_TEST_F(TypeSystemTests, LongTypeName)
{
    const std::string longTypeName(1000, 'a');
    const auto id = registry_.RegisterType(longTypeName.c_str());
    EXPECT_EQ(id, registry_.GetTypeId(longTypeName.c_str()));
}

NOLINT_TEST_F(TypeSystemTests, StressTest)
{
    std::unordered_map<std::string, oxygen::TypeId> registeredTypes;
    constexpr int numTypes = 10'000;

    // Generate and register 10,000 random strings as types
    for (int i = 0; i < numTypes; ++i) {
        std::string typeName = "test::Type" + std::to_string(i);
        const auto id = registry_.RegisterType(typeName.c_str());
        registeredTypes[typeName] = id;
    }

    // Verify that each type id is unique
    std::unordered_set<oxygen::TypeId> uniqueIds;
    for (const auto& id : registeredTypes | std::views::values) {
        EXPECT_TRUE(uniqueIds.insert(id).second);
    }

    // Randomly get type id by name from the collection of types we registered
    for (const auto& [typeName, id] : registeredTypes) {
        EXPECT_EQ(id, registry_.GetTypeId(typeName.c_str()));
    }
}
} // namespace
