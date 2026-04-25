// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Engine timing settings.
/// </summary>
public sealed class TimingSettings
{
    /// <summary>
    ///     Gets or sets the fixed timestep delta.
    /// </summary>
    public TimeSpan? FixedDelta { get; set; }

    /// <summary>
    ///     Gets or sets the maximum fixed timestep accumulator.
    /// </summary>
    public TimeSpan? MaxAccumulator { get; set; }

    /// <summary>
    ///     Gets or sets the maximum fixed timestep substeps per frame.
    /// </summary>
    public uint? MaxSubsteps { get; set; }

    /// <summary>
    ///     Gets or sets the frame pacing safety margin.
    /// </summary>
    public TimeSpan? PacingSafetyMargin { get; set; }

    /// <summary>
    ///     Gets or sets the cooperative sleep used by uncapped frame pacing.
    /// </summary>
    public TimeSpan? UncappedCooperativeSleep { get; set; }
}
