// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>Authored perspective-camera framing policy.</summary>
public enum CameraAspectMode : byte
{
    /// <summary>Derive aspect from each target while retaining vertical FOV.</summary>
    Auto = 0,

    /// <summary>Preserve the authored aspect ratio and complete frame.</summary>
    Fixed = 1,
}
