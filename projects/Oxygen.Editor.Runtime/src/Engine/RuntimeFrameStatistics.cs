// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The rate and duration of the last completed engine frame.</summary>
/// <param name="FramesPerSecond">Frames per second, as measured by the engine clock.</param>
/// <param name="FrameTimeMilliseconds">Duration of the last completed frame, in milliseconds.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimeFrameStatistics(float FramesPerSecond, float FrameTimeMilliseconds);
