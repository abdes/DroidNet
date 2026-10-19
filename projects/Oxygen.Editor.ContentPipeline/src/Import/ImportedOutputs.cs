// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Resolves an imported model's products to the model that produces them.</summary>
/// <remarks>
/// A model's products are named under its identity (<c>/Content/Ship/Ship.glb/Paint.omat</c>), so the identity alone
/// names the model that cooks a product: <see cref="CookInputResolver.ResolveContainer"/>. Unlike an input source's
/// products, the cook checks that the model still produces each one referenced, since a reimport can drop it.
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
        return CookInputResolver.ResolveContainer(project, uri, role) is { Kind: ContentCookAssetKind.ForeignSource } model ? model : null;
    }

    /// <summary>Substitutes a requested product with its container; a model keeps the product as a required output.</summary>
    /// <param name="scope">The original requested scope.</param>
    /// <returns>The scope with the container in place of a requested product.</returns>
    public static ContentCookScope ResolveScope(ContentCookScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        if (scope.TargetKind == CookTargetKind.Folder || scope.ScopeUri is not { } requested
            || CookInputResolver.ResolveContainer(scope.Project, requested, ContentCookInputRole.Primary) is not { } source)
        {
            return scope;
        }

        return source.Kind == ContentCookAssetKind.ForeignSource
            ? scope with { Inputs = [source], RequiredImportedOutputs = [requested] }
            : scope with { Inputs = [source] };
    }
}
