// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data;

/// <summary>
/// Descriptor query methods for <see cref="EditorSettingsManager"/>.
/// </summary>
public partial class EditorSettingsManager
{
    /// <summary>
    /// Groups descriptors by category.
    /// </summary>
    /// <returns>A dictionary mapping category names to descriptor lists.</returns>
    private Dictionary<string, IReadOnlyList<ISettingDescriptor>> GetDescriptorsByCategory()
    {
        var descriptors = this.descriptorProvider.EnumerateDescriptors().ToList();
        var grouped = descriptors
            .GroupBy(d => d.Category ?? string.Empty, StringComparer.Ordinal)
            .ToDictionary(
                g => g.Key,
                g => (IReadOnlyList<ISettingDescriptor>)g.ToList(),
                StringComparer.Ordinal);
        return grouped;
    }

    /// <summary>
    /// Searches descriptors by term across multiple fields.
    /// </summary>
    /// <param name="searchTerm">The search term (case-insensitive).</param>
    /// <returns>Matching descriptors.</returns>
    private List<ISettingDescriptor> SearchDescriptors(string searchTerm)
    {
        var descriptors = this.descriptorProvider.EnumerateDescriptors();

        if (string.IsNullOrEmpty(searchTerm))
        {
            return descriptors.ToList();
        }

        var term = searchTerm.Trim();
        var comparer = StringComparison.OrdinalIgnoreCase;
        var matches = descriptors
            .Where(d =>
                d.SettingsModule.Contains(term, comparer) ||
                d.Name.Contains(term, comparer) ||
                (d.DisplayName?.Contains(term, comparer) ?? false) ||
                (d.Description?.Contains(term, comparer) ?? false) ||
                (d.Category?.Contains(term, comparer) ?? false))
            .ToList();
        return matches;
    }
}
