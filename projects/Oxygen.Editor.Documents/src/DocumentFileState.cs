// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>What a document found when it heard that its file changed.</summary>
public enum DocumentFileState
{
    /// <summary>The file holds what the document read or last saved, such as after the document's own save.</summary>
    Unchanged = 0,

    /// <summary>The file came back with what the document read or last saved, after it was missing.</summary>
    Restored = 1,

    /// <summary>The document had no edits, and it read the changed file again.</summary>
    Reloaded = 2,

    /// <summary>The file changed while the document has edits, or the changed file did not decode.</summary>
    ChangedOnDisk = 3,

    /// <summary>The file is gone; the document stays open with its content.</summary>
    Missing = 4,
}
