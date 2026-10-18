// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Keeps every script of the active project analyzed, and re-analyzes a script when its file changes.
/// </summary>
/// <remarks>
/// Analysis is not cooking: it reads declarations and problems without running or compiling the script, so
/// parameters and problems update on save. Results are cached by path and content hash. Type checking runs when the
/// SDK's API definitions are available; otherwise scripts are linted only.
/// </remarks>
public interface IScriptAnalysisService
{
    /// <summary>Gets the analyses as they change, including removals.</summary>
    public IObservable<ScriptAnalysisSnapshot> Changes { get; }

    /// <summary>Gets the current analysis of every script of the active project.</summary>
    public IReadOnlyCollection<ScriptAnalysisSnapshot> Current { get; }

    /// <summary>Finds the current analysis of a script.</summary>
    /// <param name="sourcePath">The script file.</param>
    /// <returns>The analysis, or <see langword="null"/> when the file has not been analyzed.</returns>
    public ScriptAnalysisSnapshot? Find(FileSystemPath sourcePath);

    /// <summary>Analyzes a script now, unless its content is unchanged since the last analysis.</summary>
    /// <param name="sourcePath">The script file.</param>
    /// <param name="cancellationToken">A token that cancels the analysis.</param>
    /// <returns>The analysis, or <see langword="null"/> when the file is not a script of the active project.</returns>
    public Task<ScriptAnalysisSnapshot?> AnalyzeAsync(FileSystemPath sourcePath, CancellationToken cancellationToken = default);

    /// <summary>Analyzes every script of the active project again, including unchanged ones.</summary>
    /// <param name="cancellationToken">A token that cancels the analysis.</param>
    /// <returns>A task that completes when every script has been analyzed.</returns>
    public Task RefreshAsync(CancellationToken cancellationToken = default);
}
