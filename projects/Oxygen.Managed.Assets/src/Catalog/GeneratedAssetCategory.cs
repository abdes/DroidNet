// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>Identifies the engine-owned authoring availability of a generated asset.</summary>
public enum GeneratedAssetCategory
{
    /// <summary>A regular authoring choice.</summary>
    Standard,

    /// <summary>A supported choice intended for advanced authoring.</summary>
    Advanced,

    /// <summary>An internal engine or tool resource, excluded from authoring choices.</summary>
    Internal,
}
