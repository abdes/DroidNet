// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>Writes property values to their targets and captures exactly what the writes replaced.</summary>
internal static class PropertyWrites
{
    /// <summary>Checks each value against its descriptor.</summary>
    /// <param name="rules">The kind's rules.</param>
    /// <param name="edit">The values.</param>
    /// <returns>Why a value is invalid, or <see langword="null"/> when all are valid.</returns>
    public static SceneDiagnostic? CheckValues(IComponentRules rules, PropertyEdit edit)
    {
        foreach (var (id, value) in edit)
        {
            if (!rules.Descriptors.TryGetValue(id, out var descriptor))
            {
                return SceneDiagnostic.Error(SceneEditCodes.PropertyUnknown, $"Unknown property: {id.Qualified()}.", property: id);
            }

            var validation = descriptor.ValidateBoxed(value);
            if (!validation.IsValid)
            {
                return SceneDiagnostic.Error(SceneEditCodes.PropertyOutOfRange, validation.Message, property: id);
            }
        }

        return null;
    }

    /// <summary>Reads the current values of properties from a target.</summary>
    /// <param name="rules">The kind's rules.</param>
    /// <param name="target">The target.</param>
    /// <param name="ids">The properties.</param>
    /// <returns>The values.</returns>
    public static PropertyEdit Read(IComponentRules rules, object target, IEnumerable<PropertyId> ids)
    {
        var values = new PropertyEdit.Builder();
        foreach (var id in ids)
        {
            _ = values.SetRaw(id, rules.Descriptors[id].ReadBoxed(target));
        }

        return values.Build();
    }

    /// <summary>Writes values to a target and returns what they replaced, coupled properties included.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="rules">The kind's rules.</param>
    /// <param name="target">The target, from the rules.</param>
    /// <param name="edit">The values.</param>
    /// <returns>The replaced values of the properties that changed; empty when nothing changed.</returns>
    public static PropertyEdit Write(Scene scene, IComponentRules rules, object target, PropertyEdit edit)
    {
        var ids = rules.Coupled(edit.Ids);
        var before = Read(rules, target, ids);
        PropertyApply.ApplyToTarget(target, edit, rules.Descriptors);
        rules.Commit(scene, target);
        var after = Read(rules, target, ids);
        return PropertyEdit.Of(before.Where(entry => !after.TryGetRaw(entry.Key, out var value) || !Equals(entry.Value, value)));
    }

    /// <summary>Converts a descriptor or rules failure into a validation.</summary>
    /// <param name="error">The failure, or <see langword="null"/>.</param>
    /// <returns>The validation.</returns>
    public static SceneEditValidation ToValidation(SceneDiagnostic? error)
        => error is null ? SceneEditValidation.Valid : SceneEditValidation.Invalid(error);
}
