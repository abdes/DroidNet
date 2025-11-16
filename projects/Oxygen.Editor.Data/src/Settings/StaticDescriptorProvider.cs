// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Provides setting descriptors from an explicit static registration.
/// This is the primary provider for source-generated descriptor registration.
/// </summary>
/// <remarks>
/// Thread-safe and handles duplicate registrations gracefully.
/// Descriptors are deduplicated by their module and name combination.
/// Descriptors are immutable and registered once per AppDomain lifetime via module initializers.
/// </remarks>
public sealed class StaticDescriptorProvider : IDescriptorProvider
{
    private readonly ConcurrentDictionary<string, ISettingDescriptor> descriptors = new(StringComparer.Ordinal);

    /// <summary>
    /// Registers a setting descriptor.
    /// </summary>
    /// <typeparam name="T">The type of the setting value.</typeparam>
    /// <param name="descriptor">The descriptor to register.</param>
    /// <remarks>
    /// If a descriptor with the same module and name is already registered, it will be replaced.
    /// This handles duplicate registrations gracefully.
    /// </remarks>
    public void Register<T>(SettingDescriptor<T> descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        var key = $"{descriptor.Key.SettingsModule}:{descriptor.Key.Name}";
        _ = this.descriptors.AddOrUpdate(
            key,
            descriptor,
            (_, _) => descriptor);
    }

    /// <inheritdoc/>
    public IEnumerable<ISettingDescriptor> EnumerateDescriptors()
        => this.descriptors.Values;
}
