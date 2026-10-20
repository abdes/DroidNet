// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>The input sources of a project at one moment, and every problem they have.</summary>
internal sealed class InputSnapshot
{
    private InputSnapshot(ProjectContext? project, ImmutableDictionary<AssetPath, InputSourceEntry> entries)
    {
        this.Entries = entries;
        var ordered = entries.OrderBy(static entry => entry.Key.VirtualPath, StringComparer.Ordinal).ToList();
        this.All = [.. ordered.Select(static entry => entry.Value.Container).OfType<InputContainer>()];
        this.Problems = project is null ? [] : [.. ordered.SelectMany(entry => this.ProblemsOf(project, entry.Key, entry.Value))];
    }

    /// <summary>Gets the snapshot of no project.</summary>
    public static InputSnapshot Empty { get; } = new(project: null, []);

    /// <summary>Gets every input source file, by the source's identity.</summary>
    public ImmutableDictionary<AssetPath, InputSourceEntry> Entries { get; }

    /// <summary>Gets every source that matches the engine's input schema, ordered by identity.</summary>
    public IReadOnlyCollection<InputContainer> All { get; }

    /// <summary>Gets every problem, by source and then in source order.</summary>
    public IReadOnlyList<AssetProblem> Problems { get; }

    /// <summary>Makes the snapshot of a project's sources, checking each against the others.</summary>
    /// <param name="project">The project.</param>
    /// <param name="entries">Every input source file, by the source's identity.</param>
    /// <returns>The snapshot.</returns>
    public static InputSnapshot Create(ProjectContext project, ImmutableDictionary<AssetPath, InputSourceEntry> entries)
        => new(project, entries);

    /// <summary>Finds a source that matches the engine's input schema.</summary>
    /// <param name="container">The source's identity.</param>
    /// <returns>The source, or <see langword="null"/>.</returns>
    public InputContainer? Find(AssetPath container) => this.Entries.TryGetValue(container, out var entry) ? entry.Container : null;

    private IEnumerable<InputProblem> ProblemsOf(ProjectContext project, AssetPath identity, InputSourceEntry entry)
        => entry.FormatProblems
            .Concat(entry.Container is { } container ? InputRules.CheckActionPaths(project, container, this.Find) : [])
            .Select(problem => new InputProblem(problem with { File = entry.File.FullPath }, identity));
}
