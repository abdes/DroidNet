// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>The editor and installed engine locations for each build configuration.</summary>
/// <param name="EditorRoot">The editor application directory.</param>
/// <param name="EngineRoot">The installed engine SDK directory.</param>
/// <param name="Configuration">The Debug or Release build configuration.</param>
public sealed record EditorNativeInstallation(string EditorRoot, string EngineRoot, string Configuration)
{
    /// <summary>Gets the Interop assembly containing its native SDK build receipt.</summary>
    public string InteropPath => Path.Combine(this.EditorRoot, "DroidNet.Oxygen.Editor.Interop.dll");

    /// <summary>Resolves the packaged engine or the current checkout's installed SDK without loading native code.</summary>
    /// <param name="editorRoot">The application directory.</param>
    /// <param name="configuration">The running build configuration.</param>
    /// <returns>The expected installation paths, including when files are missing.</returns>
    public static EditorNativeInstallation Discover(string editorRoot, string configuration)
    {
        editorRoot = Path.GetFullPath(editorRoot);
        var bundled = Path.Combine(editorRoot, "Engine");
        if (Directory.Exists(bundled))
        {
            return new(editorRoot, bundled, configuration);
        }

        for (var current = new DirectoryInfo(editorRoot); current is not null; current = current.Parent)
        {
            var engine = Path.Combine(current.FullName, "projects", "Oxygen.Engine");
            if (Directory.Exists(engine))
            {
                return new(editorRoot, Path.Combine(engine, "out", "install", configuration), configuration);
            }
        }

        return new(editorRoot, bundled, configuration);
    }
}
