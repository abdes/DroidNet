//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

#include <ranges>
#include <thread>
#include <vector>

#include "Oxygen/Composition/Composition.h"
#include "Oxygen/Composition/ComponentMacros.h"
#include "Oxygen/Composition/Object.h"

namespace {

// Test Components
class SimpleComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(SimpleComponent)
};

// Test Components
class BetterComponent final : public oxygen::Component {
    OXYGEN_COMPONENT(BetterComponent)
public:
    explicit BetterComponent(const int value)
        : value_(value)
    {
    }

    [[nodiscard]] auto Value() const -> int { return value_; }

private:
    int value_ { 0 };
};

class DependentComponent final : public oxygen::Component {
    OXYGEN_TYPED(DependentComponent)
    OXYGEN_COMPONENT_REQUIRES(SimpleComponent)
public:
    void UpdateDependencies(const oxygen::Composition& composition) override
    {
        simple_ = &composition.GetComponent<SimpleComponent>();
    }

    SimpleComponent* simple_ { nullptr };
};

class TestComposition : public oxygen::Composition {
public:
    using Base = Composition;

    template <typename T, typename... Args>
    auto AddComponent(Args&&... args) -> T&
    {
        return Base::AddComponent<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    void RemoveComponent()
    {
        Base::RemoveComponent<T>();
    }

    template <typename OldT, typename NewT = OldT, typename... Args>
    // ReSharper disable once CppDFAConstantParameter
    auto ReplaceComponent(Args&&... args) -> NewT&
    {
        return Base::ReplaceComponent<OldT, NewT>(std::forward<Args>(args)...);
    }

    template <typename T>
    [[nodiscard]] auto HasComponent() const -> bool
    {
        return Base::HasComponent<T>();
    }

    template <typename T>
    [[nodiscard]] auto GetComponent() const -> T&
    {
        return Base::GetComponent<T>();
    }

    auto Value() const noexcept -> std::optional<int>
    {
        if (HasComponent<BetterComponent>()) {
            return GetComponent<BetterComponent>().Value();
        }
        return std::nullopt;
    }
};

class CompositionTest : public testing::Test {
protected:
    TestComposition composition_;
};

// Test for empty composition operations
NOLINT_TEST_F(CompositionTest, EmptyCompositionOperations)
{
    EXPECT_FALSE(composition_.HasComponent<SimpleComponent>());
    NOLINT_EXPECT_THROW([[maybe_unused]] auto _ = composition_.GetComponent<SimpleComponent>(), oxygen::ComponentError);
    EXPECT_NO_THROW(composition_.RemoveComponent<SimpleComponent>());
}

// Basic Operations Tests
NOLINT_TEST_F(CompositionTest, AddAndVerifyComponent)
{
    auto& component = composition_.AddComponent<SimpleComponent>();
    EXPECT_TRUE(composition_.HasComponent<SimpleComponent>());
    EXPECT_EQ(&component, &composition_.GetComponent<SimpleComponent>());
}

NOLINT_TEST_F(CompositionTest, RemoveComponent)
{
    composition_.AddComponent<SimpleComponent>();
    composition_.RemoveComponent<SimpleComponent>();
    EXPECT_FALSE(composition_.HasComponent<SimpleComponent>());
}

// Copy/Move Tests
NOLINT_TEST_F(CompositionTest, CopyConstructor)
{
    composition_.AddComponent<SimpleComponent>();
    const TestComposition copy(composition_);
    EXPECT_TRUE(copy.HasComponent<SimpleComponent>());
}

NOLINT_TEST_F(CompositionTest, MoveConstructor)
{
    composition_.AddComponent<SimpleComponent>();
    const TestComposition moved(std::move(composition_));
    EXPECT_TRUE(moved.HasComponent<SimpleComponent>());
}

// Dependency Tests
NOLINT_TEST_F(CompositionTest, DependencyValidation)
{
    composition_.AddComponent<SimpleComponent>();
    EXPECT_NO_THROW(composition_.AddComponent<DependentComponent>());
    const auto& dependent = composition_.GetComponent<DependentComponent>();
    EXPECT_NE(dependent.simple_, nullptr);
}

NOLINT_TEST_F(CompositionTest, MissingDependencyThrows)
{
    EXPECT_THROW(composition_.AddComponent<DependentComponent>(), oxygen::ComponentError);
}

namespace destruction {
    std::vector<std::string> order;

