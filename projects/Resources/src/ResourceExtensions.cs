// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reflection;

namespace DroidNet.Resources;

/// <summary>
///     Provides extension methods for resource localization.
/// </summary>
/// <remarks>
///     The <see cref="ResourceExtensions"/> class offers convenient methods to retrieve localized
///     strings from resource maps. It supports fetching resources from the application's main
///     resource map and from sub-trees for specific assemblies.
/// </remarks>
public static class ResourceExtensions
{
    private const string LocalizedResourcePath = "Localized";
    private static IResourceMapProvider? provider;

    /// <summary>
    ///     Gets the current resource map provider.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown when the provider has not been initialized.</exception>
    private static IResourceMapProvider Provider =>
        provider ?? throw new InvalidOperationException(
            "ResourceExtensions has not been initialized. Call ResourceExtensions.Initialize() at application startup.");

    /// <summary>
    ///     Initializes the resource extensions with the specified provider.
    /// </summary>
    /// <param name="resourceMapProvider">The provider to use for resource map resolution.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="resourceMapProvider"/> is null.</exception>
    /// <remarks>
    ///     This method should be called once at application startup. It can be called multiple times
    ///     to replace the provider (e.g., for testing scenarios).
    /// </remarks>
    public static void Initialize(IResourceMapProvider resourceMapProvider)
    {
        provider = resourceMapProvider ?? throw new ArgumentNullException(nameof(resourceMapProvider));
#pragma warning disable CA1303 // Console writes are for troubleshooting
        Console.WriteLine($"ResourceExtensions: Initialized with provider {resourceMapProvider.GetType().FullName}");
#pragma warning restore CA1303
    }

    /// <summary>
    ///     Gets the localized string for the specified value, using the `Localized` sub-tree of the
    ///     application's resource map.
    /// </summary>
    /// <param name="value">The string value to localize.</param>
    /// <param name="resourceMap">
    ///     The resource map to use for localization. If <see langword="null"/>, the default
    ///     resource map's Localized subtree is used. If provided, direct key lookup is performed.
    /// </param>
    /// <returns>The localized string if found; otherwise, the original string.</returns>
    /// <remarks>
    ///     <strong>Resolution logic:</strong>
    ///     <list type="bullet">
    ///       <item>If <paramref name="resourceMap"/> is provided: direct lookup using <paramref name="value"/> as key.</item>
    ///       <item>If <paramref name="resourceMap"/> is <see langword="null"/>: lookup in provider's application resource map `Localized` subtree.</item>
    ///       <item>If not found: returns original <paramref name="value"/>.</item>
    ///     </list>
    /// </remarks>
    /// <example>
    ///     <para><strong>Example Usage:</strong></para>
    ///     <code><![CDATA[
    ///     string localizedString = "Hello".GetLocalized();
    ///     Console.WriteLine(localizedString); // Output: Localized string for "Hello" if found, otherwise "Hello"
    ///     ]]></code>
    /// </example>
    public static string GetLocalized(this string value, IResourceMap? resourceMap = null)
    {
        ArgumentNullException.ThrowIfNull(value);
#pragma warning disable CA1303 // Console writes are for troubleshooting
        Console.WriteLine($"ResourceExtensions.GetLocalized: value='{value}', resourceMapProvided={resourceMap is not null}");
#pragma warning restore CA1303

        if (resourceMap is not null)
        {
            // User provided explicit resource map - do direct key lookup
            if (resourceMap.TryGetValue(value, out var localized) && localized is not null)
            {
                return localized;
            }
        }
        else
        {
            // No explicit map - use provider's application resource map Localized subtree
            if (TryGetLocalizedValue(Provider.ApplicationResourceMap, value, out var localized))
            {
                return localized;
            }
        }

        return value;
    }

    /// <summary>
    ///     Gets the localized string for the specified value using the assembly that defines
    ///     <typeparamref name="T"/>.
    /// </summary>
    /// <typeparam name="T">The type whose assembly should be used for lookups.</typeparam>
    /// <param name="value">The string value to localize.</param>
    /// <param name="resourceMap">
    ///     Optional resource map to search before falling back to the assembly and application
    ///     maps.
    /// </param>
    /// <returns>The localized string if found; otherwise, the original string.</returns>
    public static string GetLocalized<T>(this string value, IResourceMap? resourceMap = null)
        => value.GetLocalized(typeof(T).Assembly, resourceMap);

