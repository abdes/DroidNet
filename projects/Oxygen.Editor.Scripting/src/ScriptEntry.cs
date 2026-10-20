// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scripting;

/// <summary>
/// One analyzed script source: the hash of the content analyzed, the script, and the parameters of its last valid
/// version.
/// </summary>
/// <param name="Hash">The hash of the analyzed content.</param>
/// <param name="Script">The script.</param>
/// <param name="LastValidParameters">The parameters of the last analysis whose declaration was valid.</param>
internal sealed record ScriptEntry(string Hash, Script Script, IReadOnlyList<ScriptParameter> LastValidParameters);
