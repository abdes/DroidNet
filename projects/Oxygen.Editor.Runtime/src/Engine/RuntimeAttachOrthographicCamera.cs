// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable runtime request to AttachOrthographicCamera.</summary>
/// <param name="NodeId">The NodeId command value.</param>
/// <param name="OrthographicSize">The OrthographicSize command value.</param>
/// <param name="AspectRatio">The AspectRatio command value.</param>
/// <param name="NearPlane">The NearPlane command value.</param>
/// <param name="FarPlane">The FarPlane command value.</param>
/// <param name="AspectMode">The authored framing policy.</param>
public sealed record RuntimeAttachOrthographicCamera(Guid NodeId, float OrthographicSize, float AspectRatio, float NearPlane, float FarPlane, Oxygen.Managed.Core.CameraAspectMode AspectMode) : RuntimeWorldCommand;
