//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec4.hpp>

#include <EditorModule/TransformGizmo.h>

namespace oxygen::interop::module {

  namespace {

    using vortex::ViewOverlay;
    using vortex::ViewOverlayLayer;
    using vortex::ViewOverlayLine;
    using vortex::ViewOverlayVertex;

    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kTwoPi = 2.0F * kPi;

    // Layout, as fractions of the gizmo size.
    constexpr float kShaftStart = 0.12F;
    constexpr float kArrowShaftEnd = 0.8F;
    constexpr float kArrowTip = 1.0F;
    constexpr float kArrowRadius = 0.065F;
    constexpr float kScaleShaftEnd = 0.82F;
    constexpr float kScaleBoxCenter = 0.88F;
    constexpr float kScaleBoxHalf = 0.06F;
    constexpr float kScaleCenterHalf = 0.075F;
    constexpr float kTranslatePlaneNear = 0.28F;
    constexpr float kTranslatePlaneFar = 0.48F;
    constexpr float kScalePlaneNear = 0.28F;
    constexpr float kScalePlaneFar = 0.44F;
    constexpr float kRingRadius = 0.85F;
    constexpr float kViewRingRadius = 1.0F;

    // Pixel sizes, before display scaling.
    constexpr float kCenterHalfPixels = 6.0F;
    constexpr float kHandleStrokePixels = 2.5F;
    constexpr float kOutlineStrokePixels = 1.5F;
    constexpr float kGuideStrokePixels = 1.0F;
    constexpr float kHitTolerancePixels = 8.0F;
    constexpr float kCenterHitPixels = 10.0F;

    // An axis pointing this close to the eye is hidden, and so is a plane seen
    // this close to edge-on: neither can be dragged predictably.
    constexpr float kAxisHideCosine = 0.985F;
    constexpr float kPlaneHideCosine = 0.2F;
    // A rotation drag reads angles from the ring plane only while the pointer
    // ray meets it at least this steeply.
    constexpr float kRingPlaneMinCosine = 0.08F;
    constexpr int kRingSegments = 64;
    // Tolerance of the shear test on a result's rotation axes.
    constexpr float kOrthogonalityTolerance = 1.0e-3F;
    // A guideline is long enough to leave any view.
    constexpr float kGuideLengthFactor = 1.0e4F;

    const glm::vec4 kAxisColors[3] = {
      { 0.92F, 0.26F, 0.26F, 1.0F },
      { 0.42F, 0.80F, 0.22F, 1.0F },
      { 0.26F, 0.52F, 0.96F, 1.0F },
    };
    const glm::vec4 kHoverColor { 1.0F, 0.84F, 0.24F, 1.0F };
    const glm::vec4 kNeutralColor { 0.88F, 0.90F, 0.92F, 1.0F };
    const glm::vec3 kSweepColor { 0.62F, 0.64F, 0.66F };

    constexpr float kPlaneFillAlpha = 0.3F;
    constexpr float kPlaneFillHoverAlpha = 0.55F;
    constexpr float kSweepAlpha = 0.2F;
    constexpr float kSweepMaxAlpha = 0.35F;

    auto AxisIndex(const GizmoHandle handle) -> int
    {
      switch (handle) {
      case GizmoHandle::kX:
        return 0;
      case GizmoHandle::kY:
        return 1;
      case GizmoHandle::kZ:
        return 2;
      default:
        return -1;
      }
    }

    //! The two axes a plane handle moves along and the axis normal to it.
    struct PlaneAxes {
      int first { 0 };
      int second { 1 };
      int normal { 2 };
    };

    auto PlaneOf(const GizmoHandle handle) -> std::optional<PlaneAxes>
    {
      switch (handle) {
      case GizmoHandle::kXY:
        return PlaneAxes { 0, 1, 2 };
      case GizmoHandle::kXZ:
        return PlaneAxes { 0, 2, 1 };
      case GizmoHandle::kYZ:
        return PlaneAxes { 1, 2, 0 };
      default:
        return std::nullopt;
      }
    }

    constexpr std::array kPlaneHandles {
      GizmoHandle::kXY,
      GizmoHandle::kXZ,
      GizmoHandle::kYZ,
    };

    auto AxisHandle(const int index) -> GizmoHandle
    {
      return index == 0 ? GizmoHandle::kX
        : index == 1    ? GizmoHandle::kY
                        : GizmoHandle::kZ;
    }

    auto IsFinite(const glm::vec3& value) -> bool
    {
      return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
    }

    auto IsFinite(const glm::quat& value) -> bool
    {
      return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && std::isfinite(value.w);
    }

    auto AnyPerpendicular(const glm::vec3& n) -> glm::vec3
    {
      const auto seed = std::abs(n.z) < 0.9F ? glm::vec3 { 0.0F, 0.0F, 1.0F }
                                             : glm::vec3 { 1.0F, 0.0F, 0.0F };
      return glm::normalize(glm::cross(n, seed));
    }

    //! Wraps an angle difference into (-pi, pi].
    auto WrapAngle(float radians) -> float
    {
      while (radians > kPi) {
        radians -= kTwoPi;
      }
      while (radians <= -kPi) {
        radians += kTwoPi;
      }
      return radians;
    }

    auto SnapTo(const float value, const float increment) -> float
    {
      return increment > 0.0F ? std::round(value / increment) * increment
                              : value;
    }

    auto DistanceToSegment(
      const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) -> float
    {
      const auto ab = b - a;
      const auto length_squared = glm::dot(ab, ab);
      if (length_squared <= 1.0e-8F) {
        return glm::distance(p, a);
      }
      const auto t = std::clamp(glm::dot(p - a, ab) / length_squared, 0.0F, 1.0F);
      return glm::distance(p, a + ab * t);
    }

    auto Cross2(const glm::vec2& a, const glm::vec2& b) -> float
    {
      return a.x * b.y - a.y * b.x;
    }

    auto InsideConvex(const glm::vec2& p, const std::array<glm::vec2, 4>& quad)
      -> bool
    {
      auto positive = false;
      auto negative = false;
      for (std::size_t i = 0; i < quad.size(); ++i) {
        const auto& a = quad[i];
        const auto& b = quad[(i + 1) % quad.size()];
        const auto side = Cross2(b - a, p - a);
        positive = positive || side > 0.0F;
        negative = negative || side < 0.0F;
      }
      return !(positive && negative);
    }

