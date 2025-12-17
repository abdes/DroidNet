// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// LOD policy that selects LOD based on screen space error.
/// </summary>
/// <remarks>
/// This policy evaluates how many pixels the object occupies on screen to select the appropriate LOD.
/// Screen space error provides a more perceptually accurate LOD selection than simple distance,
/// accounting for both object size and camera field of view.
/// </remarks>
public class ScreenSpaceErrorLodPolicy : LodPolicy
{
    /// <summary>
    /// Gets or sets the screen space error thresholds for each LOD level.
    /// </summary>
    /// <value>
    /// A list of screen space error values in pixels. LOD N is used when the screen space error exceeds Thresholds[N].
    /// Higher values mean more tolerance for error (lower detail LODs).
    /// </value>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2227:Collection properties should be read only", Justification = "JSON serialized")]
    public IList<float> Thresholds { get; set; } = [];
}
