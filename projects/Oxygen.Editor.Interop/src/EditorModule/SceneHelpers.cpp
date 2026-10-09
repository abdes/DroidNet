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
#include <glm/mat3x3.hpp>
#include <glm/vec4.hpp>

#include <EditorModule/SceneHelpers.h>

namespace oxygen::interop::module {

  namespace {

    using vortex::ViewOverlay;
    using vortex::ViewOverlayLayer;
    using vortex::ViewOverlayLine;
    using vortex::ViewOverlayVertex;

    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kTwoPi = 2.0F * kPi;

    // Pixel sizes, before display scaling.
    constexpr float kIconStrokePixels = 1.75F;
    constexpr float kIconRingPixels = 2.0F;
    constexpr float kHelperStrokePixels = 1.5F;
    constexpr float kActiveStrokePixels = 2.0F;
    constexpr float kHandleHalfPixels = 5.0F;
    constexpr float kHandleHitPixels = 9.0F;
    constexpr float kSilhouetteHitPixels = 6.0F;
    constexpr float kArrowPixels = 90.0F;
    constexpr float kArrowHeadPixels = 14.0F;
    constexpr float kTriadMarginPixels = 46.0F;
    constexpr float kTriadLetterHalfPixels = 4.5F;
    constexpr float kTriadLetterGapPixels = 8.0F;
    constexpr float kTriadHitPixels = 9.0F;
    constexpr float kTriadStrokePixels = 1.75F;
    constexpr float kTriadHoverStrokePixels = 2.5F;

    // A camera's frame is drawn this far in front of it, inside its range.
    constexpr float kCameraFrameDistance = 1.0F;
    constexpr int kCircleSegments = 64;
    constexpr int kDiscSegments = 16;
    // Great circles of a range sphere are drawn this much fainter.
    constexpr float kGreatCircleAlpha = 0.35F;
    constexpr float kInnerConeAlpha = 0.55F;
    // Cone angles closer than this are made equal by a drag.
    constexpr float kConeMergeRadians = 1.0e-3F;

    const glm::vec4 kAxisColors[3] = {
      { 0.92F, 0.26F, 0.26F, 1.0F },
      { 0.42F, 0.80F, 0.22F, 1.0F },
      { 0.26F, 0.52F, 0.96F, 1.0F },
    };
    // The selection outline's colours.
    const glm::vec4 kSelectedColor { 0.96F, 0.55F, 0.15F, 1.0F };
    const glm::vec4 kActiveColor { 1.0F, 0.80F, 0.35F, 1.0F };
    const glm::vec4 kHoverColor { 1.0F, 0.84F, 0.24F, 1.0F };
    const glm::vec4 kCameraColor { 0.86F, 0.88F, 0.91F, 1.0F };
    const glm::vec4 kFogColor { 0.72F, 0.82F, 0.92F, 1.0F };
    const glm::vec4 kIconBackdrop { 0.07F, 0.08F, 0.09F, 0.55F };

    auto IsFinite(const glm::vec3& value) -> bool
    {
      return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
    }

    auto WithAlpha(const glm::vec4& color, const float alpha) -> glm::vec4
    {
      return { color.r, color.g, color.b, alpha };
    }

    auto AnyPerpendicular(const glm::vec3& n) -> glm::vec3
    {
      const auto seed = std::abs(n.z) < 0.9F ? glm::vec3 { 0.0F, 0.0F, 1.0F }
                                             : glm::vec3 { 1.0F, 0.0F, 0.0F };
      return glm::normalize(glm::cross(n, seed));
    }

    //! A light's colour scaled to full brightness, so a dim light's icon
    //! still reads; a black light is drawn grey.
    auto Tint(const SceneHelperNode& node) -> glm::vec4
    {
      if (node.kind == SceneHelperKind::kCamera) {
        return kCameraColor;
      }
      if (node.kind == SceneHelperKind::kLocalFogVolume) {
        return kFogColor;
      }
      const auto peak = std::max({ node.color.r, node.color.g, node.color.b });
      if (!std::isfinite(peak) || peak <= 1.0e-4F) {
        return { 0.5F, 0.5F, 0.5F, 1.0F };
      }
      return { glm::clamp(node.color / peak, 0.0F, 1.0F), 1.0F };
    }

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