    auto IntersectPlane(const GizmoCamera& camera, const GizmoRay& ray,
      const glm::vec3& point, const glm::vec3& normal)
      -> std::optional<glm::vec3>
    {
      const auto denominator = glm::dot(ray.direction, normal);
      if (std::abs(denominator) < 1.0e-6F) {
        return std::nullopt;
      }
      const auto t = glm::dot(point - ray.origin, normal) / denominator;
      if (!camera.IsOrthographic() && t <= 0.0F) {
        return std::nullopt;
      }
      const auto hit = ray.origin + ray.direction * t;
      return IsFinite(hit) ? std::optional { hit } : std::nullopt;
    }

    //! The drag plane of an axis: it holds the axis and faces the eye.
    auto AxisDragNormal(const glm::vec3& axis, const glm::vec3& to_eye)
      -> std::optional<glm::vec3>
    {
      const auto normal = to_eye - axis * glm::dot(to_eye, axis);
      const auto length = glm::length(normal);
      if (length < 1.0e-3F) {
        return std::nullopt;
      }
      return normal / length;
    }

    //! Splits an affine matrix into position, rotation and scale; false when
    //! it shears or collapses an axis.
    auto DecomposeTrs(const glm::mat4& m, glm::vec3& position,
      glm::quat& rotation, glm::vec3& scale) -> bool
    {
      position = glm::vec3(m[3]);
      auto x = glm::vec3(m[0]);
      auto y = glm::vec3(m[1]);
      auto z = glm::vec3(m[2]);
      scale = { glm::length(x), glm::length(y), glm::length(z) };
      if (scale.x < 1.0e-8F || scale.y < 1.0e-8F || scale.z < 1.0e-8F) {
        return false;
      }
      x /= scale.x;
      y /= scale.y;
      z /= scale.z;
      if (glm::dot(glm::cross(x, y), z) < 0.0F) {
        scale.x = -scale.x;
        x = -x;
      }
      if (std::abs(glm::dot(x, y)) > kOrthogonalityTolerance
        || std::abs(glm::dot(x, z)) > kOrthogonalityTolerance
        || std::abs(glm::dot(y, z)) > kOrthogonalityTolerance) {
        return false;
      }
      rotation = glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
      return IsFinite(position) && IsFinite(scale) && IsFinite(rotation);
    }

    auto ToParent(const GizmoTargetStart& target, const glm::vec3& world)
      -> glm::vec3
    {
      return glm::vec3(glm::inverse(target.parent_world) * glm::vec4(world, 1.0F));
    }

    auto Lighten(const glm::vec4& color) -> glm::vec4
    {
      return { glm::mix(glm::vec3(color), glm::vec3(1.0F), 0.25F), color.a };
    }

    auto WithAlpha(const glm::vec4& color, const float alpha) -> glm::vec4
    {
      return { color.r, color.g, color.b, alpha };
    }

    //! Gizmo size in world units at the pivot.
    auto GizmoLength(const GizmoCamera& camera, const glm::vec3& pivot,
      const float display_scale) -> float
    {
      return kGizmoSizePixels * display_scale * camera.PixelSize(pivot);
    }

    auto AxisVisible(const GizmoCamera& camera, const GizmoFrame& frame,
      const int axis) -> bool
    {
      return std::abs(glm::dot(frame.Axis(axis), camera.ToEye(frame.pivot)))
        < kAxisHideCosine;
    }

    auto PlaneVisible(const GizmoCamera& camera, const GizmoFrame& frame,
      const PlaneAxes& plane) -> bool
    {
      return std::abs(
               glm::dot(frame.Axis(plane.normal), camera.ToEye(frame.pivot)))
        > kPlaneHideCosine;
    }

    auto PlaneCorners(const GizmoFrame& frame, const PlaneAxes& plane,
      const float length, const float inner, const float outer)
      -> std::array<glm::vec3, 4>
    {
      const auto a = frame.Axis(plane.first) * length;
      const auto b = frame.Axis(plane.second) * length;
      return {
        frame.pivot + a * inner + b * inner,
        frame.pivot + a * outer + b * inner,
        frame.pivot + a * outer + b * outer,
        frame.pivot + a * inner + b * outer,
      };
    }

    // -- Geometry -----------------------------------------------------------

    void AddLine(ViewOverlayLayer& layer, const glm::vec3& a, const glm::vec3& b,
      const float width, const glm::vec4& color)
    {
      layer.lines.push_back(ViewOverlayLine {
        .start = a,
        .width = width,
        .end = b,
        .pad0 = 0.0F,
        .color = color,
      });
    }

    void AddTriangle(ViewOverlayLayer& layer, const glm::vec3& a,
      const glm::vec3& b, const glm::vec3& c, const glm::vec4& color)
    {
      for (const auto& p : { a, b, c }) {
        layer.triangles.push_back(
          ViewOverlayVertex { .position = p, .pad0 = 0.0F, .color = color });
      }
    }

    //! A solid face lit a little by how much it faces the eye.
    void AddShadedTriangle(ViewOverlayLayer& layer, const GizmoCamera& camera,
      const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
      const glm::vec4& color)
    {
      auto normal = glm::cross(b - a, c - a);
      const auto length = glm::length(normal);
      auto shade = 0.85F;
      if (length > 1.0e-12F) {
        normal /= length;
        shade = 0.7F
          + 0.3F
            * std::abs(glm::dot(normal, camera.ToEye((a + b + c) / 3.0F)));
      }
      AddTriangle(layer, a, b, c,
        glm::vec4(glm::vec3(color) * shade, color.a));
    }

    void AddQuad(ViewOverlayLayer& layer, const std::array<glm::vec3, 4>& q,
      const glm::vec4& color)
    {
      AddTriangle(layer, q[0], q[1], q[2], color);
      AddTriangle(layer, q[0], q[2], q[3], color);
    }

    void AddQuadOutline(ViewOverlayLayer& layer,
      const std::array<glm::vec3, 4>& q, const float width,
      const glm::vec4& color)
    {
      for (std::size_t i = 0; i < q.size(); ++i) {
        AddLine(layer, q[i], q[(i + 1) % q.size()], width, color);
      }
    }

