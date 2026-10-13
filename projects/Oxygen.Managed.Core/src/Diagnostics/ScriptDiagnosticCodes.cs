// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for script authoring.
/// </summary>
public static class ScriptDiagnosticCodes
{
    /// <summary>
    /// A new script or input file could not be written.
    /// </summary>
    public const string CreateFailed = DiagnosticCodes.ScriptPrefix + "CREATE_FAILED";

    /// <summary>
    /// The configured code editor could not be started.
    /// </summary>
    public const string EditorLaunchFailed = DiagnosticCodes.ScriptPrefix + "EDITOR_LAUNCH_FAILED";

    /// <summary>
    /// Scripts cannot be analyzed, for example because the engine analyzer is unavailable.
    /// </summary>
    public const string AnalysisUnavailable = DiagnosticCodes.ScriptPrefix + "ANALYSIS_UNAVAILABLE";

    /// <summary>
    /// Two script sources in one folder share a name, so they would cook to the same asset.
    /// </summary>
    public const string IdentityCollision = DiagnosticCodes.ScriptPrefix + "IDENTITY_COLLISION";

    /// <summary>
    /// An input file is not valid JSON or does not match the engine's input schema.
    /// </summary>
    public const string InputInvalid = DiagnosticCodes.ScriptPrefix + "INPUT_INVALID";

    /// <summary>
    /// An input file names an action that another file declares; only self-contained input files cook.
    /// </summary>
    public const string InputReferenceUnsupported = DiagnosticCodes.ScriptPrefix + "INPUT_REFERENCE_UNSUPPORTED";

    /// <summary>
    /// The project's script tooling files (<c>.luaurc</c>, type definitions, VS Code settings) could not be updated.
    /// </summary>
    public const string ToolingUpdateFailed = DiagnosticCodes.ScriptPrefix + "TOOLING_UPDATE_FAILED";
}
