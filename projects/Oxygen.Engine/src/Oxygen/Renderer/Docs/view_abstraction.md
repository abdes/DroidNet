# View Abstraction

## View Features

1. **Encapsulation of View State**
   - Store all camera/view parameters (view matrix, projection matrix, viewport,
     scissor, etc.) in a single class.

2. **Matrix and Frustum Caching**
   - Cache derived matrices (view-projection, inverses) and frustums.
   - Only recompute them when the underlying parameters change.

3. **Viewport and Scissor Management**
   - Include viewport and scissor rectangles as part of the view state.

4. **Pixel Offset Support**
   - Support a pixel offset for TAA, jitter, or subpixel rendering.

5. **Reverse Depth and Mirroring Flags**
   - Track whether the view uses reverse depth or is mirrored.

6. **Visibility/Frustum Queries**
   - Provide methods to get the frustum and test bounding boxes for visibility.

## How to Integrate Camera and View

### 1. Camera as Scene Component

- Your Camera (perspective or ortho) is attached to a scene node/component.
- It defines the view and projection parameters (position, orientation, FOV,
  near/far, etc.).
- The camera is updated as part of the scene update (e.g., following a player,
  animation).

### 2. View as Render-Time Snapshot

- At render time, create or update a View object using the current state of the
  camera.
- The View takes the camera’s world transform and projection parameters and sets
  its internal matrices (view, projection, etc.).
- The View also manages render-specific state: viewport, scissor, pixel offset,
  array slice, and cached matrices/frustums.

### 3. Renderer Consumes the View

- The renderer uses the View to get all the information it needs for rendering:
  view/projection matrices, frustum for culling, viewport/scissor, etc.
- This decouples the scene/camera logic from the rendering logic, allowing for
  features like jitter, multi-view, or custom viewports without modifying the
  camera itself.

---

### Integration Flow

1. Query the camera’s transform and projection.
2. Set these values on the View: view.SetMatrices(camera.GetViewMatrix(),
   camera.GetProjectionMatrix());
3. Set any render-specific state on the View (viewport, pixel offset, etc.).
4. Pass the View to the renderer.

---

### Benefits

- Keeps camera logic focused on scene representation.
- Lets View handle all render-specific details and optimizations.
- Makes it easy to support advanced rendering features in the future (e.g., TAA,
  split-screen, VR) without changing your camera or scene logic.

---

### Summary Table

| Camera (Scene)         | View (Rendering)           |
|------------------------|----------------------------|
| Scene node/component   | Standalone render object   |
| World transform        | View matrix                |
| Projection params      | Projection matrix          |
| No render state        | Viewport, scissor, offset  |
| No caching             | Caches derived data        |
| Scene update           | Updated per-frame for render |

**In short:** The camera defines what to see; the view defines how to render it.
You update the view from the camera before

## Example

