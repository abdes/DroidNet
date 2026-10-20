// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scripting;

/// <summary>
/// The engine's script analyzer, which reads a script's declarations and problems from its source. Live Preview
/// implements it over the native analyzer.
/// </summary>
public interface IScriptAnalyzer
{
    /// <summary>Gets a value indicating whether the analyzer can be loaded now, without waiting.</summary>
    public bool IsAvailable { get; }

    /// <summary>Waits until the analyzer can be loaded, such as once the engine SDK it loads from is verified.</summary>
    /// <param name="cancellationToken">A token that cancels the wait.</param>
    /// <returns>A task that completes when the analyzer is available.</returns>
    public Task WaitUntilAvailableAsync(CancellationToken cancellationToken);

    /// <summary>Analyzes one script source.</summary>
    /// <param name="sourceName">The name the problems give the source, such as its path below the project.</param>
    /// <param name="source">The source text, as UTF-8.</param>
    /// <param name="definitions">The engine's API definitions, which enable type checking; without them the source is
    /// linted only.</param>
    /// <returns>The analysis; <see cref="ScriptAnalysis.IsUnavailable"/> when the analyzer could not run.</returns>
    public ScriptAnalysis Analyze(string sourceName, ReadOnlySpan<byte> source, string? definitions);
}