    void AddCone(ViewOverlayLayer& layer, const GizmoCamera& camera,
      const glm::vec3& base, const glm::vec3& direction, const float length,
      const float radius, const glm::vec4& color)
    {
      constexpr int kSegments = 14;
      const auto u = AnyPerpendicular(direction);
      const auto v = glm::cross(direction, u);
      const auto tip = base + direction * length;
      for (int i = 0; i < kSegments; ++i) {
        const auto a0 = kTwoPi * static_cast<float>(i) / kSegments;
        const auto a1 = kTwoPi * static_cast<float>(i + 1) / kSegments;
        const auto p0 = base + (u * std::cos(a0) + v * std::sin(a0)) * radius;
        const auto p1 = base + (u * std::cos(a1) + v * std::sin(a1)) * radius;
        AddShadedTriangle(layer, camera, p0, p1, tip, color);
        AddShadedTriangle(layer, camera, base, p1, p0, color);
      }
    }

    void AddBox(ViewOverlayLayer& layer, const GizmoCamera& camera,
      const glm::vec3& center, const GizmoFrame& frame, const float half,
      const glm::vec4& color)
    {
      const auto x = frame.Axis(0) * half;
      const auto y = frame.Axis(1) * half;
      const auto z = frame.Axis(2) * half;
      const auto corner = [&](const float sx, const float sy, const float sz) {
        return center + x * sx + y * sy + z * sz;
      };
      const std::array<std::array<glm::vec3, 4>, 6> faces { {
        { corner(1, -1, -1), corner(1, 1, -1), corner(1, 1, 1),
          corner(1, -1, 1) },
        { corner(-1, -1, -1), corner(-1, -1, 1), corner(-1, 1, 1),
          corner(-1, 1, -1) },
        { corner(-1, 1, -1), corner(-1, 1, 1), corner(1, 1, 1),
          corner(1, 1, -1) },
        { corner(-1, -1, -1), corner(1, -1, -1), corner(1, -1, 1),
          corner(-1, -1, 1) },
        { corner(-1, -1, 1), corner(1, -1, 1), corner(1, 1, 1),
          corner(-1, 1, 1) },
        { corner(-1, -1, -1), corner(-1, 1, -1), corner(1, 1, -1),
          corner(1, -1, -1) },
      } };
      for (const auto& face : faces) {
        AddShadedTriangle(layer, camera, face[0], face[1], face[2], color);
        AddShadedTriangle(layer, camera, face[0], face[2], face[3], color);
      }
    }

    //! A circle in the plane of `u` and `v`; when `front_only`, the half
    //! turned away from the eye is left out.
    void AddCircle(ViewOverlayLayer& layer, const GizmoCamera& camera,
      const glm::vec3& center, const glm::vec3& u, const glm::vec3& v,
      const float radius, const float width, const glm::vec4& color,
      const bool front_only)
    {
      const auto to_eye = camera.ToEye(center);
      auto previous = center + u * radius;
      for (int i = 1; i <= kRingSegments; ++i) {
        const auto angle = kTwoPi * static_cast<float>(i) / kRingSegments;
        const auto point
          = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        const auto middle = (previous + point) * 0.5F - center;
        if (!front_only || glm::dot(middle, to_eye) >= -0.02F * radius) {
          AddLine(layer, previous, point, width, color);
        }
        previous = point;
      }
    }

    //! A filled circular sector from `from` to `to` radians around the
    //! centre, measured from `u` toward `v`.
    void AddSector(ViewOverlayLayer& layer, const glm::vec3& center,
      const glm::vec3& u, const glm::vec3& v, const float radius,
      const float from, const float to, const glm::vec4& color)
    {
      const auto span = to - from;
      const auto steps = std::max(
        2, static_cast<int>(std::ceil(std::abs(span) / (kPi / 45.0F))));
      auto previous = center + (u * std::cos(from) + v * std::sin(from)) * radius;
      for (int i = 1; i <= steps; ++i) {
        const auto angle = from + span * static_cast<float>(i) / steps;
        const auto point
          = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        AddTriangle(layer, center, previous, point, color);
        previous = point;
      }
    }

    auto HandleColor(const GizmoHandle handle, const GizmoHandle hovered,
      const glm::vec4& color) -> glm::vec4
    {
      return handle == hovered ? kHoverColor : color;
    }

    // -- Handles ------------------------------------------------------------

    struct Context {
      const GizmoCamera& camera;
      const GizmoFrame& frame;
      float length { 1.0F };
      float pixel { 1.0F };
      float display_scale { 1.0F };
    };

    void AddTranslateAxis(ViewOverlayLayer& layer, const Context& ctx,
      const int axis, const glm::vec4& color)
    {
      const auto a = ctx.frame.Axis(axis);
      const auto& p = ctx.frame.pivot;
      AddLine(layer, p + a * (kShaftStart * ctx.length),
        p + a * (kArrowShaftEnd * ctx.length),
        kHandleStrokePixels * ctx.display_scale, color);
      AddCone(layer, ctx.camera, p + a * (kArrowShaftEnd * ctx.length), a,
        (kArrowTip - kArrowShaftEnd) * ctx.length, kArrowRadius * ctx.length,
        color);
    }

    void AddScaleAxis(ViewOverlayLayer& layer, const Context& ctx,
      const int axis, const glm::vec4& color)
    {
      const auto a = ctx.frame.Axis(axis);
      const auto& p = ctx.frame.pivot;
      AddLine(layer, p + a * (kShaftStart * ctx.length),
        p + a * (kScaleShaftEnd * ctx.length),
        kHandleStrokePixels * ctx.display_scale, color);
      AddBox(layer, ctx.camera, p + a * (kScaleBoxCenter * ctx.length),
        ctx.frame, kScaleBoxHalf * ctx.length, color);
    }

    void AddPlaneHandle(ViewOverlayLayer& layer, const Context& ctx,
      const PlaneAxes& plane, const float inner, const float outer,
      const bool hovered)
    {
      const auto corners = PlaneCorners(ctx.frame, plane, ctx.length, inner, outer);
      const auto& color = hovered ? kHoverColor : kAxisColors[plane.normal];
      AddQuad(layer, corners,
        WithAlpha(color, hovered ? kPlaneFillHoverAlpha : kPlaneFillAlpha));
      AddQuadOutline(
        layer, corners, kOutlineStrokePixels * ctx.display_scale, color);
    }

