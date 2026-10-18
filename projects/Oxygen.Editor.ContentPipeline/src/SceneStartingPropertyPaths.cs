// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Stable paths of how the scene starts, shared by cook problems and Go To Property.
/// </summary>
public static class SceneStartingPropertyPaths
{
    /// <summary>The camera node the scene starts viewing through.</summary>
    public const string StartingCamera = "/starting_camera";

    /// <summary>The input mapping contexts that are active when the scene starts.</summary>
    public const string StartingInputContexts = "/starting_input_contexts";

    /// <summary>Checks whether a property path names how the scene starts.</summary>
    /// <param name="propertyPath">The property path.</param>
    /// <returns><see langword="true"/> for the starting camera and the starting input contexts.</returns>
    public static bool IsStartingProperty(string propertyPath)
        => string.Equals(propertyPath, StartingCamera, StringComparison.Ordinal)
            || string.Equals(propertyPath, StartingInputContexts, StringComparison.Ordinal);
}