    void AddCircle(ViewOverlayLayer& layer, const glm::vec3& center,
      const glm::vec3& u, const glm::vec3& v, const float radius,
      const float width, const glm::vec4& color)
    {
      auto previous = center + u * radius;
      for (int i = 1; i <= kCircleSegments; ++i) {
        const auto angle = kTwoPi * static_cast<float>(i) / kCircleSegments;
        const auto point
          = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        AddLine(layer, previous, point, width, color);
        previous = point;
      }
    }

    //! Places screen-sized glyphs around a world point: one unit is one
    //! pixel, x runs right and y up on screen.
    struct Billboard {
      glm::vec3 origin { 0.0F };
      glm::vec3 right { 0.0F };
      glm::vec3 up { 0.0F };

      [[nodiscard]] auto At(const float x, const float y) const -> glm::vec3
      {
        return origin + right * x + up * y;
      }

      void Line(ViewOverlayLayer& layer, const glm::vec2& a, const glm::vec2& b,
        const float width, const glm::vec4& color) const
      {
        AddLine(layer, At(a.x, a.y), At(b.x, b.y), width, color);
      }

      void Disc(ViewOverlayLayer& layer, const glm::vec2& center,
        const float radius, const glm::vec4& color) const
      {
        const auto c = At(center.x, center.y);
        for (int i = 0; i < kDiscSegments; ++i) {
          const auto a0 = kTwoPi * static_cast<float>(i) / kDiscSegments;
          const auto a1 = kTwoPi * static_cast<float>(i + 1) / kDiscSegments;
          AddTriangle(layer, c,
            At(center.x + std::cos(a0) * radius,
              center.y + std::sin(a0) * radius),
            At(center.x + std::cos(a1) * radius,
              center.y + std::sin(a1) * radius),
            color);
        }
      }

      void Ring(ViewOverlayLayer& layer, const float radius, const float width,
        const glm::vec4& color) const
      {
        AddCircle(layer, origin, right, up, radius, width, color);
      }

      void Square(ViewOverlayLayer& layer, const float half,
        const glm::vec4& color) const
      {
        AddTriangle(layer, At(-half, -half), At(half, -half), At(half, half),
          color);
        AddTriangle(layer, At(-half, -half), At(half, half), At(-half, half),
          color);
      }
    };

    auto MakeBillboard(const GizmoCamera& camera, const glm::vec3& point,
      const float display_scale) -> Billboard
    {
      const auto pixel = camera.PixelSize(point) * display_scale;
      return Billboard {
        .origin = point,
        .right = camera.Right() * pixel,
        .up = camera.Up() * pixel,
      };
    }

