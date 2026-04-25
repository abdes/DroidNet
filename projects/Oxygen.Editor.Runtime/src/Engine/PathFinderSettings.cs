// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Engine path resolution settings.
/// </summary>
public sealed class PathFinderSettings
{
    /// <summary>
    ///     Gets or sets the workspace root path.
    /// </summary>
    public string? WorkspaceRootPath { get; set; }

    /// <summary>
    ///     Gets or sets the shader library path.
    /// </summary>
    public string? ShaderLibraryPath { get; set; }

    /// <summary>
    ///     Gets or sets the CVars archive path.
    /// </summary>
    public string? CVarsArchivePath { get; set; }

    /// <summary>
    ///     Gets the script source roots.
    /// </summary>
    public ICollection<string> ScriptSourceRoots { get; } = new Collection<string>();

    /// <summary>
    ///     Gets or sets the script bytecode cache path.
    /// </summary>
    public string? ScriptBytecodeCachePath { get; set; }
}
