// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed frame capture provider options, converted at the native session boundary.</summary>
public enum FrameCaptureProvider
{
    /// <summary>None.</summary>
    None = 0,

    /// <summary>Render Doc.</summary>
    RenderDoc = 1,

    /// <summary>Pix.</summary>
    Pix = 2,
}
