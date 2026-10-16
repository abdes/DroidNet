// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>The result of reading a document's file again.</summary>
/// <param name="Succeeded">Whether the document now holds the file's content.</param>
/// <param name="Problem">Why it does not, as people read it; null when it does.</param>
public sealed record DocumentReloadResult(bool Succeeded, string? Problem = null);