    void AddViewSquare(
      ViewOverlayLayer& layer, const Context& ctx, const bool hovered)
    {
      const auto half = kCenterHalfPixels * ctx.display_scale * ctx.pixel;
      const auto r = ctx.camera.Right() * half;
      const auto u = ctx.camera.Up() * half;
      const auto& p = ctx.frame.pivot;
      const std::array corners { p - r - u, p + r - u, p + r + u, p - r + u };
      const auto& color = hovered ? kHoverColor : kNeutralColor;
      AddQuad(layer, corners, WithAlpha(color, hovered ? 0.6F : 0.3F));
      AddQuadOutline(
        layer, corners, kOutlineStrokePixels * ctx.display_scale, color);
    }

    void AddAxisRing(ViewOverlayLayer& layer, const Context& ctx,
      const int axis, const glm::vec4& color, const bool front_only,
      const float width)
    {
      AddCircle(layer, ctx.camera, ctx.frame.pivot,
        ctx.frame.Axis((axis + 1) % 3), ctx.frame.Axis((axis + 2) % 3),
        kRingRadius * ctx.length, width, color, front_only);
    }

    void AddViewRing(ViewOverlayLayer& layer, const Context& ctx,
      const glm::vec4& color, const float width)
    {
      AddCircle(layer, ctx.camera, ctx.frame.pivot, ctx.camera.Right(),
        ctx.camera.Up(), kViewRingRadius * ctx.length, width, color, false);
    }

    void AddIdleGizmo(ViewOverlayLayer& layer, const Context& ctx,
      const TransformTool tool, const GizmoHandle hovered)
    {
      const auto stroke = kHandleStrokePixels * ctx.display_scale;
      switch (tool) {
      case TransformTool::kTranslate:
      case TransformTool::kScale: {
        const auto translate = tool == TransformTool::kTranslate;
        for (const auto handle : kPlaneHandles) {
          const auto plane = *PlaneOf(handle);
          if (PlaneVisible(ctx.camera, ctx.frame, plane)) {
            AddPlaneHandle(layer, ctx, plane,
              translate ? kTranslatePlaneNear : kScalePlaneNear,
              translate ? kTranslatePlaneFar : kScalePlaneFar,
              hovered == handle);
          }
        }
        for (int axis = 0; axis < 3; ++axis) {
          if (!AxisVisible(ctx.camera, ctx.frame, axis)) {
            continue;
          }
          const auto color
            = HandleColor(AxisHandle(axis), hovered, kAxisColors[axis]);
          if (translate) {
            AddTranslateAxis(layer, ctx, axis, color);
          } else {
            AddScaleAxis(layer, ctx, axis, color);
          }
        }
        if (translate) {
          AddViewSquare(layer, ctx, hovered == GizmoHandle::kCenter);
        } else {
          AddBox(layer, ctx.camera, ctx.frame.pivot, ctx.frame,
            kScaleCenterHalf * ctx.length,
            HandleColor(GizmoHandle::kCenter, hovered, kNeutralColor));
        }
        break;
      }
      case TransformTool::kRotate: {
        // A faint silhouette makes the rings read as one sphere.
        AddCircle(layer, ctx.camera, ctx.frame.pivot, ctx.camera.Right(),
          ctx.camera.Up(), kRingRadius * ctx.length,
          kGuideStrokePixels * ctx.display_scale,
          WithAlpha(kNeutralColor, 0.3F), false);
        for (int axis = 0; axis < 3; ++axis) {
          AddAxisRing(layer, ctx, axis,
            HandleColor(AxisHandle(axis), hovered, kAxisColors[axis]), true,
            stroke);
        }
        AddViewRing(layer, ctx,
          HandleColor(GizmoHandle::kView, hovered, kNeutralColor), stroke);
        break;
      }
      case TransformTool::kSelect:
        break;
      }
    }

    void AddGuide(ViewOverlayLayer& layer, const Context& ctx,
      const glm::vec3& direction, const glm::vec4& color)
    {
      const auto reach = kGuideLengthFactor * ctx.length;
      AddLine(layer, ctx.frame.pivot - direction * reach,
        ctx.frame.pivot + direction * reach,
        kGuideStrokePixels * ctx.display_scale, WithAlpha(color, 0.75F));
    }

    void AddRotationSweep(ViewOverlayLayer& layer, const glm::vec3& center,
      const glm::vec3& u, const glm::vec3& v, const float radius,
      const float degrees)
    {
      const auto magnitude = std::abs(degrees);
      const auto turns = std::floor(magnitude / 360.0F);
      const auto residual = magnitude - turns * 360.0F;
      const auto sign = degrees < 0.0F ? -1.0F : 1.0F;
      const auto residual_radians = glm::radians(residual) * sign;
      if (turns < 1.0F) {
        if (residual > 0.01F) {
          AddSector(layer, center, u, v, radius, 0.0F, residual_radians,
            glm::vec4(kSweepColor, kSweepAlpha));
        }
        return;
      }
      // Completed turns: one disk and the residual sweep with disjoint
      // coverage, never stacked fans.
      const auto background = std::min(kSweepAlpha * turns, kSweepMaxAlpha);
      const auto front = std::min(kSweepAlpha * (turns + 1.0F), kSweepMaxAlpha);
      if (residual <= 0.01F) {
        AddSector(layer, center, u, v, radius, 0.0F, kTwoPi * sign,
          glm::vec4(kSweepColor, background));
        return;
      }
      AddSector(layer, center, u, v, radius, 0.0F, residual_radians,
        glm::vec4(kSweepColor, front));
      AddSector(layer, center, u, v, radius, residual_radians, kTwoPi * sign,
        glm::vec4(kSweepColor, background));
    }

