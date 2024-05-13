// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

public interface ITemplatesSource
{
    /// <summary>Check if this template source can load templates from the given location.</summary>
    /// <param name="fromUri">The template location.</param>
    /// <returns><see langword="true" /> if the location is supported by this template source; <see langword="false" /> if not.</returns>
    bool CanLoad(Uri fromUri);

    /// <summary>Load a project template from the given <paramref name="fromUri">location</paramref>.</summary>
    /// <param name="fromUri">The location from which the template should be loaded.</param>
    /// <returns>The template information if successful.</returns>
    /// <exception cref="ArgumentException">
    /// If the given <paramref name="fromUri">location</paramref> is not supported or is not valid.
    /// </exception>
    /// <exception cref="TemplateLoadingException">
    /// If an error occurs while loading the template from the given <paramref name="fromUri">location</paramref>. The exception
    /// message, and any attached inner exception to it, can provide additional details on the nature of the error.
    /// </exception>
    Task<ITemplateInfo> LoadTemplateAsync(Uri fromUri);
}
