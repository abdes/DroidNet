// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>The observed outcome of an individual asset, distinct from the overall run.</summary>
public enum CookAssetState
{
    /// <summary>The input has been discovered and is being checked.</summary>
    Preparing,

    /// <summary>The asset has been submitted for cooking.</summary>
    Cooking,

    /// <summary>The produced asset was inspected and validated.</summary>
    Updated,

    /// <summary>Existing validated output was reused.</summary>
    Reused,

    /// <summary>A diagnostic identifies this asset as failing.</summary>
    Failed,

    /// <summary>The asset was not submitted because preparation failed.</summary>
    Skipped,

    /// <summary>The operation was cancelled before this asset completed.</summary>
    Cancelled,

    /// <summary>No individual completion was reported for a submitted asset.</summary>
    Unresolved,
}
