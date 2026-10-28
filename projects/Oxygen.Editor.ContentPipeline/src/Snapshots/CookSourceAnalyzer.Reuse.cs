// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Reuses accepted source facts independently of cooked-output health.</summary>
internal sealed partial class CookSourceAnalyzer
{
    /// <summary>Projects output repairs from accepted snapshot bytes without another source-analysis pass.</summary>
    /// <param name="inputs">Scene sources whose generated descriptors need rebuilding.</param>
    /// <param name="snapshot">The accepted saved-input snapshot.</param>
    /// <param name="cancellationToken">Cancels descriptor preparation.</param>
    /// <returns>The prepared scene descriptors, their captured inputs and diagnostics.</returns>
    internal async Task<CookSourceFrontier> PrepareCapturedScenesAsync(
        IReadOnlyList<ContentCookInput> inputs,
        CookInputSnapshot snapshot,
        CancellationToken cancellationToken)
    {
        _ = Directory.CreateDirectory(this.root);
        var prepared = new List<PreparedSource>();
        foreach (var input in inputs)
        {
            prepared.Add(await this.PrepareAsync(input, cancellationToken, snapshot).ConfigureAwait(false));
        }

        var diagnostics = prepared.SelectMany(static source => source.Scene!.Diagnostics).ToImmutableArray();
        if (diagnostics.Any(static issue => issue.Severity >= Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Error))
        {
            return new([], [], diagnostics);
        }

        var descriptors = prepared.Select(source => this.Rebase(source.Input with
        {
            SourceAbsolutePath = FileSystemPath.From(source.Scene!.DescriptorPath),
            Role = ContentCookInputRole.GeneratedDescriptor,
        }));
        var captured = await CookInputSnapshotCapture.CaptureGeneratedAsync(descriptors, cancellationToken).ConfigureAwait(false);
        return new(
            [.. prepared.Select(static source => new CookSourceFacts(source.Input, [], [], source.ManagedFiles) { Job = source.Job, Scene = source.Scene })],
            captured,
            diagnostics);
    }

    private async Task<Dictionary<AssetPath, CookSourceFacts>> ReadReusableFactsAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
    {
        var reused = new Dictionary<AssetPath, CookSourceFacts>();
        if (previous?.Products.IsEmpty != false)
        {
            return reused;
        }

        var products = previous.Products.ToDictionary(static product => product.SourceUri);
        var importers = await native.ReadImporterRevisionsAsync(artifacts, cancellationToken).ConfigureAwait(false);
        var accepted = await new AcceptedCookSourceFacts(
                operation.Project,
                previous,
                CookSavedSourceReader.HashAsync)
            .ReadAsync(inputs, cancellationToken).ConfigureAwait(false);
        foreach (var facts in accepted.Sources)
        {
            if (facts.RequiresAnalysis || !products.TryGetValue(facts.Input.AssetUri, out var product)
                || product.SourceInput is not { } prior
                || prior.Kind != facts.Input.Kind
                || !string.Equals(prior.MountName, facts.Input.MountName, StringComparison.Ordinal)
                || !string.Equals(prior.SourceRelativePath, facts.Input.SourceRelativePath, StringComparison.Ordinal))
            {
                continue;
            }

            var fingerprint = CookIncrementalPlanner.Fingerprint(facts.Input, facts.Files, product.Dependencies, product.CookedDependencies, importers);
            if (string.Equals(fingerprint, product.ReuseFingerprint, StringComparison.Ordinal))
            {
                reused.Add(facts.Input.AssetUri, facts);
            }
        }

        return reused;
    }
}
