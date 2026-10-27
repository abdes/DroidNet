// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>
/// The cook's warnings about kept changes, for the Problems pane (scene instances 8.10): each cook of a scene replaces
/// that scene's warnings with its own.
/// </summary>
public sealed class KeptChangeProblems : IAssetProblemSource, IDisposable
{
    private readonly ICookRunService runs;
    private readonly SubscriberEvent<EventHandler> problemsChanged = new();
    private readonly Lock gate = new();
    private ImmutableDictionary<string, ImmutableArray<CookProblem>> scenes = ImmutableDictionary.Create<string, ImmutableArray<CookProblem>>(StringComparer.Ordinal);
    private ImmutableArray<AssetProblem> problems = [];

    /// <summary>Initializes a new instance of the <see cref="KeptChangeProblems"/> class.</summary>
    /// <param name="runs">The session's cook runs.</param>
    public KeptChangeProblems(ICookRunService runs)
    {
        ArgumentNullException.ThrowIfNull(runs);
        this.runs = runs;
        runs.CookCompleted += this.OnCookCompletedAsync;
    }

    /// <inheritdoc/>
    public event EventHandler? ProblemsChanged
    {
        add => this.problemsChanged.Add(value);
        remove => this.problemsChanged.Remove(value);
    }

    /// <inheritdoc/>
    public IReadOnlyList<AssetProblem> Problems => this.problems;

    /// <summary>Takes a completed cook's kept-change warnings in place of those of the scenes it cooked.</summary>
    /// <param name="result">The cook's result.</param>
    public void Take(ContentCookResult result)
    {
        ArgumentNullException.ThrowIfNull(result);
        var reported = result.Diagnostics
            .Where(static issue => KeptChangeDiagnostics.IsKeptChange(issue.Code) && issue.AffectedVirtualPath is not null)
            .GroupBy(static issue => issue.AffectedVirtualPath!, StringComparer.Ordinal)
            .ToDictionary(static group => group.Key, static group => group.Select(static issue => new CookProblem(issue)).ToImmutableArray(), StringComparer.Ordinal);
        var cooked = result.CookedAssets.Concat(result.ReusedAssets)
            .Where(static asset => asset.Kind == ContentCookAssetKind.Scene)
            .Select(static asset => asset.SourceAssetUri.VirtualPath)
            .Concat(reported.Keys)
            .ToHashSet(StringComparer.Ordinal);
        if (cooked.Count == 0)
        {
            return;
        }

        lock (this.gate)
        {
            var updated = this.scenes.RemoveRange(cooked).SetItems(reported);
            if (updated.Count == 0 && this.scenes.Count == 0)
            {
                return;
            }

            this.scenes = updated;
            this.problems = [.. updated.OrderBy(static scene => scene.Key, StringComparer.Ordinal).SelectMany(static scene => scene.Value)];
        }

        this.problemsChanged.Invoke(handler => handler(this, EventArgs.Empty));
    }

    /// <inheritdoc/>
    public void Dispose() => this.runs.CookCompleted -= this.OnCookCompletedAsync;

    private Task OnCookCompletedAsync(CookCompletedEventArgs args)
    {
        this.Take(args.Result);
        return Task.CompletedTask;
    }
}
