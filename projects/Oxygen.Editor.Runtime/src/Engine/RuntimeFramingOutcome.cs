// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>What a frame request did to a view's editor camera.</summary>
public enum RuntimeFramingOutcome
{
    /// <summary>The editor camera is moving to frame the bounds.</summary>
    Framed = 0,

    /// <summary>None of the requested nodes exist.</summary>
    NothingToFrame = 1,

    /// <summary>The view looks through a scene camera, which framing never moves.</summary>
    ViewingSceneCamera = 2,

    /// <summary>The view does not exist.</summary>
    NoView = 3,

    /// <summary>The bounds are not finite; the view is unchanged.</summary>
    InvalidBounds = 4,
}
