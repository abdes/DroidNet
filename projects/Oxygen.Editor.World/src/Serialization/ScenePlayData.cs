// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// How a scene starts when it is played: the camera it is first viewed through and the input mapping contexts that
/// start active.
/// </summary>
public sealed record ScenePlayData
{
    /// <summary>
    /// Gets the camera node the scene starts viewing through, or <see langword="null"/> for the first camera in
    /// hierarchy order.
    /// </summary>
    public NodeId? StartingCamera { get; init; }

    /// <summary>Gets the input mapping contexts that start active, in order.</summary>
    public IList<StartingInputContextData> StartingInputContexts { get; init; } = [];

    /// <summary>Gets a value indicating whether the scene starts with the defaults.</summary>
    [JsonIgnore]
    public bool IsEmpty => this.StartingCamera is null && this.StartingInputContexts.Count == 0;

    /// <summary>Compares two play settings by value, including the order of their contexts.</summary>
    /// <param name="left">The first settings.</param>
    /// <param name="right">The second settings.</param>
    /// <returns>Whether both start the scene the same way.</returns>
    public static bool AreEquivalent(ScenePlayData left, ScenePlayData right)
    {
        ArgumentNullException.ThrowIfNull(left);
        ArgumentNullException.ThrowIfNull(right);
        return left.StartingCamera == right.StartingCamera
            && left.StartingInputContexts.SequenceEqual(right.StartingInputContexts);
    }
}