    void AddDragGizmo(ViewOverlayLayer& layer, const Context& ctx,
      const GizmoDrag& drag)
    {
      const auto handle = drag.Handle();
      const auto axis = AxisIndex(handle);
      const auto plane = PlaneOf(handle);
      const auto stroke = kHandleStrokePixels * ctx.display_scale;
      switch (drag.Tool()) {
      case TransformTool::kTranslate:
      case TransformTool::kScale: {
        const auto translate = drag.Tool() == TransformTool::kTranslate;
        if (axis >= 0) {
          AddGuide(layer, ctx, ctx.frame.Axis(axis), kAxisColors[axis]);
          const auto color = Lighten(kAxisColors[axis]);
          if (translate) {
            AddTranslateAxis(layer, ctx, axis, color);
          } else {
            AddScaleAxis(layer, ctx, axis, color);
          }
        } else if (plane.has_value()) {
          AddGuide(layer, ctx, ctx.frame.Axis(plane->first),
            kAxisColors[plane->first]);
          AddGuide(layer, ctx, ctx.frame.Axis(plane->second),
            kAxisColors[plane->second]);
          AddPlaneHandle(layer, ctx, *plane,
            translate ? kTranslatePlaneNear : kScalePlaneNear,
            translate ? kTranslatePlaneFar : kScalePlaneFar, true);
        } else if (translate) {
          AddViewSquare(layer, ctx, true);
        } else {
          AddBox(layer, ctx.camera, ctx.frame.pivot, ctx.frame,
            kScaleCenterHalf * ctx.length, kHoverColor);
        }
        break;
      }
      case TransformTool::kRotate: {
        // The captured plane, not the moving frame: the start spoke stays put
        // while the node turns.
        const auto& n = drag.RotationNormal();
        const auto& u = drag.RotationStart();
        const auto v = glm::cross(n, u);
        const auto radius = (axis >= 0 ? kRingRadius : kViewRingRadius)
          * ctx.length;
        const auto color = axis >= 0 ? kAxisColors[axis] : kNeutralColor;
        const auto& pivot = drag.StartFrame().pivot;
        const auto degrees = drag.AppliedDegrees();
        AddRotationSweep(layer, pivot, u, v, radius, degrees);
        AddCircle(layer, ctx.camera, pivot, u, v, radius, stroke, color, false);
        AddGuide(layer, ctx, n, color);
        AddLine(layer, pivot, pivot + u * radius,
          kGuideStrokePixels * ctx.display_scale, color);
        if (std::abs(degrees) > 0.01F) {
          const auto angle = glm::radians(degrees);
          AddLine(layer, pivot,
            pivot + (u * std::cos(angle) + v * std::sin(angle)) * radius,
            stroke, Lighten(color));
        }
        break;
      }
      case TransformTool::kSelect:
        break;
      }
    }

  } // namespace

  // -- GizmoCamera ----------------------------------------------------------

  auto GizmoCamera::Create(const glm::mat4& view, const glm::mat4& projection,
    const glm::vec2& viewport_origin, const glm::vec2& viewport_size)
    -> GizmoCamera
  {
    auto camera = GizmoCamera {};
    camera.view = view;
    camera.projection = projection;
    camera.viewport_origin = viewport_origin;
    camera.viewport_size = glm::max(viewport_size, glm::vec2(1.0F));
    camera.view_projection_ = projection * view;
    camera.inverse_view_projection_ = glm::inverse(camera.view_projection_);
    camera.inverse_view_ = glm::inverse(view);
    return camera;
  }

  auto GizmoCamera::IsOrthographic() const noexcept -> bool
  {
    return std::abs(projection[2][3]) < 1.0e-6F;
  }

  auto GizmoCamera::Position() const noexcept -> glm::vec3
  {
    return glm::vec3(inverse_view_[3]);
  }

  auto GizmoCamera::Forward() const noexcept -> glm::vec3
  {
    return -glm::normalize(glm::vec3(inverse_view_[2]));
  }

  auto GizmoCamera::Right() const noexcept -> glm::vec3
  {
    return glm::normalize(glm::vec3(inverse_view_[0]));
  }

  auto GizmoCamera::Up() const noexcept -> glm::vec3
  {
    return glm::normalize(glm::vec3(inverse_view_[1]));
  }

  auto GizmoCamera::ToEye(const glm::vec3& point) const noexcept -> glm::vec3
  {
    if (IsOrthographic()) {
      return -Forward();
    }
    const auto offset = Position() - point;
    const auto length = glm::length(offset);
    return length > 1.0e-6F ? offset / length : -Forward();
  }

  auto GizmoCamera::Project(const glm::vec3& point) const
    -> std::optional<glm::vec2>
  {
    const auto clip = view_projection_ * glm::vec4(point, 1.0F);
    if (clip.w <= 1.0e-6F) {
      return std::nullopt;
    }
    const auto ndc = glm::vec2(clip) / clip.w;
    return viewport_origin
      + glm::vec2 { (ndc.x * 0.5F + 0.5F) * viewport_size.x,
          (0.5F - ndc.y * 0.5F) * viewport_size.y };
  }

  auto GizmoCamera::Ray(const glm::vec2& pixel) const -> GizmoRay
  {
    const auto local = (pixel - viewport_origin) / viewport_size;
    const auto ndc = glm::vec2 { local.x * 2.0F - 1.0F, 1.0F - local.y * 2.0F };
    // Mid-depth is inside the frustum whichever way depth runs.
    const auto unprojected
      = inverse_view_projection_ * glm::vec4(ndc, 0.5F, 1.0F);
    const auto point = glm::vec3(unprojected) / unprojected.w;
    const auto forward = Forward();
    if (IsOrthographic()) {
      return GizmoRay {
        .origin = point - forward * glm::dot(point - Position(), forward),
        .direction = forward,
      };
    }
    return GizmoRay {
      .origin = Position(),
      .direction = glm::normalize(point - Position()),
    };
  }

  auto GizmoCamera::PixelSize(const glm::vec3& point) const -> float
  {
    const auto focal = std::max(std::abs(projection[1][1]), 1.0e-6F);
    if (IsOrthographic()) {
      return 2.0F / (focal * viewport_size.y);
    }
    const auto depth
      = std::max(glm::dot(point - Position(), Forward()), 1.0e-4F);
    return 2.0F * depth / (focal * viewport_size.y);
  }

  auto GizmoCamera::Unproject(const glm::vec2& pixel) const -> glm::vec3
  {
    const auto local = (pixel - viewport_origin) / viewport_size;
    const auto ndc = glm::vec2 { local.x * 2.0F - 1.0F, 1.0F - local.y * 2.0F };
    const auto unprojected
      = inverse_view_projection_ * glm::vec4(ndc, 0.5F, 1.0F);
    return glm::vec3(unprojected) / unprojected.w;
  }

