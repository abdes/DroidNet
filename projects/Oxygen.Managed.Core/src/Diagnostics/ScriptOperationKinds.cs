// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Stable operation kinds for script authoring.
/// </summary>
public static class ScriptOperationKinds
{
    /// <summary>
    /// Creates a script from the template.
    /// </summary>
    public const string Create = "Script.Create";

    /// <summary>
    /// Creates a hand-written input file from the template.
    /// </summary>
    public const string CreateInputFile = "Script.CreateInputFile";

    /// <summary>
    /// Opens a script or input file in the code editor.
    /// </summary>
    public const string OpenInCodeEditor = "Script.OpenInCodeEditor";

    /// <summary>
    /// Analyzes the project's scripts.
    /// </summary>
    public const string Analyze = "Script.Analyze";

    /// <summary>
    /// Updates the project's script tooling files.
    /// </summary>
    public const string UpdateTooling = "Script.UpdateTooling";
}
