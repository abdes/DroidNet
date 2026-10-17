// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>What the environment descriptors read and write: a copy of the scene's environment.</summary>
/// <param name="value">The environment to start from.</param>
/// <remarks>The environment is immutable data; the scene takes the written copy when the edit commits it.</remarks>
public sealed class SceneEnvironmentTarget(SceneEnvironmentData value)
{
    /// <summary>Gets or sets the environment.</summary>
    public SceneEnvironmentData Value { get; set; } = value;
}
