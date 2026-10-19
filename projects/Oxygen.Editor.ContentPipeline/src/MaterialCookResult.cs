// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Result of a single material cook request.
/// </summary>
/// <param name="Material">The material's identity.</param>
/// <param name="CookedMaterialUri">The cooked material URI, when known.</param>
/// <param name="State">The cook state.</param>
/// <param name="OperationId">The related operation result identity, when one was published.</param>
public sealed record MaterialCookResult(
    AssetPath Material,
    AssetPath? CookedMaterialUri,
    MaterialCookState State,
    Guid? OperationId)
{
    /// <summary>Gets shared operation details, including captured inputs and their freshness.</summary>
    public ContentCookResult? Cook { get; init; }
}
