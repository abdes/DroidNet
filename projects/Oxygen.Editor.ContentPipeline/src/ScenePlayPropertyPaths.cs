// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Stable paths of the scene's Play properties, shared by cook problems and Go To Property.
/// </summary>
public static class ScenePlayPropertyPaths
{
    /// <summary>The camera a played scene starts viewing through.</summary>
    public const string StartingCamera = "/play/starting_camera";

    /// <summary>The input mapping contexts a played scene starts with.</summary>
    public const string StartingInputContexts = "/play/starting_input_contexts";

    /// <summary>Checks whether a property path names a Play property.</summary>
    /// <param name="propertyPath">The property path.</param>
    /// <returns><see langword="true"/> for the starting camera and the starting input contexts.</returns>
    public static bool IsPlayProperty(string propertyPath)
        => string.Equals(propertyPath, StartingCamera, StringComparison.Ordinal)
            || string.Equals(propertyPath, StartingInputContexts, StringComparison.Ordinal);
}
