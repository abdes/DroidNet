#pragma once

#include <memory>

#include "Oxygen/Composition/Object.h"

namespace oxygen::platform {
template <typename T>
concept NativeEventType =
    // std::is_default_constructible_v<T> &&
    std::is_trivially_copyable_v<T> // Add to ensure event types are POD-like
    && !std::is_pointer_v<T>; // Prevent pointer types

class NativeEventHolder : public oxygen::Object {
public:
    using NativeEventHandle = void*;

    NativeEventHolder() = default;
    ~NativeEventHolder() override = default;

    NativeEventHolder(const NativeEventHolder&) = default;
    auto operator=(const NativeEventHolder&) -> NativeEventHolder& = default;

    NativeEventHolder(NativeEventHolder&&) = default;
    auto operator=(NativeEventHolder&&) -> NativeEventHolder& = default;

    [[nodiscard]] virtual auto NativeEvent() noexcept -> NativeEventHandle = 0;
};

template <NativeEventType T>
class PlatformEventImpl final : public NativeEventHolder {
    OXYGEN_TYPED(PlatformEventImpl)
public:
    [[nodiscard]] auto NativeEvent() noexcept -> NativeEventHandle override
    {
        static_assert(std::is_standard_layout_v<T>,
            "Native event type must have standard layout");
        return static_cast<NativeEventHandle>(&native_event_);
    }

private:
    alignas(alignof(T)) T native_event_ {}; // Ensure proper alignment
};

class PlatformEvent final {
public:
    template <NativeEventType T>
    static auto Create()
    {
        return PlatformEvent(std::make_unique<PlatformEventImpl<T>>());
    }

    ~PlatformEvent() = default;

    PlatformEvent(const PlatformEvent&) = delete;
    auto operator=(const PlatformEvent&) -> PlatformEvent& = delete;

    PlatformEvent(PlatformEvent&&) = default;
    auto operator=(PlatformEvent&&) -> PlatformEvent& = default;

    [[nodiscard]] auto IsHandled() const { return handled_; }
    void SetHandled() const noexcept { handled_ = true; }

    [[nodiscard]] auto NativeEvent() const noexcept { return impl_->NativeEvent(); }

    template <NativeEventType T>
    [[nodiscard]] auto NativeEventAs() const noexcept(false)
    {
        auto my_impl_type = impl_->GetTypeId();
        auto requested_type = PlatformEventImpl<T>::ClassTypeId();
        return my_impl_type == requested_type ? static_cast<T*>(impl_->NativeEvent()) : nullptr;
    }

private:
    explicit PlatformEvent(std::unique_ptr<NativeEventHolder> impl)
        : impl_(std::move(impl))
    {
    }

    std::unique_ptr<NativeEventHolder> impl_;

    // This is the only mutable state in the class. The rest of the event, once
    // it is produced, is constant.
    mutable bool handled_ { false };
};

} // namespace oxygen::platform
