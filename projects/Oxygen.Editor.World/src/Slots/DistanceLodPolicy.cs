// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// LOD policy that selects LOD based on distance from the camera.
/// </summary>
/// <remarks>
/// This policy evaluates the distance between the object and the camera to select the appropriate LOD.
/// Each distance threshold in the <see cref="Distances"/> list defines the maximum distance for that LOD level.
/// </remarks>
public class DistanceLodPolicy : LodPolicy
{
    /// <summary>
    /// Gets or sets the distance thresholds for each LOD level.
    /// </summary>
    /// <value>
    /// A list of distances in world units. LOD N is used when the object distance is less than Distances[N].
    /// The list should be sorted in ascending order.
    /// </value>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2227:Collection properties should be read only", Justification = "JSON serialized")]
    public IList<float> Distances { get; set; } = [];
}
