// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// The rules of a scene's script slots against the scripts they run, the same for the inspector, the scene edits and
/// the cook.
/// </summary>
/// <remarks>
/// A slot stores only the values that differ from its script's declared defaults. A value its script does not declare
/// with that type is stale: the cook refuses it, and the inspector offers to remove it.
/// </remarks>
public static class ScriptSlotRules
{
    /// <summary>Finds the values a slot holds that its script does not declare with that type.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="declared">The script's declared parameters.</param>
    /// <returns>The stale parameter names, in name order.</returns>
    public static IReadOnlyList<string> FindStale(ScriptSlot slot, IReadOnlyList<ScriptParameter> declared)
    {
        ArgumentNullException.ThrowIfNull(slot);
        ArgumentNullException.ThrowIfNull(declared);
        return [.. slot.Overrides
            .Where(entry => declared.FirstOrDefault(parameter => string.Equals(parameter.Name, entry.Key, StringComparison.Ordinal)) is not { } parameter
                || parameter.ValueKind != entry.Value.Kind)
            .Select(static entry => entry.Key)];
    }

    /// <summary>Tells whether a value is its parameter's declared default, so a slot does not store it.</summary>
    /// <param name="parameter">The declared parameter.</param>
    /// <param name="value">The value; <see langword="null"/> takes the default.</param>
    /// <returns>Whether the slot stores no value for it.</returns>
    public static bool IsDefault(ScriptParameter parameter, ScriptValue? value)
    {
        ArgumentNullException.ThrowIfNull(parameter);
        return value?.Equals(parameter.Default) != false;
    }

    /// <summary>Checks a value a slot holds for a declared parameter: its type, and the type of the asset it names.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="parameter">The declared parameter.</param>
    /// <param name="value">The value.</param>
    /// <returns>The problem, or <see langword="null"/> when the parameter accepts the value.</returns>
    public static ScriptSlotProblem? CheckValue(Guid slot, ScriptParameter parameter, ScriptValue value)
    {
        ArgumentNullException.ThrowIfNull(parameter);
        ArgumentNullException.ThrowIfNull(value);
        var field = new ScriptFieldAddress(slot, parameter.Name);
        if (parameter.ValueKind != value.Kind)
        {
            return new ScriptSlotProblem(
                DiagnosticSeverity.Error,
                ScriptDiagnosticCodes.SlotValueKindMismatch,
                $"Parameter '{parameter.Name}' holds a {ScriptingComponent.GetTypeName(value.Kind)} value, but the script declares another type. Remove the value.",
                field);
        }

        // An asset whose type the file name does not tell is left to the cook, which reads the cooked asset.
        return value.Asset is { } asset && parameter.AssetType != 0
            && AssetFileTypes.Find(asset.VirtualPath)?.AssetType is { } assetType && assetType != AssetType.Unknown
            && (byte)assetType != parameter.AssetType
                ? new ScriptSlotProblem(
                    DiagnosticSeverity.Error,
                    ScriptDiagnosticCodes.SlotAssetTypeMismatch,
                    $"Parameter '{parameter.Name}' names '{asset.VirtualPath}', which is not the type of asset the script declares. Pick another asset.",
                    field)
                : null;
    }

    /// <summary>Checks a slot: its script, the nodes its values name and, when its script is known, each value.</summary>
    /// <param name="slot">The slot.</param>
    /// <param name="hasNode">Tells whether the scene has a node.</param>
    /// <param name="scriptExists">Tells whether a script the slot runs exists.</param>
    /// <param name="script">The slot's script, when known; its values are checked against its declarations.</param>
    /// <returns>The problems; a slot without a script, or whose script is missing, has only that problem.</returns>
    public static IReadOnlyList<ScriptSlotProblem> Check(ScriptSlot slot, Func<NodeId, bool> hasNode, Func<AssetPath, bool> scriptExists, Script? script = null)
    {
        ArgumentNullException.ThrowIfNull(slot);
        ArgumentNullException.ThrowIfNull(hasNode);
        ArgumentNullException.ThrowIfNull(scriptExists);
        if (slot.Script is not { } path)
        {
            return [new ScriptSlotProblem(DiagnosticSeverity.Warning, ScriptDiagnosticCodes.SlotWithoutScript, "No script is picked, so the slot is left out of the cooked scene.", new ScriptFieldAddress(slot.Id))];
        }

        if (!path.VirtualPath.EndsWith(AssetFileTypes.Script, StringComparison.Ordinal) || !scriptExists(path))
        {
            return [new ScriptSlotProblem(DiagnosticSeverity.Error, ScriptDiagnosticCodes.SlotScriptMissing, $"The script '{path.VirtualPath}' no longer exists. Pick another script.", new ScriptFieldAddress(slot.Id))];
        }

        var problems = new List<ScriptSlotProblem>();
        foreach (var (name, value) in slot.Overrides)
        {
            if (value.Kind == ScriptValueKind.NodeValue && !hasNode(value.NodeId))
            {
                problems.Add(new ScriptSlotProblem(DiagnosticSeverity.Error, ScriptDiagnosticCodes.SlotNodeMissing, $"Parameter '{name}' refers to a node the scene no longer has. Pick another node or clear it.", new ScriptFieldAddress(slot.Id, name)));
            }
            else if (script?.FindParameter(name) is { } parameter && CheckValue(slot.Id, parameter, value) is { } problem)
            {
                problems.Add(problem);
            }
        }

        return problems;
    }
}
