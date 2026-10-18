// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Materials.Documents;

/// <summary>
/// Result of applying one scalar material edit.
/// </summary>
/// <param name="Succeeded">Whether the edit was accepted.</param>
/// <param name="OperationId">The related operation result identity, when one was published.</param>
/// <param name="Code">Why the edit was refused, one of the <see cref="MaterialDiagnosticCodes"/> or a property check's code; <see langword="null"/> when it was accepted.</param>
public sealed record MaterialEditResult(
    bool Succeeded,
    Guid? OperationId,
    string? Code = null);