    /// <summary>
    ///     Gets the localized string for the specified value using a particular assembly.
    /// </summary>
    /// <param name="value">The string value to localize.</param>
    /// <param name="assembly">The assembly from which to resolve the localized resource.</param>
    /// <param name="resourceMap">
    ///     The resource map to use for localization. If provided, direct key lookup is performed.
    ///     If <see langword="null"/>, falls back to assembly map and then application map.
    /// </param>
    /// <returns>The localized string if found; otherwise, the original string.</returns>
    /// <remarks>
    ///     <strong>Resolution logic:</strong>
    ///     <list type="number">
    ///       <item>If <paramref name="resourceMap"/> is provided: direct lookup using <paramref name="value"/> as key.</item>
    ///       <item>Assembly map's `Localized` subtree (obtained via provider).</item>
    ///       <item>Application resource map's `Localized` subtree (obtained via provider).</item>
    ///       <item>Original <paramref name="value"/>.</item>
    ///     </list>
    /// </remarks>
    /// <example>
    ///     <para><strong>Example Usage:</strong></para>
    ///     <code><![CDATA[
    ///     string localizedString = "Goodbye".GetLocalized(typeof(MyType).Assembly);
    ///     Console.WriteLine(localizedString); // Output: Localized string for "Goodbye" if found, otherwise "Goodbye"
    ///     ]]></code>
    /// </example>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Localization helpers must not surface exceptions; they fall back gracefully.")]
    public static string GetLocalized(this string value, Assembly assembly, IResourceMap? resourceMap = null)
    {
        ArgumentNullException.ThrowIfNull(value);
        ArgumentNullException.ThrowIfNull(assembly);
#pragma warning disable CA1303 // Console writes are for troubleshooting
        Console.WriteLine($"ResourceExtensions.GetLocalized<T>: value='{value}', assembly={assembly.FullName}");
#pragma warning restore CA1303

        var hadResourceMap = resourceMap is not null;
        if (hadResourceMap)
        {
            try
            {
                // User provided explicit resource map - do direct key lookup
                if (resourceMap!.TryGetValue(value, out var result) && result is not null)
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"LOCALIZATION ERROR: Provided resource map lookup failed for `{value}`: {ex.Message}");
            }
        }

        // Try assembly's Localized subtree regardless of whether a custom map was supplied.
        try
        {
            var assemblyMap = Provider.GetAssemblyResourceMap(assembly);
            if (TryGetLocalizedValue(assemblyMap, value, out var assemblyLocalized))
            {
                return assemblyLocalized;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"LOCALIZATION ERROR: Failed to obtain assembly resource map for `{assembly.FullName}`: {ex.Message}");
        }

        // Fallback to application's Localized subtree.
        if (TryGetLocalizedValue(Provider.ApplicationResourceMap, value, out var appLocalized))
        {
            return appLocalized;
        }

        if (hadResourceMap)
        {
            Debug.WriteLine($"LOCALIZATION MISSING: `{value}` not found in provided map or fallbacks for assembly `{assembly.GetName().Name}`");
        }
        else
        {
            Debug.WriteLine($"LOCALIZATION MISSING: `{value}` has no localized string in App main resources or assembly `{assembly.GetName().Name}`");
        }

