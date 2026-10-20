// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Stable diagnostic codes of the script family, under <see cref="DiagnosticCodes.ScriptPrefix"/>.
/// </summary>
public static class ScriptDiagnosticCodes
{
    /// <summary>
    /// A new script or input file could not be written.
    /// </summary>
    public const string CreateFailed = DiagnosticCodes.ScriptPrefix + "CREATE_FAILED";

    /// <summary>
    /// Scripts cannot be analyzed, for example because the engine analyzer is unavailable.
    /// </summary>
    public const string AnalysisUnavailable = DiagnosticCodes.ScriptPrefix + "ANALYSIS_UNAVAILABLE";

    /// <summary>
    /// The project's script tooling files (<c>.luaurc</c>, type definitions, VS Code settings) could not be updated.
    /// </summary>
    public const string ToolingUpdateFailed = DiagnosticCodes.ScriptPrefix + "TOOLING_UPDATE_FAILED";

    /// <summary>
    /// A script slot refers to a script whose source no longer exists; the scene does not cook.
    /// </summary>
    public const string SlotScriptMissing = DiagnosticCodes.ScriptPrefix + "SLOT_SCRIPT_MISSING";

    /// <summary>
    /// A node parameter refers to a node the scene no longer has; the scene does not cook.
    /// </summary>
    public const string SlotNodeMissing = DiagnosticCodes.ScriptPrefix + "SLOT_NODE_MISSING";

    /// <summary>
    /// A script slot has no script yet; it is left out of the cooked scene.
    /// </summary>
    public const string SlotWithoutScript = DiagnosticCodes.ScriptPrefix + "SLOT_WITHOUT_SCRIPT";

    /// <summary>
    /// A script slot holds a value of another type than its script declares for that parameter.
    /// </summary>
    public const string SlotValueKindMismatch = DiagnosticCodes.ScriptPrefix + "SLOT_VALUE_KIND_MISMATCH";

    /// <summary>
    /// An asset parameter holds an asset of another type than its script declares.
    /// </summary>
    public const string SlotAssetTypeMismatch = DiagnosticCodes.ScriptPrefix + "SLOT_ASSET_TYPE_MISMATCH";
}