    auto IntersectPlane(const GizmoCamera& camera, const glm::vec2& pointer,
      const glm::vec3& point, const glm::vec3& normal)
      -> std::optional<glm::vec3>
    {
      const auto ray = camera.Ray(pointer);
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

    //! The plane holding a light's axis that faces the eye best; none when
    //! the axis points at the eye.
    auto AxisPlaneNormal(const GizmoCamera& camera, const glm::vec3& apex,
      const glm::vec3& axis) -> std::optional<glm::vec3>
    {
      const auto to_eye = camera.ToEye(apex);
      const auto normal = to_eye - axis * glm::dot(to_eye, axis);
      const auto length = glm::length(normal);
      if (length < 0.05F) {
        return std::nullopt;
      }
      return normal / length;
    }

    //! The side of a spot light's cone its outer handle sits on: across the
    //! axis in the plane facing the eye, toward the top of the screen.
    auto ConeSide(const GizmoCamera& camera, const SceneHelperNode& node)
      -> glm::vec3
    {
      const auto& axis = node.direction;
      auto side = glm::vec3 { 0.0F };
      if (const auto normal = AxisPlaneNormal(camera, node.position, axis)) {
        side = glm::normalize(glm::cross(*normal, axis));
      } else {
        const auto up = camera.Up();
        side = up - axis * glm::dot(up, axis);
        side = glm::length(side) > 1.0e-4F ? glm::normalize(side)
                                           : AnyPerpendicular(axis);
      }
      const auto preference
        = glm::dot(side, camera.Up()) + 0.01F * glm::dot(side, camera.Right());
      return preference < 0.0F ? -side : side;
    }

    //! The unit direction from a sphere's centre to its silhouette's right
    //! side.
    auto SilhouetteSide(const GizmoCamera& camera, const glm::vec3& center)
      -> glm::vec3
    {
      const auto to_eye = camera.ToEye(center);
      auto side = camera.Right() - to_eye * glm::dot(camera.Right(), to_eye);
      return glm::length(side) > 1.0e-4F ? glm::normalize(side)
                                         : camera.Right();
    }

    auto ConeAngle(const SceneHelperNode& node, const HelperHandle handle)
      -> float
    {
      return handle == HelperHandle::kInnerCone ? node.inner_cone
                                                : node.outer_cone;
    }

    auto HasHandle(const SceneHelperNode& node, const HelperHandle handle)
      -> bool
    {
      switch (node.kind) {
      case SceneHelperKind::kPointLight:
        return handle == HelperHandle::kRange;
      case SceneHelperKind::kSpotLight:
        return handle != HelperHandle::kNone;
      default:
        return false;
      }
    }

    constexpr std::array kHandles {
      HelperHandle::kRange,
      HelperHandle::kOuterCone,
      HelperHandle::kInnerCone,
    };

    // -- Icons --------------------------------------------------------------

    void AddIconGlyph(ViewOverlayLayer& layer, const Billboard& b,
      const SceneHelperNode& node, const float scale)
    {
      const auto color = Tint(node);
      const auto stroke = kIconStrokePixels * scale;
      switch (node.kind) {
      case SceneHelperKind::kPointLight: {
        // A bulb radiating in every direction.
        b.Disc(layer, { 0.0F, 0.0F }, 4.5F, color);
        for (int i = 0; i < 8; ++i) {
          const auto angle = kTwoPi * static_cast<float>(i) / 8.0F;
          const auto d = glm::vec2 { std::cos(angle), std::sin(angle) };
          b.Line(layer, d * 7.5F, d * 11.0F, stroke, color);
        }
        break;
      }
      case SceneHelperKind::kSpotLight: {
        // A lamp shade shining down.
        b.Disc(layer, { 0.0F, 6.0F }, 3.5F, color);
        b.Line(layer, { -3.5F, 5.0F }, { -9.0F, -4.0F }, stroke, color);
        b.Line(layer, { 3.5F, 5.0F }, { 9.0F, -4.0F }, stroke, color);
        b.Line(layer, { -9.0F, -4.0F }, { 9.0F, -4.0F }, stroke, color);
        b.Line(layer, { -5.0F, -7.0F }, { -7.0F, -11.0F }, stroke, color);
        b.Line(layer, { 0.0F, -7.0F }, { 0.0F, -11.5F }, stroke, color);
        b.Line(layer, { 5.0F, -7.0F }, { 7.0F, -11.0F }, stroke, color);
        break;
      }
      case SceneHelperKind::kDirectionalLight: {
        // A sun with parallel rays.
        b.Disc(layer, { 0.0F, 6.0F }, 4.5F, color);
        b.Line(layer, { -7.0F, -1.0F }, { -7.0F, -11.0F }, stroke, color);
        b.Line(layer, { 0.0F, -2.5F }, { 0.0F, -11.5F }, stroke, color);
        b.Line(layer, { 7.0F, -1.0F }, { 7.0F, -11.0F }, stroke, color);
        break;
      }
      case SceneHelperKind::kLocalFogVolume: {
        // Drifting bands of fog, as on a weather map.
        b.Line(layer, { -6.0F, 7.0F }, { 9.0F, 7.0F }, stroke, color);
        b.Line(layer, { -10.0F, 2.5F }, { 6.0F, 2.5F }, stroke, color);
        b.Line(layer, { -7.0F, -2.0F }, { 10.0F, -2.0F }, stroke, color);
        b.Line(layer, { -10.0F, -6.5F }, { 4.0F, -6.5F }, stroke, color);
        b.Line(layer, { -4.0F, -11.0F }, { 8.0F, -11.0F }, stroke, color);
        break;
      }
      case SceneHelperKind::kCamera: {
        // A camera body and its lens.
        const std::array<glm::vec2, 4> body { {
          { -10.0F, -6.0F },
          { 3.0F, -6.0F },
          { 3.0F, 5.0F },
          { -10.0F, 5.0F },
        } };
        for (std::size_t i = 0; i < body.size(); ++i) {
          b.Line(layer, body[i], body[(i + 1) % body.size()], stroke, color);
        }
        b.Line(layer, { 3.0F, -1.0F }, { 10.0F, -6.0F }, stroke, color);
        b.Line(layer, { 10.0F, -6.0F }, { 10.0F, 5.0F }, stroke, color);
        b.Line(layer, { 10.0F, 5.0F }, { 3.0F, 1.0F }, stroke, color);
        b.Disc(layer, { -6.0F, 8.5F }, 2.5F, color);
        b.Disc(layer, { -0.5F, 8.5F }, 2.5F, color);
        break;
      }
      }
    }

    // -- Helpers ------------------------------------------------------------

    void AddHandle(ViewOverlayLayer& layer, const GizmoCamera& camera,
      const glm::vec3& position, const bool hovered, const glm::vec4& color,
      const float scale)
    {
      const auto b = MakeBillboard(camera, position, scale);
      b.Square(layer, kHandleHalfPixels + 1.0F, kIconBackdrop);
      b.Square(layer, kHandleHalfPixels, hovered ? kHoverColor : color);
    }

    void AddDirectionalHelper(ViewOverlayLayer& layer,
      const GizmoCamera& camera, const SceneHelperNode& node,
      const glm::vec4& color, const float width, const float scale)
    {
      const auto pixel = camera.PixelSize(node.position) * scale;
      const auto length = kArrowPixels * pixel;
      const auto tip = node.position + node.direction * length;
      AddLine(layer, node.position, tip, width, color);
      auto side = glm::cross(node.direction, camera.ToEye(node.position));
      if (glm::length(side) < 1.0e-3F) {
        // Seen along its direction: the arrow is a point, so ring it.
        MakeBillboard(camera, node.position, scale)
          .Ring(layer, kArrowHeadPixels, width, color);
        return;
      }
      side = glm::normalize(side);
      const auto head = kArrowHeadPixels * pixel;
      const auto back = tip - node.direction * head;
      AddLine(layer, tip, back + side * head * 0.5F, width, color);
      AddLine(layer, tip, back - side * head * 0.5F, width, color);
    }

    void AddPointHelper(ViewOverlayLayer& layer, const GizmoCamera& camera,
      const SceneHelperNode& node, const glm::vec4& color, const float width)
    {
      const auto to_eye = camera.ToEye(node.position);
      const auto u = SilhouetteSide(camera, node.position);
      const auto v = glm::normalize(glm::cross(to_eye, u));
      AddCircle(layer, node.position, u, v, node.range, width, color);
      const auto faint = WithAlpha(color, color.a * kGreatCircleAlpha);
      const glm::vec3 x { 1.0F, 0.0F, 0.0F };
      const glm::vec3 y { 0.0F, 1.0F, 0.0F };
      const glm::vec3 z { 0.0F, 0.0F, 1.0F };
      AddCircle(layer, node.position, x, y, node.range, width, faint);
      AddCircle(layer, node.position, x, z, node.range, width, faint);
      AddCircle(layer, node.position, y, z, node.range, width, faint);
    }

    void AddCone(ViewOverlayLayer& layer, const SceneHelperNode& node,
      const glm::vec3& side, const float angle, const glm::vec4& color,
      const float width, const bool all_edges)
    {
      const auto& axis = node.direction;
      const auto other = glm::normalize(glm::cross(axis, side));
      const auto base = node.position + axis * (node.range * std::cos(angle));
      const auto radius = node.range * std::sin(angle);
      AddCircle(layer, base, side, other, radius, width, color);
      AddLine(layer, node.position, base + side * radius, width, color);
      AddLine(layer, node.position, base - side * radius, width, color);
      if (all_edges) {
        AddLine(layer, node.position, base + other * radius, width, color);
        AddLine(layer, node.position, base - other * radius, width, color);
      }
    }

    void AddSpotHelper(ViewOverlayLayer& layer, const GizmoCamera& camera,
      const SceneHelperNode& node, const glm::vec4& color, const float width)
    {
      const auto side = ConeSide(camera, node);
      AddLine(layer, node.position,
        node.position + node.direction * node.range, width,
        WithAlpha(color, color.a * kInnerConeAlpha));
      AddCone(layer, node, side, node.outer_cone, color, width, true);
      AddCone(layer, node, side, node.inner_cone,
        WithAlpha(color, color.a * kInnerConeAlpha), width, false);
    }

    void AddCameraHelper(ViewOverlayLayer& layer, const SceneHelperNode& node,
      const glm::vec4& color, const float width)
    {
      if (!node.has_frustum) {
        return;
      }
      const auto& f = node.frustum;
      const auto faint = WithAlpha(color, color.a * 0.6F);
      for (std::size_t i = 0; i < 4; ++i) {
        AddLine(layer, f[i], f[(i + 1) % 4], width, faint);
        AddLine(layer, f[4 + i], f[4 + (i + 1) % 4], width, faint);
        AddLine(layer, f[i], f[4 + i], width, faint);
      }

      // The frame shows the camera's aspect ratio, and a triangle its up.
      std::array<glm::vec3, 4> frame {};
      for (std::size_t i = 0; i < 4; ++i) {
        const auto near_depth = glm::dot(f[i] - node.position, node.direction);
        const auto far_depth
          = glm::dot(f[4 + i] - node.position, node.direction);
        const auto span = far_depth - near_depth;
        const auto t = span > 1.0e-6F
          ? std::clamp((kCameraFrameDistance - near_depth) / span, 0.0F, 1.0F)
          : 0.0F;
        frame[i] = f[i] + (f[4 + i] - f[i]) * t;
        AddLine(layer, node.position, frame[i], width, faint);
      }
      for (std::size_t i = 0; i < 4; ++i) {
        AddLine(layer, frame[i], frame[(i + 1) % 4], width * 1.5F, color);
      }
      const auto top_mid = (frame[2] + frame[3]) * 0.5F;
      const auto half_width = (frame[2] - frame[3]) * 0.2F;
      const auto rise = (frame[3] - frame[0]) * 0.25F;
      AddTriangle(
        layer, top_mid - half_width, top_mid + half_width, top_mid + rise, faint);
    }

    // -- Triad --------------------------------------------------------------

    struct TriadLetter {
      int axis { 0 };
      //! Unit screen direction, y down, scaled by how much it faces the
      //! screen.
      glm::vec2 offset { 0.0F };
      //! Toward the eye when positive.
      float facing { 0.0F };
    };

    auto TriadLetters(const GizmoCamera& camera) -> std::array<TriadLetter, 3>
    {
      const auto rotation = glm::mat3(camera.view);
      std::array<TriadLetter, 3> letters {};
      for (int i = 0; i < 3; ++i) {
        auto unit = glm::vec3 { 0.0F };
        unit[i] = 1.0F;
        const auto v = rotation * unit;
        letters[static_cast<std::size_t>(i)] = TriadLetter {
          .axis = i,
          .offset = { v.x, -v.y },
          .facing = v.z,
        };
      }
      // Back to front.
      std::ranges::sort(letters, {}, &TriadLetter::facing);
      return letters;
    }

    void AddLetter(ViewOverlayLayer& layer, const GizmoCamera& camera,
      const int axis, const glm::vec2& center, const float half,
      const float width, const glm::vec4& color)
    {
      // Strokes in pixels around the centre, y down.
      const auto line = [&](const glm::vec2& a, const glm::vec2& b) {
        AddLine(layer, camera.Unproject(center + a * half),
          camera.Unproject(center + b * half), width, color);
      };
      switch (axis) {
      case 0:
        line({ -1.0F, -1.0F }, { 1.0F, 1.0F });
        line({ -1.0F, 1.0F }, { 1.0F, -1.0F });
        break;
      case 1:
        line({ -1.0F, -1.0F }, { 0.0F, 0.0F });
        line({ 1.0F, -1.0F }, { 0.0F, 0.0F });
        line({ 0.0F, 0.0F }, { 0.0F, 1.0F });
        break;
      default:
        line({ -1.0F, -1.0F }, { 1.0F, -1.0F });
        line({ 1.0F, -1.0F }, { -1.0F, 1.0F });
        line({ -1.0F, 1.0F }, { 1.0F, 1.0F });
        break;
      }
    }

  } // namespace

