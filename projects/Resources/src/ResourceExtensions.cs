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
    private const string LocalizedResourcePath = "Localized";
    private static readonly Lazy<IResourceMap> DefaultStrings = new(() => new ResourceMapWrapper(new ResourceManager().MainResourceMap));

    /// <summary>
    /// Gets the localized string for the specified value, using the `Localized` sub-tree of the Appllication's main resource map.
    /// </summary>
    /// <param name="value">The string value to localize.</param>
    /// <param name="resourceMap">The resource map to use for localization. If <see langword="null"/>, the default resource map is used.</param>
    /// <returns>The localized string if found; otherwise, the original string.</returns>
    /// <remarks>
    /// This method tries to find the resource in the main application resource map, which can be
    /// overridden by providing the resource map to use as a parameter. It looks under the
    /// `Localized` sub-tree, which corresponds to a resource file named `Localized.resw`.
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
        var localized = resourceMap.TryGetValue($"{LocalizedResourcePath}/{value}");
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
            // First, give a chance to the application to override the localized string resource
            var localized = resourceMap.TryGetValue($"{LocalizedResourcePath}/{value}");
            if (localized is not null)
            {
                return localized.ValueAsString;
            }

            // If not found, try to get the localized string from the calling assembly's sub-tree in the main resource map.
            // This will only work if the application is a packaged app.
            var callingAssemblyName = Assembly.GetCallingAssembly().GetName().Name;
            localized = resourceMap.TryGetValue($"{callingAssemblyName}/Localized/{value}");
            if (localized is not null)
            {
                return localized.ValueAsString;
            }

            // Last, try to get the localized string directly from the calling assembly's resource map.
            var executingAssemblyLocation = Assembly.GetExecutingAssembly().Location;
            var executingAssemblyDirectory = Path.GetDirectoryName(executingAssemblyLocation) ?? string.Empty;
            var callingAssemblyResourcesFile = Path.Combine(executingAssemblyDirectory, $"{callingAssemblyName}.pri");
            resourceMap = new ResourceMapWrapper(new ResourceManager($"{callingAssemblyResourcesFile}").MainResourceMap);
            localized = resourceMap.TryGetValue($"{callingAssemblyName}/{LocalizedResourcePath}/{value}");
            if (localized is not null)
            {
                return localized.ValueAsString;
            }

            // For testing purposes, the resources are defined in the test project. They will not have the assembly name.
            // It does not hurt anyway to try to find the resource without the assembly name root.
            localized = resourceMap.TryGetValue($"{LocalizedResourcePath}/{value}");
            if (localized is not null)
            {
                return localized.ValueAsString;
            }

            Debug.WriteLine($"LOCALIZATION MISSING: `{value}` has no localized string in App main resource map, App sub-tree `{callingAssemblyName}` and assembly resource map in `{callingAssemblyResourcesFile}`");
            return value;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"LOCALIZATION ERROR: Failed to get localized version of `{value}`: {ex.Message}");
            return value;
        }
    }
}
