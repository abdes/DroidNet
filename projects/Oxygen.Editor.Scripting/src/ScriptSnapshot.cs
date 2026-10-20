// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// The analyzed scripts of a project at one moment, which each change replaces.
/// </summary>
internal sealed class ScriptSnapshot
{
    private readonly ImmutableDictionary<AssetPath, Script> scripts;

    private ScriptSnapshot(ImmutableDictionary<AssetPath, Script> scripts)
    {
        this.scripts = scripts;
        this.All = [.. scripts.Values];
        this.Problems = [.. scripts.Values.SelectMany(static script => script.Problems)];
    }

    /// <summary>Gets the snapshot of no script, as while no project is open.</summary>
    public static ScriptSnapshot Empty { get; } = new([]);

    /// <summary>Gets every script.</summary>
    public IReadOnlyCollection<Script> All { get; }

    /// <summary>Gets the problems of every script.</summary>
    public IReadOnlyList<AssetProblem> Problems { get; }

    /// <summary>Creates the snapshot of a project's entries.</summary>
    /// <param name="entries">The entries, by script identity.</param>
    /// <returns>The snapshot.</returns>
    public static ScriptSnapshot Create(ImmutableDictionary<AssetPath, ScriptEntry> entries)
        => new(entries.ToImmutableDictionary(static entry => entry.Key, static entry => entry.Value.Script));

    /// <summary>Finds a script.</summary>
    /// <param name="script">The script's identity.</param>
    /// <returns>The script, or <see langword="null"/>.</returns>
    public Script? Find(AssetPath script) => this.scripts.GetValueOrDefault(script);
}
