// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Result of a single material cook request.
/// </summary>
/// <param name="MaterialSourceUri">The material source asset URI.</param>
/// <param name="CookedMaterialUri">The cooked material URI, when known.</param>
/// <param name="State">The cook state.</param>
/// <param name="OperationId">The related operation result identity, when one was published.</param>
public sealed record MaterialCookResult(
    Uri MaterialSourceUri,
    Uri? CookedMaterialUri,
    MaterialCookState State,
    Guid? OperationId);
