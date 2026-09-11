// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>The outcome of a conflict action; a successful copy does not save the original document.</summary>
/// <param name="Succeeded">Whether the requested action completed.</param>
/// <param name="Message">The status to display beside the document.</param>
public sealed record DocumentConflictResult(bool Succeeded, string Message);