  // -- Icons ----------------------------------------------------------------

  auto PickHelperIcons(const GizmoCamera& camera,
    const std::span<const SceneHelperNode> nodes, const glm::vec2& rect_min,
    const glm::vec2& rect_max, const float display_scale)
    -> std::vector<HelperIconHit>
  {
    const auto radius = kHelperIconPixels * 0.5F * display_scale;
    const auto rect_center = (rect_min + rect_max) * 0.5F;
    std::vector<HelperIconHit> hits;
    for (const auto& node : nodes) {
      const auto center = camera.Project(node.position);
      if (!center.has_value()) {
        continue;
      }
      const auto outside = glm::max(
        glm::max(rect_min - *center, *center - rect_max), glm::vec2(0.0F));
      if (glm::length(outside) > radius) {
        continue;
      }
      hits.push_back(HelperIconHit {
        .id = node.id,
        .center_distance
        = std::max(glm::distance(*center, rect_center) - radius, 0.0F),
        .depth = camera.DeviceDepth(node.position),
      });
    }
    return hits;
  }

  void BuildHelperIcons(const GizmoCamera& camera,
    const std::span<const SceneHelperNode> nodes, const float display_scale,
    ViewOverlay& overlay)
  {
    const auto radius = kHelperIconPixels * 0.5F;
    for (const auto& node : nodes) {
      if (!camera.Project(node.position).has_value()) {
        continue;
      }
      const auto b = MakeBillboard(camera, node.position, display_scale);
      b.Disc(overlay.scene, { 0.0F, 0.0F }, radius, kIconBackdrop);
      if (node.selected) {
        b.Ring(overlay.scene, radius, kIconRingPixels * display_scale,
          node.active ? kActiveColor : kSelectedColor);
      }
      AddIconGlyph(overlay.scene, b, node, display_scale);
    }
  }