        return value;
    }

    /// <summary>
    ///     Gets a resource string from a subtree with the assembly that defines <typeparamref name="T"/>.
    ///     Format: "SubtreeName/Key" or just "Key" (defaults to Localized subtree).
    /// </summary>
    /// <typeparam name="T">The type whose assembly should be used for lookups.</typeparam>
    /// <param name="path">
    ///     The resource path. If it contains '/', the first part is the subtree name and the second is the key.
    ///     If no '/' is present, looks up in the "Localized" subtree.
    /// </param>
    /// <returns>The resource string if found; otherwise, the original path.</returns>
    /// <example>
    ///     <code><![CDATA[
    ///     "Special/MSG_SecretMessage".R<NiceGreeter>()  // looks up MSG_SecretMessage in Greeter assembly's Special subtree
    ///     "MSG_Hello".R<NiceGreeter>()                  // looks up MSG_Hello in Localized subtree
    ///     ]]></code>
    /// </example>
    public static string R<T>(this string path)
    {
        ArgumentNullException.ThrowIfNull(path);

        try
        {
#pragma warning disable CA1303 // Console writes are for troubleshooting
            Console.WriteLine($"ResourceExtensions.R<{typeof(T).FullName}>: path='{path}'");
#pragma warning restore CA1303
            var assemblyMap = Provider.GetAssemblyResourceMap(typeof(T).Assembly);
            var result = GetResourceFromPath(assemblyMap, path);

            // If not found in assembly, try application map as fallback
            if (string.Equals(result, path, StringComparison.Ordinal))
            {
                return GetResourceFromPath(Provider.ApplicationResourceMap, path);
            }

            return result;
        }
#pragma warning disable CA1031 // Fallback should not fail the caller
        catch
        {
            // Fallback to application resource map if assembly lookup fails
            return GetResourceFromPath(Provider.ApplicationResourceMap, path);
        }
#pragma warning restore CA1031
    }

    /// <summary>
    ///     Gets a localized resource string from the "Localized" subtree with the assembly that defines <typeparamref name="T"/>.
    /// </summary>
    /// <typeparam name="T">The type whose assembly should be used for lookups.</typeparam>
    /// <param name="key">The resource key to look up in the Localized subtree.</param>
    /// <returns>The localized string if found; otherwise, the original key.</returns>
    /// <example>
    ///     <code><![CDATA[
    ///     "MSG_Hello".L<NiceGreeter>()  // looks up MSG_Hello in Localized subtree of Greeter assembly
    ///     ]]></code>
    /// </example>
    public static string L<T>(this string key)
    {
        ArgumentNullException.ThrowIfNull(key);

        try
        {
            var assemblyMap = Provider.GetAssemblyResourceMap(typeof(T).Assembly);
            var result = GetResourceFromSubtree(assemblyMap, LocalizedResourcePath, key);

            // If not found in assembly, try application map as fallback
            if (string.Equals(result, key, StringComparison.Ordinal))
            {
                return GetResourceFromSubtree(Provider.ApplicationResourceMap, LocalizedResourcePath, key);
            }

            return result;
        }
#pragma warning disable CA1031 // Localization should not raise
        catch (Exception ex)
        {
            Debug.WriteLine($"LOCALIZATION ERROR: Failed to obtain localized value for `{key}` from assembly `{typeof(T).Assembly.GetName().Name}`: {ex.Message}");

            // Fallback to application resource map if assembly lookup fails
            return GetResourceFromSubtree(Provider.ApplicationResourceMap, LocalizedResourcePath, key);
        }
#pragma warning restore CA1031
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Resource map access should never surface exceptions to callers; failures fall back to defaults.")]
    private static string GetResourceFromPath(IResourceMap map, string path)
    {
        try
        {
            if (map.TryGetValue(path, out var direct) && direct is not null)
            {
#pragma warning disable CA1303 // diagnostic logging for troubleshooting
                Console.WriteLine($"ResourceExtensions: direct key '{path}' found -> '{direct}'");
#pragma warning restore CA1303
                return direct;
            }

            var slashIndex = path.IndexOf('/', StringComparison.Ordinal);

            // If no slash, default to Localized subtree
            if (slashIndex < 0)
            {
                return GetResourceFromSubtree(map, LocalizedResourcePath, path);
            }

            // Split into subtree and key (only one slash allowed)
            var subtree = path[..slashIndex];
            var key = path[(slashIndex + 1)..];

            if (string.IsNullOrEmpty(subtree) || string.IsNullOrEmpty(key))
            {
                return path;
            }

            var subtreeValue = GetResourceFromSubtree(map, subtree, key);
            return string.Equals(subtreeValue, key, StringComparison.Ordinal) ? path : subtreeValue;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"RESOURCE ERROR: Failed to get resource from path `{path}`: {ex.Message}");
        }

        return path;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Resource map access should never surface exceptions to callers; failures fall back to defaults.")]
    private static string GetResourceFromSubtree(IResourceMap map, string subtreeName, string key)
    {
        try
        {
            var subtree = map.GetSubtree(subtreeName);
            if (subtree is not EmptyResourceMap && subtree.TryGetValue(key, out var value) && value is not null)
            {
                return value;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"RESOURCE ERROR: Failed to get resource from subtree `{subtreeName}` with key `{key}`: {ex.Message}");
        }

        return key;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Resource map access should never surface exceptions to callers; failures fall back to defaults.")]
    private static bool TryGetLocalizedValue(IResourceMap map, string key, out string localized)
    {
        localized = key;

        try
        {
            var localizedSubtree = map.GetSubtree(LocalizedResourcePath);
            if (localizedSubtree is not EmptyResourceMap && localizedSubtree.TryGetValue(key, out var candidate) && candidate is not null)
            {
                localized = candidate;
                return true;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"LOCALIZATION ERROR: Failed to read localized value for `{key}`: {ex.Message}");
        }

        return false;
    }
}