```cpp
    // a plane equation, so that any point (v) for which (dot(normal, v) == distance) lies on the plane
    struct plane
    {
        float3 normal;
        float distance;

        constexpr plane() : normal(0.f, 0.f, 0.f), distance(0.f) { }
        constexpr plane(const plane &p) : normal(p.normal), distance(p.distance) { }
        constexpr plane(const float3& n, float d) : normal(n), distance(d) { }
        constexpr plane(float x, float y, float z, float d) : normal(x, y, z), distance(d) { }

        plane normalize() const;

        constexpr bool isempty();
    };

    // six planes, normals pointing outside of the volume
    struct frustum
    {
        enum Planes
        {
            NEAR_PLANE = 0,
            FAR_PLANE,
            LEFT_PLANE,
            RIGHT_PLANE,
            TOP_PLANE,
            BOTTOM_PLANE,
            PLANES_COUNT
        };

        enum Corners
        {
            C_LEFT = 0,
            C_RIGHT = 1,
            C_BOTTOM = 0,
            C_TOP = 2,
            C_NEAR = 0,
            C_FAR = 4
        };

        plane planes[PLANES_COUNT];

        frustum() { }

        frustum(const frustum &f);
        frustum(const float4x4 &viewProjMatrix, bool isReverseProjection);

        bool intersectsWith(const float3 &point) const;
        bool intersectsWith(const box3 &box) const;

        static constexpr uint32_t numCorners = 8;
        float3 getCorner(int index) const;

        frustum normalize() const;
        frustum grow(float distance) const;

        bool isempty() const;       // returns true if the frustum trivially rejects all points; does *not* analyze cases when plane equations are mutually exclusive
        bool isopen() const;        // returns true if the frustum has at least one plane that trivially accepts all points
        bool isinfinite() const;    // returns true if the frustum trivially accepts all points

        plane& nearPlane() { return planes[NEAR_PLANE]; }
        plane& farPlane() { return planes[FAR_PLANE]; }
        plane& leftPlane() { return planes[LEFT_PLANE]; }
        plane& rightPlane() { return planes[RIGHT_PLANE]; }
        plane& topPlane() { return planes[TOP_PLANE]; }
        plane& bottomPlane() { return planes[BOTTOM_PLANE]; }

        const plane& nearPlane() const { return planes[NEAR_PLANE]; }
        const plane& farPlane() const { return planes[FAR_PLANE]; }
        const plane& leftPlane() const { return planes[LEFT_PLANE]; }
        const plane& rightPlane() const { return planes[RIGHT_PLANE]; }
        const plane& topPlane() const { return planes[TOP_PLANE]; }
        const plane& bottomPlane() const { return planes[BOTTOM_PLANE]; }

        static frustum empty();    // a frustum that doesn't intersect with any points
        static frustum infinite(); // a frustum that intersects with all points

        static frustum fromBox(const box3& b);
    };
```

```cpp
// GLM-based frustum: 6 planes, normals point outward
struct Frustum
{
    enum { NEAR = 0, FAR, LEFT, RIGHT, TOP, BOTTOM, COUNT };
    std::array<Plane, COUNT> planes;

    Frustum() = default;

    // Extract frustum planes from a view-projection matrix
    Frustum(const glm::mat4& viewProj, bool reverseZ = false)
    {
        // Extract planes in the form: ax + by + cz + d = 0
        // Reference: Gribb & Hartmann, "Fast Extraction of Viewing Frustum Planes from the World-View-Projection Matrix"
        // Note: GLM matrices are column-major

        // Left
        planes[LEFT]   = Plane(
            viewProj[0][3] + viewProj[0][0],
            viewProj[1][3] + viewProj[1][0],
            viewProj[2][3] + viewProj[2][0],
            viewProj[3][3] + viewProj[3][0]
        ).normalize();

        // Right
        planes[RIGHT]  = Plane(
            viewProj[0][3] - viewProj[0][0],
            viewProj[1][3] - viewProj[1][0],
            viewProj[2][3] - viewProj[2][0],
            viewProj[3][3] - viewProj[3][0]
        ).normalize();

        // Bottom
        planes[BOTTOM] = Plane(
            viewProj[0][3] + viewProj[0][1],
            viewProj[1][3] + viewProj[1][1],
            viewProj[2][3] + viewProj[2][1],
            viewProj[3][3] + viewProj[3][1]
        ).normalize();

        // Top
        planes[TOP]    = Plane(
            viewProj[0][3] - viewProj[0][1],
            viewProj[1][3] - viewProj[1][1],
            viewProj[2][3] - viewProj[2][1],
            viewProj[3][3] - viewProj[3][1]
        ).normalize();

        // Near
        if (!reverseZ) {
            planes[NEAR] = Plane(
                viewProj[0][3] + viewProj[0][2],
                viewProj[1][3] + viewProj[1][2],
                viewProj[2][3] + viewProj[2][2],
                viewProj[3][3] + viewProj[3][2]
            ).normalize();
            // Far
            planes[FAR] = Plane(
                viewProj[0][3] - viewProj[0][2],
                viewProj[1][3] - viewProj[1][2],
                viewProj[2][3] - viewProj[2][2],
                viewProj[3][3] - viewProj[3][2]
            ).normalize();
        } else {
            // Reverse-Z: near/far swapped
            planes[FAR] = Plane(
                viewProj[0][3] + viewProj[0][2],
                viewProj[1][3] + viewProj[1][2],
                viewProj[2][3] + viewProj[2][2],
                viewProj[3][3] + viewProj[3][2]
            ).normalize();
            planes[NEAR] = Plane(
                viewProj[0][3] - viewProj[0][2],
                viewProj[1][3] - viewProj[1][2],
                viewProj[2][3] - viewProj[2][2],
                viewProj[3][3] - viewProj[3][2]
            ).normalize();
        }
    }
};
```

