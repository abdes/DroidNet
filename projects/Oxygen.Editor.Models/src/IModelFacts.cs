// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>
/// Reads a model's facts, which a pane such as Properties shows without knowing the module that reads them.
/// </summary>
public interface IModelFacts
{
    /// <summary>Reads a model's facts from its source file and its last import.</summary>
    /// <param name="source">The model's source file.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The model's facts.</returns>
    public Task<ModelFacts> ReadAsync(AssetPath source, CancellationToken cancellationToken = default);
}
