// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Represents an asset catalog query.
/// </summary>
/// <param name="Scope">Client-controlled scope describing where to search.</param>
/// <param name="SearchText">Optional free-text search criteria (provider-defined semantics).</param>
public sealed record AssetQuery(AssetQueryScope Scope, string? SearchText = null);
