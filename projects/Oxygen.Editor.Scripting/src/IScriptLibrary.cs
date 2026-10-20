// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Reads the scripts of the cooked libraries a project mounts, which have no source to analyze. The content pipeline
/// implements it from its cooked report.
/// </summary>
public interface IScriptLibrary
{
    /// <summary>Reads a library script's declarations, without cooking or changing authoring data.</summary>
    /// <param name="project">The project that mounts the library.</param>
    /// <param name="script">The script's identity.</param>
    /// <param name="cancellationToken">A token that cancels the read.</param>
    /// <returns>The script, with <see cref="ScriptOrigin.Library"/>; <see langword="null"/> when no mounted library
    /// has it.</returns>
    /// <exception cref="IOException">The library cannot be read.</exception>
    /// <exception cref="InvalidDataException">The library does not describe the script as its format requires.</exception>
    /// <exception cref="InvalidOperationException">The library cannot be read in the project's current state.</exception>
    public Task<Script?> ReadAsync(ProjectContext project, AssetPath script, CancellationToken cancellationToken);
}