    class DependencyComponent final : public oxygen::Component {
        OXYGEN_COMPONENT(DependencyComponent)
    public:
        ~DependencyComponent() override
        {
            order.emplace_back(ClassTypeName());
        }
    };

    class DependentComponent final : public oxygen::Component {
        OXYGEN_COMPONENT(DependentComponent)
        OXYGEN_COMPONENT_REQUIRES(DependencyComponent)
    public:
        ~DependentComponent() override
        {
            order.emplace_back(ClassTypeName());
        }
    };
} // namespace destruction

NOLINT_TEST_F(CompositionTest, ComponentsDestroyedInReverseOrder)
{
    {
        destruction::order.clear();
        TestComposition comp;

        // Add in dependency order (dependencies first)
        comp.AddComponent<destruction::DependencyComponent>();
        comp.AddComponent<destruction::DependentComponent>();
    } // Composition destroyed here

    ASSERT_EQ(destruction::order.size(), 2);
    EXPECT_EQ(destruction::order[0], destruction::DependentComponent::ClassTypeName()); // Dependent destroyed first
    EXPECT_EQ(destruction::order[1], destruction::DependencyComponent::ClassTypeName()); // Dependency destroyed last
}

// Test for complex dependency chains
class ComplexComponent final : public oxygen::Component {
    OXYGEN_TYPED(ComplexComponent)
    OXYGEN_COMPONENT_REQUIRES(SimpleComponent, DependentComponent)
public:
    void UpdateDependencies(const oxygen::Composition& composition) override
    {
        simple_ = &composition.GetComponent<SimpleComponent>();
        dependent_ = &composition.GetComponent<DependentComponent>();
    }

