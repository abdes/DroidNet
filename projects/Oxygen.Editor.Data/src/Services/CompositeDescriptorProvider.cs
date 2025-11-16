// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Composite provider that delegates to multiple descriptor providers in sequence.
/// </summary>
/// <param name="providers">The providers to delegate to, in priority order.</param>
public sealed class CompositeDescriptorProvider(params IDescriptorProvider[] providers) : IDescriptorProvider
{
    private readonly IReadOnlyList<IDescriptorProvider> providers = providers ?? throw new ArgumentNullException(nameof(providers));

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
