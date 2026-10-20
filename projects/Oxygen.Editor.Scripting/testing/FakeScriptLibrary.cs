// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting.Testing;

/// <summary>
/// An <see cref="IScriptLibrary"/> for tests: the library scripts a test adds, and the reads it makes fail.
/// </summary>
public sealed class FakeScriptLibrary : IScriptLibrary
{
    private readonly Dictionary<AssetPath, Script> scripts = [];
    private readonly Dictionary<AssetPath, Exception> failures = [];

    /// <summary>Adds a library script.</summary>
    /// <param name="script">The script.</param>
    public void Add(Script script)
    {
        ArgumentNullException.ThrowIfNull(script);
        this.scripts[script.Identity] = script;
    }

    /// <summary>Makes reading a script fail.</summary>
    /// <param name="script">The script's identity.</param>
    /// <param name="failure">The exception the read throws.</param>
    public void Fail(AssetPath script, Exception failure)
    {
        ArgumentNullException.ThrowIfNull(script);
        ArgumentNullException.ThrowIfNull(failure);
        this.failures[script] = failure;
    }

    /// <inheritdoc />
    public Task<Script?> ReadAsync(ProjectContext project, AssetPath script, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(script);
        return this.failures.TryGetValue(script, out var failure)
            ? Task.FromException<Script?>(failure)
            : Task.FromResult(this.scripts.GetValueOrDefault(script));
    }
}