    SimpleComponent* simple_ { nullptr };
    DependentComponent* dependent_ { nullptr };
};

NOLINT_TEST_F(CompositionTest, ComplexDependencyChains)
{
    composition_.AddComponent<SimpleComponent>();
    composition_.AddComponent<DependentComponent>();
    EXPECT_NO_THROW(composition_.AddComponent<ComplexComponent>());
    const auto& complex = composition_.GetComponent<ComplexComponent>();
    EXPECT_NE(complex.simple_, nullptr);
    EXPECT_NE(complex.dependent_, nullptr);
}

// Error Cases
NOLINT_TEST_F(CompositionTest, DuplicateComponentThrows)
{
    composition_.AddComponent<SimpleComponent>();
    EXPECT_THROW(composition_.AddComponent<SimpleComponent>(), oxygen::ComponentError);
}

NOLINT_TEST_F(CompositionTest, RemoveRequiredComponentThrows)
{
    composition_.AddComponent<SimpleComponent>();
    composition_.AddComponent<DependentComponent>();
    EXPECT_THROW(composition_.RemoveComponent<SimpleComponent>(), oxygen::ComponentError);
}

NOLINT_TEST_F(CompositionTest, ThreadSafetyCoordinatedOperations)
{
    constexpr int READER_COUNT = 4;
    constexpr int WRITER_COUNT = 2;

    std::atomic activeReaders { 0 };
    std::atomic writerActive { false };
    std::atomic phase { 0 };
    std::atomic start { false };
    std::vector<std::thread> threads;

    // Phase 1: Multiple readers
    // Phase 2: Single writer, multiple readers
    // Phase 3: Multiple writers, no readers
    // Phase 4: Mixed read/write with coordination

    // Reader threads with coordination
    for (int i = 0; i < READER_COUNT; ++i) {
        threads.emplace_back([&] {
            while (!start) {
                std::this_thread::yield();
            }

            while (phase < 4) {
                if (phase == 0 || phase == 1) { // Read-heavy phases
                    EXPECT_FALSE(writerActive) << "Writer active during read phase";
                    EXPECT_NO_THROW({
                        if (composition_.HasComponent<SimpleComponent>()) {
                            [[maybe_unused]] auto& comp = composition_.GetComponent<SimpleComponent>();
                            std::this_thread::sleep_for(std::chrono::microseconds(10));
                        }
                    });

                    --activeReaders;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // Writer threads with coordination
    for (int i = 0; i < WRITER_COUNT; ++i) {
        threads.emplace_back([&] {
            while (!start) {
                std::this_thread::yield();
            }

            while (phase < 4) {
                if (phase == 2 || phase == 3) { // Write-heavy phases
                    while (activeReaders > 0) {
                        std::this_thread::yield();
                    }

                    if (bool expected = false; writerActive.compare_exchange_strong(expected, true)) {
                        EXPECT_NO_THROW({
                            if (composition_.HasComponent<SimpleComponent>()) {
                                composition_.RemoveComponent<SimpleComponent>();
                            } else {
                                composition_.AddComponent<SimpleComponent>();
                            }
                        });
                        writerActive = false;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // Phase controller thread
    threads.emplace_back([&] {
        start = true;
        for (int i = 0; i < 4; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ++phase;
        }
    });

    for (auto& thread : threads) {
        thread.join();
    }
}

// Component Replacement Tests
NOLINT_TEST_F(CompositionTest, ReplaceComponent)
{
    auto& original = composition_.AddComponent<SimpleComponent>();
    auto& replaced = composition_.ReplaceComponent<SimpleComponent>();
    EXPECT_NE(&original, &replaced);
}
NOLINT_TEST_F(CompositionTest, ReplaceComponentWithNewType)
{
    [[maybe_unused]] auto& original = composition_.AddComponent<SimpleComponent>();
    EXPECT_FALSE(composition_.Value());
    const auto& replaced = composition_.ReplaceComponent<SimpleComponent, BetterComponent>(10);
    EXPECT_EQ(replaced.Value(), 10);
}

// Component Manager Tests
NOLINT_TEST_F(CompositionTest, ComponentManagerOperations)
{
    composition_.AddComponent<SimpleComponent>();
    EXPECT_TRUE(composition_.HasComponent<SimpleComponent>());
    composition_.RemoveComponent<SimpleComponent>();
    EXPECT_FALSE(composition_.HasComponent<SimpleComponent>());
}

// Error Recovery Tests
NOLINT_TEST_F(CompositionTest, GetNonExistentComponent)
{
    EXPECT_THROW([[maybe_unused]] auto& _ = composition_.GetComponent<SimpleComponent>(), oxygen::ComponentError);
}

// Multiple Component Tests
NOLINT_TEST_F(CompositionTest, MultipleComponents)
{
    composition_.AddComponent<SimpleComponent>();
    EXPECT_NO_THROW(composition_.AddComponent<DependentComponent>());
    EXPECT_TRUE(composition_.HasComponent<SimpleComponent>());
    EXPECT_TRUE(composition_.HasComponent<DependentComponent>());
}
// Test Components
class NonCloneableComponent final : public oxygen::Component {
    OXYGEN_TYPED(NonCloneableComponent)
};

class CloneableComponent final : public oxygen::Component {
    OXYGEN_TYPED(CloneableComponent)
public:
    [[nodiscard]] auto IsCloneable() const noexcept -> bool override { return true; }
    [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
    {
        return std::make_unique<CloneableComponent>(*this);
    }
};

class CloneableDependentComponent final : public oxygen::Component {
    OXYGEN_TYPED(CloneableDependentComponent)
    OXYGEN_COMPONENT_REQUIRES(CloneableComponent)
public:
    [[nodiscard]] auto IsCloneable() const noexcept -> bool override { return true; }
    [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
    {
        return std::make_unique<CloneableDependentComponent>(*this);
    }
    void UpdateDependencies(const oxygen::Composition& composition) override
    {
        dependency_ = &composition.GetComponent<CloneableComponent>();
    }

    CloneableComponent* dependency_ { nullptr };
};

class CloneableComposition final
    : public TestComposition,
      public oxygen::CloneableMixin<CloneableComposition> {
};

class CompositionCloningTest : public testing::Test {
protected:
    CloneableComposition composition_;
};

// Test Cases
NOLINT_TEST_F(CompositionCloningTest, CloneableComponentsSupport)
{
    composition_.AddComponent<CloneableComponent>();
    const auto clone { composition_.Clone() };
    EXPECT_TRUE(clone->HasComponent<CloneableComponent>());

    clone->RemoveComponent<CloneableComponent>();
    EXPECT_FALSE(clone->HasComponent<CloneableComponent>());
    EXPECT_TRUE(composition_.HasComponent<CloneableComponent>());
}

NOLINT_TEST_F(CompositionCloningTest, NonCloneableComponentPreventsCloning)
{
    composition_.AddComponent<NonCloneableComponent>();
    composition_.AddComponent<CloneableComponent>();

    EXPECT_THROW(auto clone { composition_.Clone() }, oxygen::ComponentError);
}

NOLINT_TEST_F(CompositionCloningTest, ClonedComponentsHaveUpdatedDependencies)
{
    composition_.AddComponent<CloneableComponent>();
    EXPECT_NO_THROW({
        composition_.AddComponent<CloneableDependentComponent>();
    });

    const auto clone { composition_.Clone() };
    EXPECT_TRUE(clone->HasComponent<CloneableComponent>());
    EXPECT_TRUE(clone->HasComponent<CloneableDependentComponent>());

    const auto& dependent = clone->GetComponent<CloneableDependentComponent>();
    EXPECT_NE(dependent.dependency_, nullptr);
}

NOLINT_TEST_F(CompositionTest, IterateEmptyComposition)
{
    EXPECT_EQ(composition_.begin(), composition_.end());
    EXPECT_EQ(composition_.cbegin(), composition_.cend());
}

NOLINT_TEST_F(CompositionTest, IterateSingleComponent)
{
    auto& simple = composition_.AddComponent<SimpleComponent>();

    auto it = composition_.begin();
    EXPECT_EQ(&*it, &simple);
    ++it;
    EXPECT_EQ(it, composition_.end());
}

NOLINT_TEST_F(CompositionTest, IterateMultipleComponents)
{
    [[maybe_unused]] auto& simple = composition_.AddComponent<SimpleComponent>();
    [[maybe_unused]] auto& dependent = composition_.AddComponent<DependentComponent>();
    [[maybe_unused]] auto& cloneable = composition_.AddComponent<CloneableComponent>();

    int count = 0;
    for (auto& component : composition_) {
        EXPECT_TRUE((std::is_same_v<decltype(component), oxygen::Component&>));
        ++count;
    }
    EXPECT_EQ(count, 3);
}

NOLINT_TEST_F(CompositionTest, IterateConstComposition)
{
    composition_.AddComponent<SimpleComponent>();
    composition_.AddComponent<CloneableComponent>();

    const auto& constComp = composition_;
    int count = 0;
    for (const auto& component : constComp) {
        EXPECT_TRUE((std::is_same_v<decltype(component), const oxygen::Component&>));
        ++count;
    }
    EXPECT_EQ(count, 2);
}

NOLINT_TEST_F(CompositionTest, IterateWithRanges)
{
    [[maybe_unused]] auto& simple = composition_.AddComponent<SimpleComponent>();
    [[maybe_unused]] auto& dependent = composition_.AddComponent<DependentComponent>();
    [[maybe_unused]] auto& cloneable = composition_.AddComponent<CloneableComponent>();

    const auto view = composition_ | std::views::transform([](const oxygen::Component& comp) {
        return comp.GetTypeId();
    });

    std::vector<oxygen::TypeId> types {};
    for (const auto type_id : view) {
        types.push_back(type_id);
    }

    // Collect expected TypeIds
    std::vector expected = {
        SimpleComponent::ClassTypeId(),
        DependentComponent::ClassTypeId(),
        CloneableComponent::ClassTypeId()
    };

    // Verify collected TypeIds match expected values
    EXPECT_TRUE(std::ranges::equal(types, expected));

    // Count components with specific TypeId
    const auto simpleCount = std::ranges::count_if(composition_, [](const oxygen::Component& c) {
        return c.GetTypeId() == SimpleComponent::ClassTypeId();
    });

    EXPECT_EQ(simpleCount, 1);
}

NOLINT_TEST_F(CompositionTest, PrintComponents)
{
    // Add some components to the composition
    composition_.AddComponent<SimpleComponent>();
    composition_.AddComponent<DependentComponent>();
    composition_.AddComponent<CloneableComponent>();

    // Capture the output of PrintComponents
    std::ostringstream os;
    composition_.PrintComponents(os);
    const std::string output = os.str();

    using testing::HasSubstr;
    // Verify the output
    EXPECT_THAT(output, HasSubstr("> Object \"Unknown\" has 3 components:"));
    EXPECT_THAT(output, HasSubstr("SimpleComponent"));
    EXPECT_THAT(output, HasSubstr("DependentComponent"));
    EXPECT_THAT(output, HasSubstr("CloneableComponent"));
}
} // namespace
