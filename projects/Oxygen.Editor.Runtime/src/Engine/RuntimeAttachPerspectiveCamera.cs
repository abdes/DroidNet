// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to AttachPerspectiveCamera.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="FieldOfViewYRadians">The FieldOfViewYRadians command value.</param>
/// <param name="AspectRatio">The AspectRatio command value.</param>
/// <param name="NearPlane">The NearPlane command value.</param>
/// <param name="FarPlane">The FarPlane command value.</param>
public sealed record RuntimeAttachPerspectiveCamera(Guid NodeId, float FieldOfViewYRadians, float AspectRatio, float NearPlane, float FarPlane) : RuntimeWorldCommand;
