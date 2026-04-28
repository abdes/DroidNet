// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Result of saving a material document.
/// </summary>
/// <param name="Succeeded">Whether the descriptor was persisted.</param>
/// <param name="OperationId">The related operation result identity, when one was published.</param>
public sealed record MaterialSaveResult(bool Succeeded, Guid? OperationId);
