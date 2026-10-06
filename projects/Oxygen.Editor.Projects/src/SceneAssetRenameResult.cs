// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>The committed scene name/path change and the sources whose references were repaired.</summary>
/// <param name="PreviousName">The previous scene name.</param>
/// <param name="Name">The committed scene name.</param>
/// <param name="PreviousAssetPath">The previous native virtual scene path.</param>
/// <param name="AssetPath">The committed native virtual scene path.</param>
/// <param name="Sources">The saved sources changed by the operation.</param>
public sealed record SceneAssetRenameResult(
    string PreviousName,
    string Name,
    string PreviousAssetPath,
    string AssetPath,
    IReadOnlyList<SceneAssetSourceChange> Sources)
{
    /// <summary>Gets a value indicating whether the operation changed the asset.</summary>
    public bool Changed => !string.Equals(this.PreviousName, this.Name, StringComparison.Ordinal);
}
