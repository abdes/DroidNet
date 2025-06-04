# Scene Management Module

## PENDING
- Add explicit deep clone API: `SceneNode::Clone(bool deep = true)`
- Update/add scenario-based Google Test cases for all new/changed APIs (Scene module only)



class Transform {
public:
    using Vec3 = glm::vec3;
    using Quat = glm::quat;
    using Mat4 = glm::mat4;

private:
    SceneNode node_;  // Contains scene weak_ptr and handle

public:
    // Construction (only SceneNode creates these)
    explicit Transform(SceneNode node);

    // Validity checking
    [[nodiscard]] bool IsValid() const noexcept;

    // Public transform operations
    void SetLocalTransform(const Vec3& pos, const Quat& rot, const Vec3& scale);
    void SetLocalPosition(const Vec3& position);
    void SetLocalRotation(const Quat& rotation);
    void SetLocalScale(const Vec3& scale);

    // Transform operations
    void Translate(const Vec3& offset, bool local = true);
    void Rotate(const Quat& rotation, bool local = true);
    void Scale(const Vec3& scale_factor);

    // Getters
    [[nodiscard]] Vec3 GetLocalPosition() const;
    [[nodiscard]] Quat GetLocalRotation() const;
    [[nodiscard]] Vec3 GetLocalScale() const;
    [[nodiscard]] Mat4 GetLocalMatrix() const;

    // World space queries (read-only)
    [[nodiscard]] Vec3 GetWorldPosition() const;
    [[nodiscard]] Quat GetWorldRotation() const;
    [[nodiscard]] Vec3 GetWorldScale() const;
    [[nodiscard]] Mat4 GetWorldMatrix() const;

    // Scene-aware additions (future extensions)
    void LookAt(const Vec3& target, const Vec3& up = {0, 1, 0});
    void SetWorldPosition(const Vec3& world_pos);  // Computes local based on parent
    Transform GetRelativeTransform(const Transform& other) const;

private:
    // Helper to get the underlying component (with validation)
    [[nodiscard]] auto GetComponent() const -> std::optional<std::reference_wrapper<TransformComponent>>;
};


// Internal helper that all public methods use
template<typename Func>
auto Transform::SafeCall(Func&& func) const -> decltype(func(std::declval<TransformComponent&>())) {
    if (!IsValid()) {
        LOG_F(WARNING, "Transform operation on invalid node");
        if constexpr (std::is_void_v<decltype(func(std::declval<TransformComponent&>()))>) {
            return;
        } else {
            return {}; // Return default-constructed value
        }
    }

    auto component = GetComponent();
    if (!component) {
        LOG_F(ERROR, "Node missing TransformComponent");
        if constexpr (std::is_void_v<decltype(func(std::declval<TransformComponent&>()))>) {
            return;
        } else {
            return {};
        }
    }

    return func(component->get());
}

// Usage in public methods:
void Transform::SetLocalPosition(const Vec3& position) {
    SafeCall([&](TransformComponent& tc) {
        tc.SetLocalPosition(position);
    });
}

Vec3 Transform::GetLocalPosition() const {
    return SafeCall([](const TransformComponent& tc) {
        return tc.GetLocalPosition();
    });
}

// Set world position by computing the appropriate local position
void Transform::SetWorldPosition(const Vec3& world_pos) {
    SafeCall([&](TransformComponent& tc) {
        auto parent = node_.GetParent();
        if (parent && parent->IsValid()) {
            auto parent_transform = parent->Transform();
            Mat4 parent_world_inverse = glm::inverse(parent_transform.GetWorldMatrix());
            Vec4 local_pos = parent_world_inverse * Vec4(world_pos, 1.0f);
            tc.SetLocalPosition(Vec3(local_pos));
        } else {
            tc.SetLocalPosition(world_pos); // Root node
        }
    });
}

// LookAt implementation
void Transform::LookAt(const Vec3& target, const Vec3& up) {
    SafeCall([&](TransformComponent& tc) {
        Vec3 current_pos = tc.GetLocalPosition();
        Vec3 direction = glm::normalize(target - current_pos);

        // Handle special case where direction == up
        if (glm::abs(glm::dot(direction, up)) > 0.999f) {
            up = glm::abs(direction.y) > 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        }

        Mat4 look_at = glm::lookAt(current_pos, target, up);
        Quat rotation = glm::quat_cast(glm::inverse(look_at));
        tc.SetLocalRotation(rotation);
    });
}

// For hot paths, provide unchecked variants
class Transform {
public:
    // Safe versions (default)
    void SetLocalPosition(const Vec3& pos);

    // Unchecked versions for performance-critical code
    void SetLocalPositionUnchecked(const Vec3& pos) {
        // Skip validation, assume node/component are valid
        GetComponentUnchecked().SetLocalPosition(pos);
    }

private:
    auto GetComponentUnchecked() const -> TransformComponent& {
        return node_.GetObject()->get().template GetComponent<TransformComponent>();
    }
};

. Future Extension Points
The wrapper provides natural places to add:

Animation Integration:
Coordinate Space Conversions:
Physics Integration:
Validation and Constraints:
