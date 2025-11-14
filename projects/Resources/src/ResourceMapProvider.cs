// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.Windows.ApplicationModel.Resources;

namespace DroidNet.Resources;

/// <summary>
///     Provides factories for obtaining application or assembly-specific resource maps.
/// </summary>
public static class ResourceMapProvider
{
    private static readonly ConcurrentDictionary<string, IResourceMap> AssemblyMapCache = new(StringComparer.Ordinal);
    private static readonly ConcurrentDictionary<string, byte> AssemblyWarningCache = new(StringComparer.Ordinal);

    // Using Lazy ensures we try to create the Windows App SDK resource manager only once per
    // process and allows running inside environments (like VS Test Explorer) that do not ship the
    // PRI payload. When creation fails, we cache an empty map and avoid repeatedly throwing
    // FileNotFoundException for each lookup.
    private static readonly Lazy<IResourceMap> ApplicationResourceMapLazy = new(CreateApplicationResourceMap, LazyThreadSafetyMode.ExecutionAndPublication);

    /// <summary>
    ///     Gets the application main resource map.
    /// </summary>
    public static IResourceMap ApplicationResourceMap => ApplicationResourceMapLazy.Value;

    /// <summary>
    ///     Returns the resource map subtree for the requested assembly, or <see cref="EmptyResourceMap"/>
    ///     if not present.
    /// </summary>
    /// <param name="assembly">Assembly to lookup resource map for.</param>
    /// <returns>The resource map for the assembly or <see cref="EmptyResourceMap"/>.</returns>
    public static IResourceMap GetAssemblyResourceMap(Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(assembly);
        var name = assembly.GetName().Name!;
#pragma warning disable CA1303 // Console writes are for troubleshooting
        Console.WriteLine($"ResourceMapProvider: Resolving assembly resource map for '{assembly.FullName}'");
#pragma warning restore CA1303
        return AssemblyMapCache.GetOrAdd(name, static (_, asm) => CreateAssemblyResourceMap(asm), assembly);
    }

    private static IResourceMap CreateAssemblyResourceMap(Assembly assembly)
    {
        var assemblyName = assembly.GetName().Name;
        return string.IsNullOrEmpty(assemblyName)
            ? new EmptyResourceMap()
            : TryApplicationResourceSubTree(assemblyName)
                ?? TryFromAssemblyPriFile(assemblyName)
                    ?? new EmptyResourceMap();
    }

    private static IResourceMap CreateApplicationResourceMap()
    {
        try
        {
            var map = new ResourceManager().MainResourceMap;
            if (map is null)
            {
#pragma warning disable CA1303 // Console writes are for troubleshooting
                Console.WriteLine("ResourceMapProvider: Application resource map is null");
#pragma warning restore CA1303
                return new EmptyResourceMap();
            }

#pragma warning disable CA1303 // Console writes are for troubleshooting
            Console.WriteLine("ResourceMapProvider: Successfully created application resource map");
#pragma warning restore CA1303
            return new ResourceMapWrapper(map);
        }
        catch (Exception ex) when (IsRecoverableResourceManagerError(ex))
        {
            // Returning an EmptyResourceMap keeps the runtime behavior predictable: localization
            // lookups simply fall back to the original string when the Windows resource
            // infrastructure is not available (e.g., design time).
            Debug.WriteLine($"RESOURCE MAP WARNING: Failed to create application resource map: {ex.Message}");
            return new EmptyResourceMap();
        }
    }

    private static IResourceMap? TryApplicationResourceSubTree(string name)
    {
        var appMap = ApplicationResourceMap;
        if (appMap is EmptyResourceMap)
        {
            return null;
        }

        var subtree = appMap.GetSubtree(name);
        return subtree is EmptyResourceMap ? null : subtree;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1859:Use concrete types when possible for improved performance", Justification = "need consistent return type for simpler conditional expressions")]
    private static IResourceMap? TryFromAssemblyPriFile(string name)
    {
        try
        {
            var namedMap = new ResourceManager($"{name}.pri").MainResourceMap;
            return namedMap is null ? null : new ResourceMapWrapper(namedMap);
        }
        catch (Exception ex) when (IsRecoverableResourceManagerError(ex))
        {
            LogAssemblyWarning(name, $"resource map '{name}'.", ex);
#pragma warning disable CA1303 // Console writes are for troubleshooting
            Console.WriteLine($"ResourceMapProvider: Failed to load PRI map '{name}' ({ex.GetType().Name}): {ex.Message}");
#pragma warning restore CA1303
        }

        return null;
    }

    private static void LogAssemblyWarning(string name, string context, Exception ex)
    {
        var cacheKey = $"{name}|{context}";
        if (!AssemblyWarningCache.TryAdd(cacheKey, 0))
        {
            return;
        }

        Debug.WriteLine($"RESOURCE MAP WARNING: Failed to obtain {context} for '{name}': {ex.Message}");
    }

    private static bool IsRecoverableResourceManagerError(Exception ex)
        => ex is FileNotFoundException
            or FileLoadException
            or DllNotFoundException
            or DirectoryNotFoundException
            or InvalidOperationException
            or ArgumentException
            or COMException
            or PlatformNotSupportedException;
}
