// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Composite provider that delegates to multiple descriptor providers in sequence.
/// </summary>
public sealed class CompositeDescriptorProvider : IDescriptorProvider
{
    private readonly IReadOnlyList<IDescriptorProvider> providers;

    /// <summary>
    /// Initializes a new instance of the <see cref="CompositeDescriptorProvider"/> class.
    /// </summary>
    /// <param name="providers">The providers to delegate to, in priority order.</param>
    public CompositeDescriptorProvider(params IDescriptorProvider[] providers)
    {
        this.providers = providers ?? throw new ArgumentNullException(nameof(providers));
    }

    /// <inheritdoc/>
    public IEnumerable<ISettingDescriptor> EnumerateDescriptors()
    {
        foreach (var provider in this.providers)
        {
            foreach (var descriptor in provider.EnumerateDescriptors())
            {
                yield return descriptor;
            }
        }
    }
}