  // -- Handles --------------------------------------------------------------

  auto HelperHandlePosition(const GizmoCamera& camera,
    const SceneHelperNode& node, const HelperHandle handle)
    -> std::optional<glm::vec3>
  {
    if (!HasHandle(node, handle)) {
      return std::nullopt;
    }
    if (node.kind == SceneHelperKind::kPointLight) {
      return node.position + SilhouetteSide(camera, node.position) * node.range;
    }
    if (handle == HelperHandle::kRange) {
      return node.position + node.direction * node.range;
    }
    const auto angle = ConeAngle(node, handle);
    const auto side = ConeSide(camera, node)
      * (handle == HelperHandle::kInnerCone ? -1.0F : 1.0F);
    return node.position + node.direction * (node.range * std::cos(angle))
      + side * (node.range * std::sin(angle));
  }

  auto HitTestHelperHandles(const GizmoCamera& camera,
    const std::span<const SceneHelperNode> nodes, const glm::vec2& pointer,
    const float display_scale) -> std::optional<HelperHandleHit>
  {
    auto best = std::optional<HelperHandleHit> {};
    auto best_distance = kHandleHitPixels * display_scale;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const auto& node = nodes[i];
      if (!node.editable) {
        continue;
      }
      for (const auto handle : kHandles) {
        const auto position = HelperHandlePosition(camera, node, handle);
        if (!position.has_value()) {
          continue;
        }
        const auto pixel = camera.Project(*position);
        if (!pixel.has_value()) {
          continue;
        }
        const auto distance = glm::distance(*pixel, pointer);
        if (distance <= best_distance) {
          best_distance = distance;
          best = HelperHandleHit { .node = i, .handle = handle };
        }
      }
    }
    if (best.has_value()) {
      return best;
    }

