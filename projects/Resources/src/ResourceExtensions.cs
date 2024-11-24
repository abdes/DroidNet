// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reflection;
using Microsoft.Windows.ApplicationModel.Resources;

namespace DroidNet.Resources;

/// <summary>
/// Provides extension methods for resource localization.
/// </summary>
/// <remarks>
/// The <see cref="ResourceExtensions"/> class offers convenient methods to retrieve localized
/// strings from resource maps. It supports fetching resources from the application's main resource
/// map and from sub-trees for specific assemblies.
/// </remarks>
public static class ResourceExtensions
{
    private static readonly Lazy<IResourceMap> DefaultStrings = new(() => new ResourceMapWrapper(new ResourceManager().MainResourceMap));

    /// <summary>
    /// Gets the localized string for the specified value.
    /// </summary>
    /// <param name="value">The string value to localize.</param>
    /// <param name="resourceMap">The resource map to use for localization. If <see langword="null"/>, the default resource map is used.</param>
    /// <returns>The localized string if found; otherwise, the original string.</returns>
    /// <remarks>
    /// This method tries to find the resource in the main application resource map, which can be
    /// overridden by providing the resource map to use as a parameter.
    /// </remarks>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// string localizedString = "Hello".GetLocalized();
    /// Console.WriteLine(localizedString); // Output: Localized string for "Hello" if found, otherwise "Hello"
    /// ]]></code>
    /// </example>
    public static string GetLocalized(this string value, IResourceMap? resourceMap = null)
    {
        resourceMap ??= DefaultStrings.Value;
        var localized = resourceMap.TryGetValue(value);
        return localized != null ? localized.ValueAsString : value;
    }

    /// <summary>
    /// Tries to get the localized string for the specified value.
    /// </summary>
    /// <param name="value">The string value to localize.</param>
    /// <param name="resourceMap">The resource map to use for localization. If <see langword="null"/>, the default resource map is used.</param>
    /// <returns>The localized string if found; otherwise, the original string.</returns>
    /// <remarks>
    /// This method attempts to find the resource in a child resource map of the main application
    /// resource map or the one given as a parameter, by using the calling assembly name. This is
    /// particularly useful to get localized strings from a specific assembly.
    /// <para>This method catches all exceptions and returns the original string if localization fails.</para>
    /// </remarks>
    /// <example>
    /// <para><strong>Example Usage:</strong></para>
    /// <code><![CDATA[
    /// string localizedString = "Goodbye".GetLocalizedMine();
    /// Console.WriteLine(localizedString); // Output: Localized string for "Goodbye" if found, otherwise "Goodbye"
    /// ]]></code>
    /// </example>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "does not throw, instead returned the same value")]
    public static string GetLocalizedMine(this string value, IResourceMap? resourceMap = null)
    {
        resourceMap ??= DefaultStrings.Value;
        try
        {
            var assemblyName = Assembly.GetCallingAssembly().GetName().Name;
            var subMap = resourceMap.GetSubtree($"{assemblyName}/Strings");
            if (subMap is null)
            {
                Debug.WriteLine($"Resource map for assembly `{assemblyName}` does not exist.");
                return value;
            }

            var localized = subMap.TryGetValue(value);
            return localized != null ? localized.ValueAsString : value;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to get localized version of `{value}`: {ex.Message}");
            return value;
        }
    }
}
