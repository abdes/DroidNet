// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>How a save ended.</summary>
public enum DocumentSaveOutcome
{
    /// <summary>The file holds what the document captured, or the document had nothing to save.</summary>
    Saved = 0,

    /// <summary>The content refused the save; nothing was written.</summary>
    Refused = 1,

    /// <summary>The file changed since the document read or saved it, or a copy's file exists; nothing was written.</summary>
    Conflict = 2,
}
