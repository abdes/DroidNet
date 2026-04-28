// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Resolves default creation folders for authored project assets.
/// </summary>
public interface IAuthoringTargetResolver
{
    /// <summary>
    ///     Resolves the folder where a new authored asset should be created.
    /// </summary>
    /// <param name="project">The active project context.</param>
    /// <param name="assetKind">The kind of asset being created.</param>
    /// <param name="selection">The optional Content Browser selection.</param>
    /// <returns>The resolved authoring target.</returns>
    public AuthoringTarget ResolveCreateTarget(
        ProjectContext project,
        AuthoringAssetKind assetKind,
        ContentBrowserSelection? selection);
}
