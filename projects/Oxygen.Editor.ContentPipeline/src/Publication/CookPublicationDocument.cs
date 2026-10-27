// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

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
    internal const int CurrentVersion = 7;

    internal static JsonSerializerOptions JsonOptions { get; } = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
    };

    internal CookProvenance ProductState
    {
        get
        {
            var projectRoots = this.Roots.Where(static root => root.Owner == CookPublicationRootOwner.Project)
                .Select(static root => new CookProvenance.Root(root.Name, root.SourceKey, root.IndexSha256));
            return new(this.ProjectId, [.. projectRoots], this.Products);
        }
    }

    internal static string ConfigurationIdentity(ProjectContext project)
    {
        var configuration = new
        {
            project.AuthoringMounts,
            project.LocalFolderMounts,
            project.PakFileMounts,
            Order = CookedContentOrdering.Resolve(project),
        };
        return Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(configuration, JsonOptions)));
    }

    /// <summary>Reads a publication document, refusing one written in another format before reading its fields.</summary>
    /// <param name="content">The document's bytes.</param>
    /// <returns>The document.</returns>
    /// <exception cref="InvalidDataException">The document is empty.</exception>
    /// <exception cref="CookPublicationFormatException">The document is in a format this editor does not read.</exception>
    internal static CookPublicationDocument Parse(ReadOnlySpan<byte> content)
    {
        var version = ReadVersion(content);
        if (version != CurrentVersion)
        {
            throw new CookPublicationFormatException(version, CurrentVersion);
        }

        return JsonSerializer.Deserialize<CookPublicationDocument>(content, JsonOptions)
            ?? throw new InvalidDataException("The publication document is empty.");
    }

    internal void Validate(ProjectContext project)
    {
        if (this.Version != CurrentVersion || this.ProjectId != project.ProjectId || this.OperationId == Guid.Empty
            || !IsDigest(this.MountConfigurationIdentity) || this.Roots.IsDefault || this.Products.IsDefault
            || this.Roots.Any(static root => root is null || root.SourceKey == Guid.Empty || !IsDigest(root.IndexSha256)
                || string.IsNullOrWhiteSpace(root.Name)
                || root.Owner is not (CookPublicationRootOwner.Project or CookPublicationRootOwner.Library)
                || (root.Owner == CookPublicationRootOwner.Project && root.LibraryPath is not null)
                || (root.Owner == CookPublicationRootOwner.Library && (root.LibraryPath is null || !Path.IsPathFullyQualified(root.LibraryPath)))
                || root.Container is not (CookPublicationRootContainer.LooseFolder or CookPublicationRootContainer.PakFile)
                || (root.Owner == CookPublicationRootOwner.Project && root.Container != CookPublicationRootContainer.LooseFolder))
            || this.Roots.Select(static root => (root.Owner, root.Name.ToUpperInvariant())).Distinct().Count() != this.Roots.Length
            || (!this.Products.IsEmpty && this.CookInputs is null))
        {
            throw new InvalidDataException("The publication document has invalid ownership, identities or root bindings.");
        }

        var paths = this.Roots.Select(root => FileSystemPath.From(root.ResolvePath(project.ProjectRoot.FullPath))).ToArray();
        if (paths.Distinct().Count() != paths.Length || !this.ProductState.IsValid(project))
        {
            throw new InvalidDataException("The publication contains duplicate physical roots or invalid product provenance.");
        }

        if (this.CookInputs is { } inputs && (!IsDigest(inputs.InputIdentity)
            || inputs.Inputs.IsDefault || inputs.Inputs.Any(static input => input is null || !CookProvenance.ValidInputFacts(input))
            || inputs.CookedDependencies.IsDefault || inputs.ProducedSourceFiles.IsDefault))
        {
            throw new InvalidDataException("The publication contains invalid consumed-input evidence.");
        }
    }

    internal static bool IsDigest(string? value) => value is { Length: 64 } && value.All(Uri.IsHexDigit);

    // The format version, read alone so that a document of another format is named as such instead of failing on a
    // field this format does not have.
    private static int? ReadVersion(ReadOnlySpan<byte> content)
    {
        var reader = new Utf8JsonReader(content);
        if (!reader.Read() || reader.TokenType != JsonTokenType.StartObject)
        {
            return null;
        }

        while (reader.Read() && reader.TokenType == JsonTokenType.PropertyName)
        {
            var isVersion = reader.ValueTextEquals("version"u8);
            if (!reader.Read())
            {
                return null;
            }

            if (isVersion)
            {
                return reader.TokenType == JsonTokenType.Number && reader.TryGetInt32(out var version) ? version : null;
            }

            reader.Skip();
        }

        return null;
    }
}
