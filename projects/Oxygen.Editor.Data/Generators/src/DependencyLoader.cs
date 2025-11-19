// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using System.Runtime.Loader;

namespace Oxygen.Editor.Data.Generators;

/// <summary>
/// Handles resolution of embedded generator dependencies at runtime without exposing them to host projects.
/// </summary>
internal static class DependencyLoader
{
    private static readonly object SyncRoot = new();
    private static bool initialized;

    /// <summary>
    /// Ensures the Handlebars runtime assembly is available to the generator AppDomain.
    /// </summary>
    internal static void EnsureHandlebarsLoaded()
    {
        if (initialized)
        {
            return;
        }

        lock (SyncRoot)
        {
            if (initialized)
            {
                return;
            }

            AppDomain.CurrentDomain.AssemblyResolve += ResolveHandlebars;
            initialized = true;
        }
    }

    private static Assembly? ResolveHandlebars(object? sender, ResolveEventArgs args)
    {
        var requested = new AssemblyName(args.Name);
        if (!string.Equals(requested.Name, "Handlebars", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        const string resourceName = "Oxygen.Editor.Data.Generators.Dependencies.Handlebars.dll";
        if (typeof(DependencyLoader).Assembly.GetManifestResourceStream(resourceName) is not Stream resourceStream)
        {
            return null;
        }

        using var memory = new MemoryStream();
        resourceStream.CopyTo(memory);
        memory.Position = 0;
        return AssemblyLoadContext.Default.LoadFromStream(memory);
    }
}
