// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// Defines the contract for loading project templates from various storage locations.
/// </summary>
/// <remarks>
/// <para>
/// ITemplatesSource provides abstract access to project template loading functionality,
/// allowing templates to be loaded from different storage backends like local filesystem,
/// network locations, or cloud storage.
/// </para>
/// <para>
/// Implementations must validate template locations and handle loading of all template
/// assets including descriptor files, icons, and preview images.
/// </para>
/// </remarks>
public interface ITemplatesSource
{
    /// <summary>
    /// Checks if this template source can load templates from the given location.
    /// </summary>
    /// <param name="fromUri">The URI pointing to the template location.</param>
    /// <returns>
    /// <see langword="true"/> if the location scheme is supported by this source; <see langword="false"/> otherwise.
    /// </returns>
    /// <remarks>
    /// Implementations should validate the URI scheme to determine if they can handle
    /// loading from that type of location (e.g., `file://`, `http://`, etc.).
    /// </remarks>
    public bool CanLoad(Uri fromUri);

    /// <summary>
    /// Loads a project template from the specified location.
    /// </summary>
    /// <param name="fromUri">The URI pointing to the template location.</param>
    /// <returns>The loaded template information if successful.</returns>
    /// <exception cref="ArgumentException">Thrown when the given location is not supported or is invalid.</exception>
    /// <exception cref="TemplateLoadingException">Thrown when an error occurs while loading the template or its assets.</exception>
    public Task<ITemplateInfo> LoadTemplateAsync(Uri fromUri);
}
