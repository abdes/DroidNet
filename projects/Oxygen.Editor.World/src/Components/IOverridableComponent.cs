// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// A component whose properties a source, such as a geometry asset, may supply; the component stores only the values
/// this scene changes.
/// </summary>
public interface IOverridableComponent
{
    /// <summary>
    /// Gets or sets the values this scene stores over the component's sources.
    /// </summary>
    /// <exception cref="ArgumentException">The value holds overrides of another component kind.</exception>
    [SuppressMessage("Naming", "CA1716:Identifiers should not match keywords", Justification = "Overrides is the scene format's name for the values stored over a source (the JSON member and the reference fields use it); only C# implements this editor interface.")]
    public SourceOverrides Overrides { get; set; }

    /// <summary>
    /// Gets the asset that owns the identities of a kind of part, such as the geometry that owns its surfaces.
    /// </summary>
    /// <param name="partKind">The part kind.</param>
    /// <returns>The asset, or <see langword="null"/> when the component has no part of that kind.</returns>
    public AssetPath? PartOwner(string partKind);
}
