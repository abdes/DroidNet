// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>The state of a file in the import dialog's list.</summary>
public enum ModelImportFileStatus
{
    /// <summary>The file can be imported as it is.</summary>
    Ready,

    /// <summary>The analysis found problems the import will report; the file can still be imported.</summary>
    Warning,

    /// <summary>The file blocks the import: its source is missing or its name is taken.</summary>
    Error,
}
