// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Represents an import request issued by the editor.
/// </summary>
/// <param name="ProjectRoot">Absolute path to the project root directory.</param>
/// <param name="Inputs">The inputs to import.</param>
/// <param name="Options">Import options.</param>
public sealed record ImportRequest(
    string ProjectRoot,
    IReadOnlyList<ImportInput> Inputs,
    ImportOptions Options);