  auto GizmoCamera::DeviceDepth(const glm::vec3& point) const -> float
  {
    const auto clip = view_projection_ * glm::vec4(point, 1.0F);
    return std::abs(clip.w) > 1.0e-6F ? clip.z / clip.w : 0.0F;
  }

  auto GizmoFrame::Axis(const int index) const -> glm::vec3
  {
    auto unit = glm::vec3 { 0.0F };
    unit[index] = 1.0F;
    return glm::normalize(orientation * unit);
  }

  // -- Hit test -------------------------------------------------------------

  auto HitTestGizmo(const GizmoCamera& camera, const TransformTool tool,
    const GizmoFrame& frame, const glm::vec2& pointer,
    const float display_scale) -> GizmoHandle
  {
    if (tool == TransformTool::kSelect) {
      return GizmoHandle::kNone;
    }
    const auto pivot = camera.Project(frame.pivot);
    if (!pivot.has_value()) {
      return GizmoHandle::kNone;
    }
    const auto length = GizmoLength(camera, frame.pivot, display_scale);
    const auto tolerance = kHitTolerancePixels * display_scale;
    auto best = GizmoHandle::kNone;
    auto best_distance = tolerance;
    const auto consider = [&](const GizmoHandle handle, const float distance) {
      if (distance <= best_distance) {
        best = handle;
        best_distance = distance;
      }
    };

    if (tool == TransformTool::kRotate) {
      for (int axis = 0; axis < 3; ++axis) {
        const auto u = frame.Axis((axis + 1) % 3);
        const auto v = frame.Axis((axis + 2) % 3);
        const auto radius = kRingRadius * length;
        const auto to_eye = camera.ToEye(frame.pivot);
        auto previous_world = frame.pivot + u * radius;
        auto previous = camera.Project(previous_world);
        for (int i = 1; i <= kRingSegments; ++i) {
          const auto angle = kTwoPi * static_cast<float>(i) / kRingSegments;
          const auto world
            = frame.pivot + (u * std::cos(angle) + v * std::sin(angle)) * radius;
          const auto current = camera.Project(world);
          const auto middle = (previous_world + world) * 0.5F - frame.pivot;
          if (previous.has_value() && current.has_value()
            && glm::dot(middle, to_eye) >= -0.02F * radius) {
            consider(
              AxisHandle(axis), DistanceToSegment(pointer, *previous, *current));
          }
          previous_world = world;
          previous = current;
        }
      }
      if (const auto edge
        = camera.Project(frame.pivot + camera.Right() * (kViewRingRadius * length));
        edge.has_value()) {
        const auto ring = glm::distance(*pivot, *edge);
        // Axis rings win ties: they are drawn inside the view ring.
        consider(GizmoHandle::kView,
          std::abs(glm::distance(pointer, *pivot) - ring) + 0.5F);
      }
      return best;
    }

    // Translate and scale: the centre first, then the planes, then the axes.
    if (glm::distance(pointer, *pivot) <= kCenterHitPixels * display_scale) {
      return GizmoHandle::kCenter;
    }
    const auto translate = tool == TransformTool::kTranslate;
    for (const auto handle : kPlaneHandles) {
      const auto plane = *PlaneOf(handle);
      if (!PlaneVisible(camera, frame, plane)) {
        continue;
      }
      const auto corners = PlaneCorners(frame, plane, length,
        translate ? kTranslatePlaneNear : kScalePlaneNear,
        translate ? kTranslatePlaneFar : kScalePlaneFar);
      std::array<glm::vec2, 4> projected {};
      auto complete = true;
      for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto p = camera.Project(corners[i]);
        complete = complete && p.has_value();
        if (p.has_value()) {
          projected[i] = *p;
        }
      }
      if (complete && InsideConvex(pointer, projected)) {
        return handle;
      }
    }
    for (int axis = 0; axis < 3; ++axis) {
      if (!AxisVisible(camera, frame, axis)) {
        continue;
      }
      const auto a = frame.Axis(axis);
      const auto start = camera.Project(frame.pivot + a * (kShaftStart * length));
      const auto end = camera.Project(frame.pivot + a * (kArrowTip * length));
      if (start.has_value() && end.has_value()) {
        consider(AxisHandle(axis), DistanceToSegment(pointer, *start, *end));
      }
    }
    return best;
  }

  // -- Drag -----------------------------------------------------------------

  auto GizmoDrag::Begin(const GizmoCamera& camera, const TransformTool tool,
    const TransformSpace space, const GizmoFrame& frame,
    const GizmoHandle handle, const glm::vec2& pointer,
    const float display_scale, std::vector<GizmoTargetStart> targets)
    -> std::optional<GizmoDrag>
  {
    if (tool == TransformTool::kSelect || handle == GizmoHandle::kNone
      || targets.empty()) {
      return std::nullopt;
    }
    auto drag = GizmoDrag {};
    drag.tool_ = tool;
    drag.space_ = space;
    drag.handle_ = handle;
    drag.start_frame_ = frame;
    drag.frame_ = frame;
    drag.display_scale_ = display_scale;
    drag.start_pointer_ = pointer;
    drag.pointer_ = pointer;
    drag.last_pointer_ = pointer;
    drag.results_.reserve(targets.size());
    for (const auto& target : targets) {
      drag.results_.push_back(GizmoTargetResult {
        .id = target.id,
        .position = target.local_position,
        .rotation = target.local_rotation,
        .scale = target.local_scale,
      });
    }
    drag.targets_ = std::move(targets);

    const auto to_eye = camera.ToEye(frame.pivot);
    const auto axis = AxisIndex(handle);
    const auto plane = PlaneOf(handle);
    const auto ray = camera.Ray(pointer);

    if (tool == TransformTool::kRotate) {
      drag.normal_ = axis >= 0 ? frame.Axis(axis) : to_eye;
      if (handle != GizmoHandle::kView && axis < 0) {
        return std::nullopt;
      }
      const auto hit = IntersectPlane(camera, ray, frame.pivot, drag.normal_);
      auto reference = glm::vec3 { 0.0F };
      if (hit.has_value()) {
        const auto offset = *hit - frame.pivot;
        reference = offset - drag.normal_ * glm::dot(offset, drag.normal_);
      }
      if (glm::length(reference) > 1.0e-6F) {
        drag.reference_ = glm::normalize(reference);
      } else {
        drag.reference_ = AnyPerpendicular(drag.normal_);
        drag.resync_angle_ = true;
      }
      drag.binormal_ = glm::cross(drag.normal_, drag.reference_);
      return drag;
    }

    if (handle == GizmoHandle::kCenter) {
      drag.normal_ = to_eye;
    } else if (axis >= 0) {
      const auto normal = AxisDragNormal(frame.Axis(axis), to_eye);
      if (!normal.has_value()) {
        return std::nullopt;
      }
      drag.normal_ = *normal;
    } else if (plane.has_value()) {
      drag.normal_ = frame.Axis(plane->normal);
    } else {
      return std::nullopt;
    }
    const auto hit = IntersectPlane(camera, ray, frame.pivot, drag.normal_);
    if (!hit.has_value()) {
      return std::nullopt;
    }
    drag.start_hit_ = *hit;

    if (tool == TransformTool::kScale && handle != GizmoHandle::kCenter) {
      const auto direction = axis >= 0
        ? frame.Axis(axis)
        : glm::normalize(frame.Axis(plane->first) + frame.Axis(plane->second));
      const auto length = GizmoLength(camera, frame.pivot, display_scale);
      if (std::abs(glm::dot(*hit - frame.pivot, direction))
        < 0.05F * length) {
        return std::nullopt;
      }
    }
    return drag;
  }

  auto GizmoDrag::Update(const GizmoCamera& camera, const glm::vec2& pointer,
    const TransformSnap& snap, const bool invert_snap) -> bool
  {
    last_pointer_ = pointer_;
    pointer_ = pointer;
    const auto snapped = snap.enabled != invert_snap;
    switch (tool_) {
    case TransformTool::kTranslate:
      return UpdateTranslate(camera, snapped, snap);
    case TransformTool::kRotate:
      return UpdateRotate(camera, snapped, snap);
    case TransformTool::kScale:
      return UpdateScale(camera, snapped, snap);
    case TransformTool::kSelect:
      break;
    }
    return false;
  }

  auto GizmoDrag::UpdateTranslate(const GizmoCamera& camera,
    const bool snapped, const TransformSnap& snap) -> bool
  {
    const auto& pivot = start_frame_.pivot;
    const auto hit
      = IntersectPlane(camera, camera.Ray(pointer_), pivot, normal_);
    if (!hit.has_value()) {
      return false;
    }
    auto delta = *hit - start_hit_;
    const auto axis = AxisIndex(handle_);
    const auto plane = PlaneOf(handle_);
    std::array<bool, 3> free { true, true, true };
    if (axis >= 0) {
      const auto a = start_frame_.Axis(axis);
      delta = a * glm::dot(delta, a);
      free = { axis == 0, axis == 1, axis == 2 };
    } else {
      delta -= normal_ * glm::dot(delta, normal_);
      if (plane.has_value()) {
        free = { plane->normal != 0, plane->normal != 1, plane->normal != 2 };
      }
    }

    const auto world = space_ == TransformSpace::kWorld;
    if (snapped && snap.translation > 0.0F) {
      if (world) {
        // World space: the pivot lands on the grid along the moved axes.
        auto moved = pivot + delta;
        for (int c = 0; c < 3; ++c) {
          if (free[c]) {
            moved[c] = SnapTo(moved[c], snap.translation);
          }
        }
        delta = moved - pivot;
      } else {
        auto snapped_delta = glm::vec3 { 0.0F };
        for (int c = 0; c < 3; ++c) {
          if (free[c]) {
            const auto a = start_frame_.Axis(c);
            snapped_delta += a * SnapTo(glm::dot(delta, a), snap.translation);
          }
        }
        delta = snapped_delta;
      }
    }

    readout_.axes = static_cast<std::uint8_t>(
      (free[0] ? 1U : 0U) | (free[1] ? 2U : 0U) | (free[2] ? 4U : 0U));
    readout_.values = world ? delta
                            : glm::vec3 { glm::dot(delta, start_frame_.Axis(0)),
                                glm::dot(delta, start_frame_.Axis(1)),
                                glm::dot(delta, start_frame_.Axis(2)) };

    std::vector<GizmoTargetResult> results;
    results.reserve(targets_.size());
    for (const auto& target : targets_) {
      results.push_back(GizmoTargetResult {
        .id = target.id,
        .position = ToParent(target, glm::vec3(target.world[3]) + delta),
        .rotation = target.local_rotation,
        .scale = target.local_scale,
      });
    }
    frame_.pivot = pivot + delta;
    return Commit(std::move(results));
  }

  auto GizmoDrag::UpdateRotate(const GizmoCamera& camera, const bool snapped,
    const TransformSnap& snap) -> bool
  {
    const auto& pivot = start_frame_.pivot;
    const auto pixel = camera.PixelSize(pivot);
    const auto ray = camera.Ray(pointer_);
    const auto hit = std::abs(glm::dot(ray.direction, normal_))
        > kRingPlaneMinCosine
      ? IntersectPlane(camera, ray, pivot, normal_)
      : std::nullopt;
    auto in_plane = glm::vec3 { 0.0F };
    if (hit.has_value()) {
      const auto offset = *hit - pivot;
      in_plane = offset - normal_ * glm::dot(offset, normal_);
    }

    if (glm::length(in_plane) > 3.0F * display_scale_ * pixel) {
      const auto angle = std::atan2(
        glm::dot(in_plane, binormal_), glm::dot(in_plane, reference_));
      if (resync_angle_) {
        resync_angle_ = false;
      } else {
        raw_radians_ += WrapAngle(angle - last_angle_);
      }
      last_angle_ = angle;
    } else {
      // Edge-on ring or a pointer at the pivot: follow the ring's tangent on
      // screen instead, and pick the plane up again without a jump.
      const auto radius = kRingRadius * GizmoLength(camera, pivot, display_scale_);
      const auto spoke = reference_ * std::cos(raw_radians_)
        + binormal_ * std::sin(raw_radians_);
      const auto tangent = glm::cross(normal_, spoke);
      const auto rim = camera.Project(pivot + spoke * radius);
      const auto ahead = camera.Project(pivot + spoke * radius + tangent * radius);
      const auto center = camera.Project(pivot);
      if (rim.has_value() && ahead.has_value() && center.has_value()) {
        const auto screen_tangent = *ahead - *rim;
        const auto tangent_length = glm::length(screen_tangent);
        const auto radius_pixels
          = std::max(glm::distance(*center, *rim), 1.0F);
        if (tangent_length > 1.0e-3F) {
          raw_radians_ += glm::dot(pointer_ - last_pointer_,
                            screen_tangent / tangent_length)
            / radius_pixels;
        }
      }
      resync_angle_ = true;
    }

    auto degrees = glm::degrees(raw_radians_);
    if (snapped && snap.rotation_degrees > 0.0F) {
      degrees = SnapTo(degrees, snap.rotation_degrees);
    }
    const auto axis = AxisIndex(handle_);
    readout_.axes = static_cast<std::uint8_t>(axis >= 0 ? (1U << axis) : 0U);
    readout_.values = glm::vec3 { degrees, 0.0F, 0.0F };
    const auto angle_changed = degrees != applied_degrees_;
    applied_degrees_ = degrees;

    const auto turn = glm::angleAxis(glm::radians(degrees), normal_);
    const auto about_pivot = glm::translate(glm::mat4(1.0F), pivot)
      * glm::mat4_cast(turn) * glm::translate(glm::mat4(1.0F), -pivot);
    std::vector<GizmoTargetResult> results;
    results.reserve(targets_.size());
    for (const auto& target : targets_) {
      auto result = GizmoTargetResult { .id = target.id };
      const auto local
        = glm::inverse(target.parent_world) * about_pivot * target.world;
      if (!DecomposeTrs(local, result.position, result.rotation, result.scale)) {
        const auto was_representable = representable_;
        representable_ = false;
        return was_representable || angle_changed;
      }
      if (glm::dot(result.rotation, target.local_rotation) < 0.0F) {
        result.rotation = -result.rotation;
      }
      results.push_back(result);
    }
    frame_.orientation = space_ == TransformSpace::kLocal
      ? glm::normalize(turn * start_frame_.orientation)
      : start_frame_.orientation;
    return Commit(std::move(results)) || angle_changed;
  }

  auto GizmoDrag::UpdateScale(const GizmoCamera& camera, const bool snapped,
    const TransformSnap& snap) -> bool
  {
    const auto& pivot = start_frame_.pivot;
    const auto axis = AxisIndex(handle_);
    const auto plane = PlaneOf(handle_);
    auto factor = 1.0F;
    std::array<bool, 3> free { true, true, true };
    if (handle_ == GizmoHandle::kCenter) {
      const auto moved = pointer_ - start_pointer_;
      factor = 1.0F + (moved.x - moved.y) / (kGizmoSizePixels * display_scale_);
    } else {
      const auto hit
        = IntersectPlane(camera, camera.Ray(pointer_), pivot, normal_);
      if (!hit.has_value()) {
        return false;
      }
      const auto direction = axis >= 0 ? start_frame_.Axis(axis)
                                       : glm::normalize(
                                           start_frame_.Axis(plane->first)
                                           + start_frame_.Axis(plane->second));
      factor = glm::dot(*hit - pivot, direction)
        / glm::dot(start_hit_ - pivot, direction);
      if (axis >= 0) {
        free = { axis == 0, axis == 1, axis == 2 };
      } else {
        free = { plane->normal != 0, plane->normal != 1, plane->normal != 2 };
      }
    }
    if (snapped && snap.scale > 0.0F) {
      factor = 1.0F + SnapTo(factor - 1.0F, snap.scale);
    }

    auto factors = glm::vec3 { 1.0F };
    for (int c = 0; c < 3; ++c) {
      if (free[c]) {
        factors[c] = factor;
      }
    }
    readout_.axes = static_cast<std::uint8_t>(
      (free[0] ? 1U : 0U) | (free[1] ? 2U : 0U) | (free[2] ? 4U : 0U));
    readout_.values = factors;
    // A zero or mirrored scale is not a scale the gizmo produces.
    if (!std::isfinite(factor) || factor <= 1.0e-3F) {
      const auto was_representable = representable_;
      representable_ = false;
      return was_representable;
    }

    // Each node scales along its own axes; positions spread from the pivot
    // along the gizmo's axes.
    const auto basis = glm::mat3_cast(start_frame_.orientation);
    std::vector<GizmoTargetResult> results;
    results.reserve(targets_.size());
    for (const auto& target : targets_) {
      const auto offset = glm::vec3(target.world[3]) - pivot;
      const auto moved
        = pivot + basis * (factors * (glm::transpose(basis) * offset));
      results.push_back(GizmoTargetResult {
        .id = target.id,
        .position = ToParent(target, moved),
        .rotation = target.local_rotation,
        .scale = target.local_scale * factors,
      });
    }
    return Commit(std::move(results));
  }

  auto GizmoDrag::Commit(std::vector<GizmoTargetResult> results) -> bool
  {
    for (const auto& result : results) {
      if (!IsFinite(result.position) || !IsFinite(result.rotation)
        || !IsFinite(result.scale)) {
        const auto was_representable = representable_;
        representable_ = false;
        return was_representable;
      }
    }
    auto changed = !representable_ || results.size() != results_.size();
    for (std::size_t i = 0; !changed && i < results.size(); ++i) {
      changed = results[i].position != results_[i].position
        || results[i].rotation != results_[i].rotation
        || results[i].scale != results_[i].scale;
    }
    results_ = std::move(results);
    representable_ = true;
    return changed;
  }

  // -- Overlay --------------------------------------------------------------

  void BuildGizmoOverlay(const GizmoCamera& camera, const GizmoVisual& visual,
    ViewOverlay& overlay)
  {
    if (visual.tool == TransformTool::kSelect) {
      return;
    }
    const auto& frame
      = visual.drag != nullptr ? visual.drag->Frame() : visual.frame;
    if (!camera.Project(frame.pivot).has_value()) {
      return;
    }
    const auto ctx = Context {
      .camera = camera,
      .frame = frame,
      .length = GizmoLength(camera, frame.pivot, visual.display_scale),
      .pixel = camera.PixelSize(frame.pivot),
      .display_scale = visual.display_scale,
    };
    if (visual.drag != nullptr) {
      AddDragGizmo(overlay.top, ctx, *visual.drag);
      return;
    }
    AddIdleGizmo(overlay.scene, ctx, visual.tool, visual.hovered);
  }

} // namespace oxygen::interop::module