```cpp

class View
{
public:
    // --- Snapshot of camera state for this frame ---
    const glm::mat4 viewMatrix;
    const glm::mat4 projMatrix;
    const glm::vec3 viewOrigin;
    const glm::vec3 viewDirection;

    // --- Render state for this view ---
    const Viewport viewport;
    const Rect scissorRect;
    const glm::vec2 pixelOffset;
    const bool reverseDepth;
    const bool isMirrored;

    // --- Derived data, computed eagerly in constructor ---
    const glm::mat4 viewMatrixInv;
    const glm::mat4 projMatrixInv;
    const glm::mat4 viewProjMatrix;
    const glm::mat4 viewProjMatrixInv;
    const Frustum viewFrustum;

    // Construct a snapshot from the camera and render state for this frame
    template<typename CameraT>
    View(
        const CameraT& camera,
        const Viewport& viewport_,
        const Rect& scissor_,
        const glm::vec2& pixelOffset_ = glm::vec2(0.0f),
        bool reverseDepth_ = false,
        bool isMirrored_ = false)
        : viewMatrix(camera.GetViewMatrix())
        , projMatrix(camera.GetProjectionMatrix())
        , viewOrigin(camera.GetPosition())
        , viewDirection(camera.GetDirection())
        , viewport(viewport_)
        , scissorRect(scissor_)
        , pixelOffset(pixelOffset_)
        , reverseDepth(reverseDepth_)
        , isMirrored(isMirrored_)
        , viewMatrixInv(glm::affineInverse(viewMatrix))
        , projMatrixInv(glm::inverse(projMatrix))
        , viewProjMatrix(projMatrix * viewMatrix)
        , viewProjMatrixInv(glm::inverse(projMatrix * viewMatrix))
        , viewFrustum(viewProjMatrix, reverseDepth)
    {}

    // --- Getters for all relevant state ---
    const glm::mat4& GetViewMatrix() const { return viewMatrix; }
    const glm::mat4& GetProjectionMatrix() const { return projMatrix; }
    const Viewport& GetViewport() const { return viewport; }
    const Rect& GetScissorRect() const { return scissorRect; }
    const glm::vec2& GetPixelOffset() const { return pixelOffset; }
    bool IsReverseDepth() const { return reverseDepth; }
    bool IsMirrored() const { return isMirrored; }
    const glm::vec3& GetViewOrigin() const { return viewOrigin; }
    const glm::vec3& GetViewDirection() const { return viewDirection; }
    const glm::mat4& GetInverseViewMatrix() const { return viewMatrixInv; }
    const glm::mat4& GetInverseProjectionMatrix() const { return projMatrixInv; }
    const glm::mat4& GetViewProjectionMatrix() const { return viewProjMatrix; }
    const glm::mat4& GetInverseViewProjectionMatrix() const { return viewProjMatrixInv; }
    const Frustum& GetViewFrustum() const { return viewFrustum; }

    // No setters or mutators—this is a true immutable snapshot.
};
```
