// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Text;

namespace Oxygen.Editor.Scripting.Testing;

/// <summary>
/// An <see cref="IScriptAnalyzer"/> for tests: it answers each source text with the analysis a test sets for it.
/// </summary>
/// <remarks>
/// A source with no analysis set declares nothing and has no problems. The analyzer is available at once unless the
/// test makes it wait, or holds an analysis back with <see cref="HoldNext"/>.
/// </remarks>
public sealed class FakeScriptAnalyzer : IScriptAnalyzer
{
    private readonly ConcurrentDictionary<string, ScriptAnalysis> analyses = new(StringComparer.Ordinal);
    private readonly ConcurrentQueue<string> analyzed = new();
    private readonly ConcurrentQueue<AnalysisHold> holds = new();
    private TaskCompletionSource available = CreateAvailable(isAvailable: true);

    /// <summary>Gets the source names analyzed, in order.</summary>
    public IReadOnlyCollection<string> Analyzed => this.analyzed;

    /// <summary>Gets the definitions passed with the last analysis.</summary>
    public string? LastDefinitions { get; private set; }

    /// <summary>Sets the analysis of a source text.</summary>
    /// <param name="source">The source text.</param>
    /// <param name="analysis">The analysis.</param>
    public void Set(string source, ScriptAnalysis analysis)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(analysis);
        this.analyses[source] = analysis;
    }

    /// <summary>Makes analyses wait until <see cref="MakeAvailable"/>.</summary>
    public void MakeUnavailable() => this.available = CreateAvailable(isAvailable: false);

    /// <summary>Lets the analyses that wait run.</summary>
    public void MakeAvailable() => this.available.TrySetResult();

    /// <summary>Holds back the next analysis that waits, until the returned hold is released.</summary>
    /// <returns>The hold. Holds are given to analyses in the order they are made, so a test that needs two analyses to
    /// overlap makes one, waits for <see cref="AnalysisHold.Arrived"/>, and makes the next.</returns>
    public AnalysisHold HoldNext()
    {
        var hold = new AnalysisHold();
        this.holds.Enqueue(hold);
        return hold;
    }

    /// <inheritdoc />
    public async Task WaitUntilAvailableAsync(CancellationToken cancellationToken)
    {
        if (this.holds.TryDequeue(out var hold))
        {
            await hold.WaitAsync(cancellationToken).ConfigureAwait(false);
        }

        await this.available.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public ScriptAnalysis Analyze(string sourceName, ReadOnlySpan<byte> source, string? definitions)
    {
        this.analyzed.Enqueue(sourceName);
        this.LastDefinitions = definitions;

        return this.analyses.GetValueOrDefault(Encoding.UTF8.GetString(source)) ?? new ScriptAnalysis();
    }

    private static TaskCompletionSource CreateAvailable(bool isAvailable)
    {
        var source = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        if (isAvailable)
        {
            source.SetResult();
        }

        return source;
    }
}
