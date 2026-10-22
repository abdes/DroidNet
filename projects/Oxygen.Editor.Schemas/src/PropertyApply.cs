// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT
using Oxygen.Managed.Core;


namespace Oxygen.Editor.Schemas;

/// <summary>
/// Writes property edits to model objects through their descriptors.
/// </summary>
public static class PropertyApply
{
    /// <summary>
    /// Writes an edit's values to a model target through the descriptors.
    /// </summary>
    /// <param name="target">The model target.</param>
    /// <param name="edit">The edit.</param>
    /// <param name="descriptors">The descriptors used to write values.</param>
    /// <exception cref="KeyNotFoundException">No descriptor is registered for one of the edit's properties.</exception>
    public static void ApplyToTarget(
        object target,
        PropertyEdit edit,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(descriptors);

        foreach (var (id, value) in edit)
        {
            if (!descriptors.TryGetValue(id, out var descriptor))
            {
                throw new KeyNotFoundException(
                    $"No property descriptor is registered for '{id.Qualified()}'.");
            }

            descriptor.WriteBoxed(target, value);
        }
    }
}
