//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <EditorModule/InputAccumulator.h>
#include <EditorModule/InputAccumulatorAdapter.h>

namespace oxygen::interop::module {

// Test-only SUT: expose a public method that calls the protected Drain()
struct InputAccumulatorSUT : InputAccumulator {
    using Base = InputAccumulator;
    AccumulatedInput DrainPublic(ViewId v) noexcept { return Base::Drain(v); }
};

} // namespace oxygen::interop::module

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using namespace oxygen::interop::module;

namespace oxygen::interop::module {
class FakeWriter : public IInputWriter {
public:
    void WriteMouseMove(ViewId view, SubPixelMotion delta, SubPixelPosition position) override {
        mouse_moves.push_back({view, delta, position});
    }
    void WriteMouseWheel(ViewId view, SubPixelMotion delta, SubPixelPosition position) override {
        mouse_wheels.push_back({view, delta, position});
    }
    void WriteKey(ViewId view, EditorKeyEvent ev) override { keys.push_back({view, ev}); }
    void WriteMouseButton(ViewId view, EditorButtonEvent ev) override { buttons.push_back({view, ev}); }

    struct MouseMove { ViewId view; SubPixelMotion delta; SubPixelPosition pos; };
    struct MouseWheel { ViewId view; SubPixelMotion delta; SubPixelPosition pos; };
    struct Key { ViewId view; EditorKeyEvent ev; };
    struct Button { ViewId view; EditorButtonEvent ev; };

    std::vector<MouseMove> mouse_moves;
    std::vector<MouseWheel> mouse_wheels;
    std::vector<Key> keys;
    std::vector<Button> buttons;
};
} // namespace oxygen::interop::module

