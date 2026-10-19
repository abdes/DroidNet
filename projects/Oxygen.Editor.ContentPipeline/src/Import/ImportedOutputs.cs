// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Resolves an imported model's products to the model that produces them.</summary>
/// <remarks>
/// A model's products are named under its identity (<c>/Content/Ship/Ship.glb/Paint.omat</c>), so the identity alone
/// names the model that cooks a product: <see cref="AssetFileTypes.ContainerOf"/>. A model is imported when its import
/// settings sit beside it; a model file without them produces nothing. Nothing authored can be named under a model,
/// since the model is a file.
/// </remarks>
internal static class ImportedOutputs
{
    /// <summary>Finds the model that must produce a product; existence is checked after cooking.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="uri">The requested product identity.</param>
    /// <param name="role">The source's role in the closure.</param>
    /// <returns>The model's input, or null for an identity no imported model produces.</returns>
    public static ContentCookInput? ResolveOutput(ProjectContext project, AssetPath uri, ContentCookInputRole role)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(uri);
        // A model in a cooked library is cooked by its library, not by this project.
        if (AssetFileTypes.ContainerOf(uri) is not { } model || !AssetFileTypes.Is(model.Name, AssetFileCategory.Model)
            || !project.AuthoringMounts.Any(mount => string.Equals(mount.Name, model.Mount, StringComparison.Ordinal)))
        {
            return null;
        }

        var source = CookInputResolver.Resolve(project, model, role);
        return File.Exists(source.SourceAbsolutePath.FullPath + NativeSceneImportSettings.SidecarSuffix) ? source : null;
    }

    /// <summary>Substitutes a requested product with its model, keeping the product as a required output.</summary>
    /// <param name="scope">The original requested scope.</param>
    /// <returns>The scope with the model in place of a requested product.</returns>
    public static ContentCookScope ResolveScope(ContentCookScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        return scope.TargetKind != CookTargetKind.Folder && scope.ScopeUri is { } requested
            && ResolveOutput(scope.Project, requested, ContentCookInputRole.Primary) is { } source
            ? scope with { Inputs = [source], RequiredImportedOutputs = [requested] }
            : scope;
    }
}
