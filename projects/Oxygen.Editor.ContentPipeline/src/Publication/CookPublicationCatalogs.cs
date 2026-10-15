// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>A project's current publication together with the catalogs of its cooked roots.</summary>
/// <param name="Publication">The publication the catalogs were read from; the caller owns and disposes it.</param>
/// <param name="Catalogs">One catalog per available root, in native precedence order; the caller owns and disposes them.</param>
public sealed record CookPublicationCatalogs(ICookPublicationReadLease Publication, IReadOnlyList<IAssetCatalog> Catalogs);
