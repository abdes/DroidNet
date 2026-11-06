// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json.Nodes;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Authoring.Textures;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>
/// Checks a material's texture bindings against the mounts they name, before a cook: a library must hold the texture, and
/// an authored texture's descriptor and images must exist. Each problem states what the material writes, what that
/// resolves to and what was looked up.
/// </summary>
/// <param name="operationId">The operation the diagnostics belong to.</param>
/// <param name="project">The project and its mounts.</param>
internal sealed class MaterialTextureCheck(Guid operationId, ProjectContext project)
{
    /// <summary>Checks a saved material. One that cannot be read reports its own problem.</summary>
    /// <param name="input">The material being cooked.</param>
    /// <param name="materialBytes">The material's saved bytes.</param>
    /// <param name="cancellationToken">Cancels reading the textures.</param>
    /// <returns>The problems found, in the material's channel order.</returns>
    public async Task<ImmutableArray<DiagnosticRecord>> RunAsync(ContentCookInput input, byte[] materialBytes, CancellationToken cancellationToken)
    {
        MaterialSource material;
        try
        {
            material = MaterialSourceReader.Read(materialBytes, input.AssetUri);
        }
        catch (AssetFormatException exception)
        {
            return [this.CreateSourceDiagnostic(input, exception.Problem)];
        }

        var diagnostics = ImmutableArray.CreateBuilder<DiagnosticRecord>();
        var document = JsonNode.Parse(materialBytes);
        foreach (var (channel, texture) in material.TextureReferences)
        {
            var written = document?["textures"]?[channel]?["virtual_path"]?.GetValue<string>() ?? texture.ToReference(input.AssetUri);
            if (await this.CheckBindingAsync(input, new(channel, written, texture), cancellationToken).ConfigureAwait(false) is { } problem)
            {
                diagnostics.Add(problem);
            }
        }

        return diagnostics.ToImmutable();
    }

    private async Task<DiagnosticRecord?> CheckBindingAsync(ContentCookInput input, TextureBinding binding, CancellationToken cancellationToken)
    {
        var texture = binding.Texture;
        if (!texture.Name.EndsWith(AssetFileTypes.Texture, StringComparison.Ordinal))
        {
            return this.CreateReferenceDiagnostic(input, binding, texture.VirtualPath, "path is not a named texture", details: null);
        }

        if (project.CookedMounts.FirstOrDefault(mount => string.Equals(mount.Name, texture.Mount, StringComparison.Ordinal)) is { } library)
        {
            return library.IsBound && library.Library?.Holds(texture.VirtualPath) == true
                ? null
                : this.CreateReferenceDiagnostic(input, binding, library.Location?.FullPath ?? texture.VirtualPath, $"names a texture the library '{library.Name}' does not hold", details: null);
        }

        if (!CookInputResolver.IsAuthoringUri(project, texture))
        {
            return null;
        }

        var descriptorPath = this.FindDescriptor(texture);
        return descriptorPath is null || !project.FileSystem.File.Exists(descriptorPath)
            ? this.CreateReferenceDiagnostic(input, binding, descriptorPath ?? texture.VirtualPath, "texture descriptor is missing", details: null)
            : await this.CheckImagesAsync(input, binding, descriptorPath, cancellationToken).ConfigureAwait(false);
    }

    private async Task<DiagnosticRecord?> CheckImagesAsync(ContentCookInput input, TextureBinding binding, string descriptorPath, CancellationToken cancellationToken)
    {
        var identity = binding.Texture;
        TextureSource texture;
        try
        {
            texture = TextureSourceReader.Read(await project.FileSystem.File.ReadAllBytesAsync(descriptorPath, cancellationToken).ConfigureAwait(false), identity);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or AssetFormatException)
        {
            return this.CreateReferenceDiagnostic(input, binding, descriptorPath, "texture descriptor is invalid", exception.Message);
        }

        var files = project.FileSystem;
        return texture.ResolveImages(identity, project, image => files.File.Exists(image.FullPath))
            .FirstOrDefault(image => image.File is not { } file || !files.File.Exists(file.FullPath)) is { } missing
            ? this.CreateReferenceDiagnostic(input, binding, missing.File?.FullPath ?? missing.Reference, "source image is missing", details: null)
            : null;
    }

    // A texture referenced by identity cooks from its authored descriptor in an authoring mount.
    private string? FindDescriptor(AssetPath texture)
        => project.SourceMounts.Any(mount => string.Equals(mount.Name, texture.Mount, StringComparison.Ordinal))
            && project.Find(texture.WithAuthoringSuffix()) is { IsMountRoot: false } descriptor
                ? descriptor.File.FullPath
                : null;

    private DiagnosticRecord CreateSourceDiagnostic(ContentCookInput input, AssetProblem problem)
        => new()
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = problem.Code,
            Message = $"Material '{input.AssetUri.VirtualPath}' cannot be read: {problem.Message}",
            TechnicalMessage = problem.Explanation,
            AffectedPath = input.SourceAbsolutePath.FullPath,
            AffectedVirtualPath = input.AssetUri.VirtualPath,
        };

    private DiagnosticRecord CreateReferenceDiagnostic(ContentCookInput input, TextureBinding binding, string lookedUp, string issue, string? details)
        => new()
        {
            OperationId = operationId,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = "material.texture_reference_invalid",
            Message = $"Material '{input.AssetUri.VirtualPath}' texture channel '{binding.Channel}' writes '{binding.Written}', which is '{binding.Texture.VirtualPath}': {issue} (looked up '{lookedUp}').",
            TechnicalMessage = details,
            AffectedPath = lookedUp,
            AffectedVirtualPath = input.AssetUri.VirtualPath,
        };

    // One texture binding of a material: its channel, what the file writes, and what that resolves to.
    private sealed record TextureBinding(string Channel, string Written, AssetPath Texture);
}
