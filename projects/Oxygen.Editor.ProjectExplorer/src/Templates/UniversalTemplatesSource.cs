// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>A composite template source that invokes the specific template source based on the template uri.</summary>
/// <param name="sources">
/// The collection of implementations of <see cref="ITemplatesSource" /> registered with the DI container using keyed
/// registrations. This collection should naturally exclude this implementation class.
/// </param>
public class UniversalTemplatesSource(ITemplatesSource[] sources) : ITemplatesSource
{
    /// <inheritdoc/>
    public bool CanLoad(Uri fromUri) => sources.Any(source => source.CanLoad(fromUri));

    /// <summary>
    /// Asynchronously load the project template from the provided <paramref name="fromUri">location</paramref>. Cycles through
    /// the known <see cref="ITemplatesSource">template sources</see> until one of them can load templates from the provided
    /// location.
    /// <para>
    /// Fails if none of the defined sources can load templates from the given location, or if a compatible source exists, but it
    /// failed to load the template.
    /// </para>
    /// </summary>
    /// <param name="fromUri">The location from which to load the templates.</param>
    /// <returns>The loaded template information.</returns>
    /// <exception cref="ArgumentException">When none of the defined sources can load templates from the given location.</exception>
    public Task<ITemplateInfo> LoadTemplateAsync(Uri fromUri)
    {
        foreach (var source in sources)
        {
            if (source.CanLoad(fromUri))
            {
                return source.LoadTemplateAsync(fromUri);
            }
        }

        throw new ArgumentException($"No template source is able to load a template from: {fromUri}", nameof(fromUri));
    }
}
