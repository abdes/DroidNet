// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Editor-facing material cook orchestration for scalar material authoring.
/// </summary>
public interface IMaterialCookService
{
    /// <summary>
    /// Cooks a material source descriptor and updates the loose cooked index.
    /// </summary>
    /// <param name="request">The material cook request.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    Task<MaterialCookResult> CookMaterialAsync(MaterialCookRequest request, CancellationToken cancellationToken = default);

    /// <summary>
    /// Gets the current material cook state when it can be determined without cooking.
    /// </summary>
    /// <param name="materialSourceUri">The material source asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The known material cook state.</returns>
    Task<MaterialCookState> GetMaterialCookStateAsync(Uri materialSourceUri, CancellationToken cancellationToken = default);
}
