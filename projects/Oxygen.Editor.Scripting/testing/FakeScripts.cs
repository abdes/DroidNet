// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting.Testing;

/// <summary>
/// An in-memory <see cref="IScripts"/> for tests: the scripts and library reads a test sets.
/// </summary>
/// <remarks>Events are raised at once, on the thread that changed the scripts.</remarks>
public sealed class FakeScripts : IScripts
{
    private readonly Dictionary<AssetPath, Script> scripts = [];
    private readonly Dictionary<AssetPath, Func<CancellationToken, Task<ScriptRead>>> library = [];
    private readonly Dictionary<AssetPath, Script> pending = [];

    /// <inheritdoc />
    public event EventHandler<ScriptChangedEventArgs>? Changed;

    /// <inheritdoc />
    public event EventHandler? ProblemsChanged;

    /// <inheritdoc />
    public IReadOnlyCollection<Script> All => [.. this.scripts.Values.OrderBy(static script => script.Identity.VirtualPath, StringComparer.Ordinal)];

    /// <inheritdoc />
    public IReadOnlyList<AssetProblem> Problems => [.. this.All.SelectMany(static script => script.Problems)];

    /// <summary>Gets the number of library reads.</summary>
    public int LibraryReads { get; private set; }

    /// <summary>Gets the sources of each <see cref="CheckAsync"/>, in order.</summary>
    public IList<IReadOnlyCollection<AssetPath>> Checks { get; } = [];

    /// <summary>Gets the <c>force</c> argument of each <see cref="RefreshAsync"/>, in order.</summary>
    public IList<bool> Refreshes { get; } = [];

    /// <inheritdoc />
    public Script? Find(AssetPath script)
    {
        ArgumentNullException.ThrowIfNull(script);
        return this.scripts.GetValueOrDefault(script);
    }

    /// <inheritdoc />
    public Task<ScriptRead> ReadLibraryAsync(AssetPath script, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(script);
        this.LibraryReads++;
        return this.library.TryGetValue(script, out var read) ? read(cancellationToken) : Task.FromResult(ScriptRead.NotFound);
    }

    /// <inheritdoc />
    /// <remarks>Publishes the pending analysis of each script a source names, as the real facts analyze a source
    /// whose analysis is behind its file.</remarks>
    public Task CheckAsync(IReadOnlyCollection<AssetPath> sources, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sources);
        this.Checks.Add(sources);
        foreach (var identity in sources.Select(AssetFileTypes.ToIdentity).Where(this.pending.ContainsKey).ToList())
        {
            _ = this.pending.Remove(identity, out var script);
            _ = this.Add(script!);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc />
    public Task RefreshAsync(bool force, CancellationToken cancellationToken = default)
    {
        this.Refreshes.Add(force);
        return Task.CompletedTask;
    }

    /// <summary>Adds or replaces a script of the project.</summary>
    /// <param name="script">The script.</param>
    /// <returns>The script that was just added or replaced.</returns>
    public Script Add(Script script)
    {
        ArgumentNullException.ThrowIfNull(script);
        this.scripts[script.Identity] = script;
        this.Raise(script.Identity);
        return script;
    }

    /// <summary>Holds the analysis of a script that has not finished yet; the next <see cref="CheckAsync"/> that
    /// names its source publishes it.</summary>
    /// <param name="script">The script as its analysis will report it.</param>
    public void SetPending(Script script)
    {
        ArgumentNullException.ThrowIfNull(script);
        this.pending[script.Identity] = script;
    }

    /// <summary>Removes a script of the project.</summary>
    /// <param name="script">The script's identity.</param>
    public void Remove(AssetPath script)
    {
        if (this.scripts.Remove(script))
        {
            this.Raise(script);
        }
    }

    /// <summary>Sets what reading a library script returns.</summary>
    /// <param name="script">The script's identity.</param>
    /// <param name="read">Reads it; it may complete later, or throw.</param>
    public void SetLibrary(AssetPath script, Func<CancellationToken, Task<ScriptRead>> read)
    {
        ArgumentNullException.ThrowIfNull(script);
        ArgumentNullException.ThrowIfNull(read);
        this.library[script] = read;
    }

    private void Raise(AssetPath script)
    {
        this.Changed?.Invoke(this, new ScriptChangedEventArgs(script));
        this.ProblemsChanged?.Invoke(this, EventArgs.Empty);
    }
}
