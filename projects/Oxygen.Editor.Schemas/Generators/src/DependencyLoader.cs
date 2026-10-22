// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;

namespace Oxygen.Editor.Schemas.Generators;

/// <summary>
/// Loads the generator's embedded YamlDotNet assembly when the compiler fails to resolve it, without exposing it to
/// the projects being compiled.
/// </summary>
internal static class DependencyLoader
{
    private const string YamlAssembly = "YamlDotNet";
    private const string YamlResource = "Oxygen.Editor.Schemas.Generators.Dependencies.YamlDotNet.dll";

    private static readonly object SyncRoot = new();
    private static Assembly? yaml;
    private static bool initialized;

    /// <summary>Resolves YamlDotNet from the embedded copy from now on.</summary>
    internal static void EnsureYamlLoaded()
    {
        lock (SyncRoot)
        {
            if (!initialized)
            {
                AppDomain.CurrentDomain.AssemblyResolve += ResolveYaml;
                initialized = true;
            }
        }
    }

    private static Assembly? ResolveYaml(object? sender, ResolveEventArgs args)
    {
        if (!string.Equals(new AssemblyName(args.Name).Name, YamlAssembly, StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        lock (SyncRoot)
        {
            if (yaml is null && typeof(DependencyLoader).Assembly.GetManifestResourceStream(YamlResource) is Stream stream)
            {
                using (stream)
                using (var memory = new MemoryStream())
                {
                    stream.CopyTo(memory);
                    yaml = Assembly.Load(memory.ToArray());
                }
            }

            return yaml;
        }
    }
}
