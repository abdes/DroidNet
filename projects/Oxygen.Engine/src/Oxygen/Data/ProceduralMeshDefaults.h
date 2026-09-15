//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::data::procedural {

//! Shared recipe defaults for direct factories and descriptor-driven
//! generation.
inline constexpr unsigned int kSubdividedCubeSegments = 6U;
inline constexpr unsigned int kSphereLatitudeSegments = 16U;
inline constexpr unsigned int kSphereLongitudeSegments = 32U;
inline constexpr unsigned int kCapsuleHemisphereSegments = 8U;
inline constexpr unsigned int kCapsuleRadialSegments = 32U;
inline constexpr unsigned int kCapsuleMaxHemisphereSegments = 64U;
inline constexpr unsigned int kCapsuleMaxRadialSegments = 256U;
inline constexpr float kCapsuleHeight = 2.0F;
inline constexpr float kCapsuleRadius = 0.5F;
inline constexpr unsigned int kIcoSphereSubdivisionLevel = 2U;
inline constexpr unsigned int kPlaneXSegments = 1U;
inline constexpr unsigned int kPlaneZSegments = 1U;
inline constexpr float kPlaneSize = 1.0F;
inline constexpr unsigned int kCylinderSegments = 32U;
inline constexpr float kCylinderHeight = 1.0F;
inline constexpr float kCylinderRadius = 0.5F;
inline constexpr unsigned int kConeSegments = 32U;
inline constexpr float kConeHeight = 1.0F;
inline constexpr float kConeRadius = 0.5F;
inline constexpr unsigned int kTorusMajorSegments = 32U;
inline constexpr unsigned int kTorusMinorSegments = 16U;
inline constexpr float kTorusMajorRadius = 0.4F;
inline constexpr float kTorusMinorRadius = 0.1F;
inline constexpr float kQuadWidth = 1.0F;
inline constexpr float kQuadHeight = 1.0F;

} // namespace oxygen::data::procedural
