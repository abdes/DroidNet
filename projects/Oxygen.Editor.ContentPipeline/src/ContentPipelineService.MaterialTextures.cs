// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads the textures of the materials a source's published product holds.</summary>
public sealed partial class ContentPipelineService : IMaterialTextures
{
    /// <inheritdoc />
    /// <remarks>
    /// A cooked material binds its textures as resources of its container's texture table: the dependency metadata
    /// gives each material's texture slots, and the container's inventory the texture each slot holds.
    /// </remarks>
    public async Task<ImmutableDictionary<Guid, ImmutableArray<string>>> ReadTexturesAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.session.Lifetime);
        var token = cancellation.Token;
        var textures = ImmutableDictionary.CreateBuilder<Guid, ImmutableArray<string>>();
        if (!ReferenceEquals(this.session.Context, project) || this.engineContentPipelineApi is not ICookedDependencyInspector inspector)
        {
            return textures.ToImmutable();
        }

        using var publicationRead = await this.publication.AcquireReadAsync(project, token).ConfigureAwait(false);
        var prior = publicationRead.ProductState;
        if (prior.Products.FirstOrDefault(product => product.SourceUri == source) is not { } product)
        {
            return textures.ToImmutable();
        }

        var operationRoot = Path.Combine(project.ProjectRoot.FullPath, ".build", "inspection");
        foreach (var mount in product.Outputs.Where(static output => output.Asset.Kind == ContentCookAssetKind.Material).GroupBy(static output => output.RootMount, StringComparer.Ordinal))
        {
            var expectedRoot = prior.Roots.FirstOrDefault(root => string.Equals(root.Mount, mount.Key, StringComparison.Ordinal));
            if (expectedRoot is null || publicationRead.FindProjectRoot(mount.Key)?.FullPath is not { } cookedRoot)
            {
                continue;
            }

            var materials = mount.Select(static output => output.Asset.VirtualPath).ToHashSet(StringComparer.Ordinal);
            await this.ReadRootAsync(new MaterialRead(cookedRoot, expectedRoot, materials, inspector, operationRoot), textures, token).ConfigureAwait(false);
        }

        return textures.ToImmutable();
    }

    private async Task ReadRootAsync(MaterialRead read, ImmutableDictionary<Guid, ImmutableArray<string>>.Builder textures, CancellationToken token)
    {
        var outputs = await CookOutputReadLease.AcquireAsync(read.CookedRoot, token).ConfigureAwait(false);
        await using var lifetime = outputs.ConfigureAwait(false);
        var inventory = await outputs.ReadInventoryAsync(this.engineContentPipelineApi, token).ConfigureAwait(false);
        if (!read.ExpectedRoot.Matches(inventory) || !inventory.IsValid)
        {
            return;
        }

        CookedDependencyReport report;
        try
        {
            report = await read.Inspector.InspectDependenciesAsync(read.OperationRoot, read.CookedRoot, token).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            // The native reader drains before the read lease releases the root's files.
            await failure.DrainCompletion.ConfigureAwait(false);
            throw;
        }

        _ = outputs.GetFiles();
        // The inventory's resources are the container's textures, each in its table slot.
        var slots = inventory.Resources
            .Where(static resource => resource.ResourceIndex is not null)
            .GroupBy(static resource => resource.ResourceIndex!.Value)
            .ToDictionary(static slot => slot.Key, static slot => slot.First().VirtualPath);
        foreach (var asset in inventory.Assets.Where(asset => asset.Type == (byte)AssetType.Material && read.Materials.Contains(asset.VirtualPath)))
        {
            if (!report.Assets.TryGetValue(asset.Key.ToString(), out var dependencies))
            {
                continue;
            }

            textures[asset.Key] = [.. dependencies.ResourceBindings
                .Where(static binding => binding.Kind == CookedResourceKind.Texture && binding.State != CookedResourceBindingState.Error)
                .Select(binding => slots.GetValueOrDefault(binding.Index))
                .OfType<string>()
                .Distinct(StringComparer.Ordinal)];
        }
    }

    /// <summary>One cooked root's read: the materials asked for and the native reader.</summary>
    private sealed record MaterialRead(string CookedRoot, CookProvenance.Root ExpectedRoot, HashSet<string> Materials, ICookedDependencyInspector Inspector, string OperationRoot);
}
