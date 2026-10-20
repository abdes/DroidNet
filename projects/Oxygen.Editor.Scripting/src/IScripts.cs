// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// The scripts of the open project, each analyzed from its saved source, and the scripts of the libraries it mounts.
/// </summary>
/// <remarks>
/// Each event handler is called on the <see cref="SynchronizationContext"/> that was current when it subscribed, or on
/// the thread pool when there was none. The members read an immutable snapshot that each change replaces.
/// </remarks>
public interface IScripts : IAssetProblemSource
{
    /// <summary>Occurs after a script was analyzed again, or went away.</summary>
    public event EventHandler<ScriptChangedEventArgs>? Changed;

    /// <summary>Gets every analyzed script of the open project.</summary>
    public IReadOnlyCollection<Script> All { get; }

    /// <summary>Finds an analyzed script of the open project.</summary>
    /// <param name="script">The script's identity, its cooked virtual path.</param>
    /// <returns>The script, or <see langword="null"/> when no source of the project is that script, or it has not
    /// been analyzed yet.</returns>
    public Script? Find(AssetPath script);

    /// <summary>Reads a script of a library the open project mounts.</summary>
    /// <param name="script">The script's identity.</param>
    /// <param name="cancellationToken">A token that cancels the read.</param>
    /// <returns>The read.</returns>
    public Task<ScriptRead> ReadLibraryAsync(AssetPath script, CancellationToken cancellationToken = default);

    /// <summary>Analyzes every script of the open project again.</summary>
    /// <param name="force">Whether to analyze a script whose source has not changed.</param>
    /// <param name="cancellationToken">A token that cancels the analysis.</param>
    /// <returns>A task that completes when every script has been analyzed.</returns>
    public Task RefreshAsync(bool force, CancellationToken cancellationToken = default);
}