namespace InteropTests {

[TestClass]
public ref class InputAccumulatorCliTests {
public:
    [TestMethod]
    void DrainAggregatesMotionAndKeys() {
        ::InputAccumulatorSUT acc;
        const ::ViewId v{1};

        ::EditorMouseMotionEvent mm;
        mm.motion = ::oxygen::SubPixelMotion{3.0f, 2.0f};
        mm.position = ::oxygen::SubPixelPosition{10.0f, 12.0f};

        acc.PushMouseMotion(v, mm);
        acc.PushMouseMotion(v, mm);

        ::EditorKeyEvent key;
        key.key = ::oxygen::platform::Key::kA;
        key.pressed = true;
        acc.PushKeyEvent(v, key);

        auto batch = acc.DrainPublic(v);

        auto fake = std::make_unique<FakeWriter>();
        FakeWriter* fake_ptr = fake.get();
        ::InputAccumulatorAdapter adapter(std::move(fake));
        adapter.DispatchForView(v, batch);

        // Asserts using MSTest Assert
        Assert::AreEqual(1, (int)fake_ptr->mouse_moves.size());
        Assert::AreEqual(6.0f, fake_ptr->mouse_moves[0].delta.dx, 0.0001f);
        Assert::AreEqual(4.0f, fake_ptr->mouse_moves[0].delta.dy, 0.0001f);
        Assert::AreEqual(1, (int)fake_ptr->keys.size());
    }

    [TestMethod]
    void DrainClearsAccumulator() {
        ::InputAccumulatorSUT acc;
        const ::ViewId v{2};

        ::EditorMouseMotionEvent mm;
        mm.motion = ::oxygen::SubPixelMotion{2.0f, 3.0f};
        mm.position = ::oxygen::SubPixelPosition{1.0f, 1.0f};

        acc.PushMouseMotion(v, mm);

        auto first = acc.DrainPublic(v);
        Assert::AreEqual(2.0f, first.mouse_delta.dx, 0.0001f);
        Assert::AreEqual(3.0f, first.mouse_delta.dy, 0.0001f);
        Assert::AreEqual(0, (int)first.key_events.size());

        auto second = acc.DrainPublic(v);
        Assert::AreEqual(0.0f, second.mouse_delta.dx, 0.0001f);
        Assert::AreEqual(0.0f, second.mouse_delta.dy, 0.0001f);
        Assert::AreEqual(0.0f, second.last_position.x, 0.0001f);
        Assert::AreEqual(0.0f, second.last_position.y, 0.0001f);
    }

    [TestMethod]
    void EventsAreScopedToView() {
        ::InputAccumulatorSUT acc;
        const ::ViewId v1{3};
        const ::ViewId v2{4};

        ::EditorMouseMotionEvent mm1; mm1.motion = ::oxygen::SubPixelMotion{1.0f, 0.0f};
        ::EditorMouseMotionEvent mm2; mm2.motion = ::oxygen::SubPixelMotion{0.0f, 2.0f};

        acc.PushMouseMotion(v1, mm1);
        acc.PushMouseMotion(v2, mm2);

        auto b1 = acc.DrainPublic(v1);
        auto b2 = acc.DrainPublic(v2);

        Assert::AreEqual(1.0f, b1.mouse_delta.dx, 0.0001f);
        Assert::AreEqual(0.0f, b1.mouse_delta.dy, 0.0001f);
        Assert::AreEqual(0.0f, b2.mouse_delta.dx, 0.0001f);
        Assert::AreEqual(2.0f, b2.mouse_delta.dy, 0.0001f);
    }

    [TestMethod]
    void MouseWheelAggregationAndPosition() {
        ::InputAccumulatorSUT acc;
        const ::ViewId v{5};

        ::EditorMouseWheelEvent w1; w1.scroll = ::oxygen::SubPixelMotion{0.0f, 1.0f}; w1.position = ::oxygen::SubPixelPosition{5.0f, 5.0f};
        ::EditorMouseWheelEvent w2; w2.scroll = ::oxygen::SubPixelMotion{0.0f, 2.0f}; w2.position = ::oxygen::SubPixelPosition{6.0f, 6.0f};

        acc.PushMouseWheel(v, w1);
        acc.PushMouseWheel(v, w2);

        auto batch = acc.DrainPublic(v);

        auto fake = std::make_unique<FakeWriter>();
        FakeWriter* fake_ptr = fake.get();
        ::InputAccumulatorAdapter adapter(std::move(fake));
        adapter.DispatchForView(v, batch);

        Assert::AreEqual(1, (int)fake_ptr->mouse_wheels.size());
        Assert::AreEqual(3.0f, fake_ptr->mouse_wheels[0].delta.dy, 0.0001f);
        Assert::AreEqual(6.0f, fake_ptr->mouse_wheels[0].pos.x, 0.0001f);
        Assert::AreEqual(6.0f, fake_ptr->mouse_wheels[0].pos.y, 0.0001f);
    }

    [TestMethod]
    void ButtonEventsOrdering() {
        ::InputAccumulatorSUT acc;
        const ::ViewId v{6};

        ::EditorButtonEvent b1; b1.button = ::oxygen::platform::MouseButton::kLeft; b1.pressed = true; b1.position = ::oxygen::SubPixelPosition{1.0f, 1.0f};
        ::EditorButtonEvent b2; b2.button = ::oxygen::platform::MouseButton::kLeft; b2.pressed = false; b2.position = ::oxygen::SubPixelPosition{2.0f, 2.0f};

        acc.PushButtonEvent(v, b1);
        acc.PushButtonEvent(v, b2);

        auto batch = acc.DrainPublic(v);

        auto fake = std::make_unique<FakeWriter>();
        FakeWriter* fake_ptr = fake.get();
        ::InputAccumulatorAdapter adapter(std::move(fake));
        adapter.DispatchForView(v, batch);

        Assert::AreEqual(2, (int)fake_ptr->buttons.size());
        Assert::IsTrue(fake_ptr->buttons[0].ev.pressed);
        Assert::IsFalse(fake_ptr->buttons[1].ev.pressed);
    }

    [TestMethod]
    void MultipleKeyEventsOrdering() {
        ::InputAccumulatorSUT acc;
        const ::ViewId v{7};

        ::EditorKeyEvent k1; k1.key = ::oxygen::platform::Key::kA; k1.pressed = true;
        ::EditorKeyEvent k2; k2.key = ::oxygen::platform::Key::kB; k2.pressed = true;
        ::EditorKeyEvent k3; k3.key = ::oxygen::platform::Key::kC; k3.pressed = true;

        acc.PushKeyEvent(v, k1);
        acc.PushKeyEvent(v, k2);
        acc.PushKeyEvent(v, k3);

        auto batch = acc.DrainPublic(v);

        auto fake = std::make_unique<FakeWriter>();
        FakeWriter* fake_ptr = fake.get();
        ::InputAccumulatorAdapter adapter(std::move(fake));
        adapter.DispatchForView(v, batch);

        Assert::AreEqual(3, (int)fake_ptr->keys.size());
        Assert::AreEqual((int)::oxygen::platform::Key::kA, (int)fake_ptr->keys[0].ev.key);
        Assert::AreEqual((int)::oxygen::platform::Key::kB, (int)fake_ptr->keys[1].ev.key);
        Assert::AreEqual((int)::oxygen::platform::Key::kC, (int)fake_ptr->keys[2].ev.key);
    }

    [TestMethod]
    void OnFocusLostClearsDeltasKeepsEvents() {
        ::InputAccumulatorSUT acc;
        const ::ViewId v{8};

        ::EditorMouseMotionEvent mm; mm.motion = ::oxygen::SubPixelMotion{5.0f, 5.0f}; mm.position = ::oxygen::SubPixelPosition{3.0f, 3.0f};
        ::EditorKeyEvent k; k.key = ::oxygen::platform::Key::kA; k.pressed = true;
        ::EditorButtonEvent b; b.button = ::oxygen::platform::MouseButton::kLeft; b.pressed = true;

        acc.PushMouseMotion(v, mm);
        acc.PushKeyEvent(v, k);
        acc.PushButtonEvent(v, b);

        acc.OnFocusLost(v);

        auto batch = acc.DrainPublic(v);

        auto fake = std::make_unique<FakeWriter>();
        FakeWriter* fake_ptr = fake.get();
        ::InputAccumulatorAdapter adapter(std::move(fake));
        adapter.DispatchForView(v, batch);

        // mouse move should be cleared
        Assert::AreEqual(0, (int)fake_ptr->mouse_moves.size());
        // key/button events should still be present
        Assert::AreEqual(1, (int)fake_ptr->keys.size());
        Assert::AreEqual(1, (int)fake_ptr->buttons.size());
    }

    [TestMethod]
    void DrainEmptyReturnsNothing() {
        ::InputAccumulatorSUT acc;
        const ::ViewId v{9}; // never used

        auto batch = acc.DrainPublic(v);
        auto fake = std::make_unique<FakeWriter>();
        FakeWriter* fake_ptr = fake.get();
        ::InputAccumulatorAdapter adapter(std::move(fake));
        adapter.DispatchForView(v, batch);

        Assert::AreEqual(0, (int)fake_ptr->mouse_moves.size());
        Assert::AreEqual(0, (int)fake_ptr->mouse_wheels.size());
        Assert::AreEqual(0, (int)fake_ptr->keys.size());
        Assert::AreEqual(0, (int)fake_ptr->buttons.size());
    }
};

}
