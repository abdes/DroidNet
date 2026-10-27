// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ModelEditor;

/// <summary>Where a model's import stands, as the Model Editor's toolbar says it.</summary>
public enum ModelImportState
{
    /// <summary>No import produced the model yet.</summary>
    NotImported,

    /// <summary>The last import is current with the source.</summary>
    Imported,

    /// <summary>The source changed since the last import.</summary>
    SourceChanged,

    /// <summary>An import of the model is queued or running.</summary>
    Importing,

    /// <summary>The last import failed.</summary>
    ImportFailed,

    /// <summary>The source file is missing.</summary>
    SourceMissing,
}
