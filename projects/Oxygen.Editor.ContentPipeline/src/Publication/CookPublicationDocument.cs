// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>One immutable accepted root order and the evidence for its cooked products.</summary>
internal sealed record CookPublicationDocument(
    int Version,
    Guid ProjectId,
    Guid OperationId,
    DateTimeOffset CompletedAt,
    string MountConfigurationIdentity,
    ImmutableArray<CookPublicationRoot> Roots,
    ImmutableArray<CookProvenance.Product> Products,
    CookPublicationInputs? CookInputs)
{
    internal const int CurrentVersion = 1;

    internal static JsonSerializerOptions JsonOptions { get; } = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
    };

    internal CookProvenance ProductState => new(this.ProjectId,
        [.. this.Roots.Where(static root => root.Owner == CookPublicationRootOwner.Project)
            .Select(static root => new CookProvenance.Root(root.Name, root.SourceKey, root.IndexSha256))], this.Products);

    internal static string ConfigurationIdentity(ProjectContext project)
        => Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new
        {
            project.AuthoringMounts,
            project.LocalFolderMounts,
            Order = CookedContentOrdering.Resolve(project.LocalFolderMounts, project.CookedContentOrder),
        }, JsonOptions)));

    internal void Validate(ProjectContext project)
    {
        if (this.Version != CurrentVersion || this.ProjectId != project.ProjectId || this.OperationId == Guid.Empty
            || !IsDigest(this.MountConfigurationIdentity) || this.Roots.IsDefault || this.Products.IsDefault
            || this.Roots.Any(static root => root is null || root.SourceKey == Guid.Empty || !IsDigest(root.IndexSha256)
                || string.IsNullOrWhiteSpace(root.Name)
                || root.Owner is not (CookPublicationRootOwner.Project or CookPublicationRootOwner.Library)
                || (root.Owner == CookPublicationRootOwner.Project && root.LibraryPath is not null)
                || (root.Owner == CookPublicationRootOwner.Library && (root.LibraryPath is null || !Path.IsPathFullyQualified(root.LibraryPath))))
            || this.Roots.Select(static root => (root.Owner, root.Name.ToUpperInvariant())).Distinct().Count() != this.Roots.Length
            || (!this.Products.IsEmpty && this.CookInputs is null))
        {
            throw new InvalidDataException("The publication document has invalid ownership, identities or root bindings.");
        }

        var paths = this.Roots.Select(root => Path.GetFullPath(root.ResolvePath(project.ProjectRoot))).ToArray();
        if (paths.Distinct(StringComparer.OrdinalIgnoreCase).Count() != paths.Length || !this.ProductState.IsValid(project))
        {
            throw new InvalidDataException("The publication contains duplicate physical roots or invalid product provenance.");
        }

        if (this.CookInputs is { } inputs && (string.IsNullOrWhiteSpace(inputs.BuildFingerprint) || !IsDigest(inputs.InputIdentity)
            || inputs.Inputs.IsDefault || inputs.Documents.IsDefault || inputs.CookedDependencies.IsDefault || inputs.ProducedSourceFiles.IsDefault))
        {
            throw new InvalidDataException("The publication contains invalid consumed-input evidence.");
        }
    }

    internal static bool IsDigest(string? value) => value is { Length: 64 } && value.All(Uri.IsHexDigit);
}

/// <summary>The last cook's consumed inputs; mount-only publications preserve these facts unchanged.</summary>
internal sealed record CookPublicationInputs(
    string BuildFingerprint,
    string InputIdentity,
    ImmutableArray<CookSnapshotInput> Inputs,
    ImmutableArray<CookDocumentState> Documents,
    ImmutableArray<CookedDependencySnapshot> CookedDependencies,
    ImmutableArray<CookPublicationSourceTransition> ProducedSourceFiles);

/// <summary>A producer-owned source transition committed with cooked output.</summary>
internal sealed record CookPublicationSourceTransition(string RelativePath, string BeforeHash, string AfterHash);

/// <summary>The sole mutable selection of an immutable publication document.</summary>
internal sealed record CookPublicationHead(int Version, Guid PublicationId, string DocumentSha256)
{
    internal const int CurrentVersion = 1;
}