    // A point light's range is also grabbed on its silhouette circle.
    best_distance = kSilhouetteHitPixels * display_scale;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const auto& node = nodes[i];
      if (!node.editable || node.kind != SceneHelperKind::kPointLight) {
        continue;
      }
      const auto center = camera.Project(node.position);
      const auto edge = camera.Project(
        node.position + SilhouetteSide(camera, node.position) * node.range);
      if (!center.has_value() || !edge.has_value()) {
        continue;
      }
      const auto distance = std::abs(glm::distance(pointer, *center)
        - glm::distance(*edge, *center));
      if (distance <= best_distance) {
        best_distance = distance;
        best = HelperHandleHit { .node = i, .handle = HelperHandle::kRange };
      }
    }
    return best;
  }

  // -- Drag -----------------------------------------------------------------

  auto HelperDrag::Begin(const GizmoCamera& camera, const SceneHelperNode& node,
    const HelperHandle handle, const glm::vec2& pointer)
    -> std::optional<HelperDrag>
  {
    if (!HasHandle(node, handle)) {
      return std::nullopt;
    }
    auto drag = HelperDrag {};
    drag.node_ = node;
    drag.handle_ = handle;
    drag.pointer_ = pointer;
    if (node.kind == SceneHelperKind::kPointLight) {
      drag.plane_point_ = node.position;
      drag.plane_normal_ = camera.ToEye(node.position);
      drag.start_value_ = node.range;
    } else if (handle == HelperHandle::kRange) {
      const auto normal = AxisPlaneNormal(camera, node.position, node.direction);
      if (!normal.has_value()) {
        return std::nullopt;
      }
      drag.plane_point_ = node.position;
      drag.plane_normal_ = *normal;
      drag.start_value_ = node.range;
    } else {
      // Cone angles are read in the plane holding the axis; looking down the
      // axis, in the plane across the cone's base.
      const auto angle = ConeAngle(node, handle);
      if (const auto normal
        = AxisPlaneNormal(camera, node.position, node.direction)) {
        drag.plane_point_ = node.position;
        drag.plane_normal_ = *normal;
      } else {
        drag.plane_point_
          = node.position + node.direction * (node.range * std::cos(angle));
        drag.plane_normal_ = node.direction;
      }
      drag.start_value_ = angle;
    }
    const auto hit
      = IntersectPlane(camera, pointer, drag.plane_point_, drag.plane_normal_);
    if (!hit.has_value()) {
      return std::nullopt;
    }
    drag.start_measure_ = drag.Measure(*hit);
    drag.value_ = drag.start_value_;
    return drag;
  }

  auto HelperDrag::Update(const GizmoCamera& camera, const glm::vec2& pointer)
    -> bool
  {
    pointer_ = pointer;
    const auto hit = IntersectPlane(camera, pointer, plane_point_, plane_normal_);
    if (!hit.has_value()) {
      return false;
    }
    const auto previous = value_;
    Apply(start_value_ + (Measure(*hit) - start_measure_));
    return value_ != previous;
  }

  auto HelperDrag::Measure(const glm::vec3& hit) const -> float
  {
    const auto offset = hit - node_.position;
    if (node_.kind == SceneHelperKind::kPointLight) {
      return glm::length(offset);
    }
    const auto axial = glm::dot(offset, node_.direction);
    if (handle_ == HelperHandle::kRange) {
      return axial;
    }
    const auto radial = glm::length(offset - node_.direction * axial);
    return std::atan2(radial, axial);
  }

  void HelperDrag::Apply(const float value)
  {
    if (!std::isfinite(value)) {
      return;
    }
    switch (handle_) {
    case HelperHandle::kRange:
      value_ = std::max(value, kMinHelperRange);
      node_.range = value_;
      break;
    case HelperHandle::kOuterCone:
      value_ = std::clamp(value,
        std::max(node_.inner_cone, kMinOuterConeRadians), kMaxConeRadians);
      // Nearly equal angles would leave no representable falloff: they meet.
      if (value_ - node_.inner_cone < kConeMergeRadians) {
        value_ = std::max(node_.inner_cone, kMinOuterConeRadians);
      }
      node_.outer_cone = value_;
      break;
    case HelperHandle::kInnerCone:
      value_ = std::clamp(value, 0.0F, node_.outer_cone);
      if (node_.outer_cone - value_ < kConeMergeRadians) {
        value_ = node_.outer_cone;
      }
      node_.inner_cone = value_;
      break;
    case HelperHandle::kNone:
      break;
    }
  }

  // -- Selected helpers -----------------------------------------------------

  void BuildSelectedHelpers(const GizmoCamera& camera,
    const std::span<const SceneHelperNode> nodes, const HelperVisual& visual,
    ViewOverlay& overlay)
  {
    const auto scale = visual.display_scale;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const auto& stored = nodes[i];
      if (!stored.selected) {
        continue;
      }
      const auto dragged
        = visual.drag != nullptr && visual.drag->Node().id == stored.id;
      const auto& node = dragged ? visual.drag->Node() : stored;
      const auto color = Tint(node);
      const auto width = (node.active ? kActiveStrokePixels
                                      : kHelperStrokePixels)
        * scale;
      switch (node.kind) {
      case SceneHelperKind::kDirectionalLight:
        AddDirectionalHelper(overlay.scene, camera, node, color, width, scale);
        break;
      case SceneHelperKind::kPointLight:
        AddPointHelper(overlay.scene, camera, node, color, width);
        break;
      case SceneHelperKind::kSpotLight:
        AddSpotHelper(overlay.scene, camera, node, color, width);
        break;
      case SceneHelperKind::kCamera:
        AddCameraHelper(overlay.scene, node, color, width);
        break;
      case SceneHelperKind::kLocalFogVolume:
        // The volume's sphere; it is sized by the node's scale, not a handle.
        AddPointHelper(overlay.scene, camera, node, color, width);
        break;
      }
      if (!node.editable) {
        continue;
      }
      for (const auto handle : kHandles) {
        const auto position = HelperHandlePosition(camera, node, handle);
        if (!position.has_value()) {
          continue;
        }
        const auto hovered = dragged
          ? visual.drag->Handle() == handle
          : visual.has_hover && visual.hovered.node == i
            && visual.hovered.handle == handle;
        // A dragged handle stays visible through the scene.
        AddHandle(dragged && hovered ? overlay.top : overlay.scene, camera,
          *position, hovered, color, scale);
      }
    }
  }

  // -- Triad ----------------------------------------------------------------

  auto TriadCenter(const GizmoCamera& camera, const float display_scale)
    -> glm::vec2
  {
    const auto margin = kTriadMarginPixels * display_scale;
    return camera.viewport_origin
      + glm::vec2 { margin, camera.viewport_size.y - margin };
  }

  auto HitTestTriad(const GizmoCamera& camera, const glm::vec2& pointer,
    const float display_scale) -> TriadAxis
  {
    const auto center = TriadCenter(camera, display_scale);
    const auto length = kTriadAxisPixels * display_scale;
    const auto radius = kTriadHitPixels * display_scale;
    auto hit = TriadAxis::kNone;
    // Front letters come last, so they win an overlap.
    for (const auto& letter : TriadLetters(camera)) {
      if (glm::distance(pointer, center + letter.offset * length) <= radius) {
        hit = static_cast<TriadAxis>(letter.axis + 1);
      }
    }
    return hit;
  }

  void BuildTriad(const GizmoCamera& camera, const TriadAxis hovered,
    const float display_scale, ViewOverlay& overlay)
  {
    const auto center = TriadCenter(camera, display_scale);
    const auto length = kTriadAxisPixels * display_scale;
    const auto gap = kTriadLetterGapPixels * display_scale;
    for (const auto& letter : TriadLetters(camera)) {
      const auto is_hovered = static_cast<int>(hovered) == letter.axis + 1;
      // Axes pointing away from the eye are drawn fainter.
      const auto alpha = 0.55F + 0.45F * (letter.facing + 1.0F) * 0.5F;
      auto color = kAxisColors[letter.axis];
      if (is_hovered) {
        color = glm::vec4(glm::mix(glm::vec3(color), glm::vec3(1.0F), 0.35F), 1.0F);
      } else {
        color.a = alpha;
      }
      const auto reach = glm::length(letter.offset) * length;
      if (reach > gap + 1.0F) {
        const auto direction = letter.offset / glm::length(letter.offset);
        AddLine(overlay.top, camera.Unproject(center),
          camera.Unproject(center + direction * (reach - gap)),
          kTriadStrokePixels * display_scale, WithAlpha(color, color.a * 0.6F));
      }
      AddLetter(overlay.top, camera, letter.axis,
        center + letter.offset * length,
        kTriadLetterHalfPixels * display_scale
          * (is_hovered ? 1.25F : 1.0F),
        (is_hovered ? kTriadHoverStrokePixels : kTriadStrokePixels)
          * display_scale,
        color);
    }
  }

} // namespace oxygen::interop::module
