// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>
/// Checks and writes the changes a scene makes of a node an instance brings in, held by the instance root's
/// <see cref="SceneInstanceComponent.Changes"/> (scene instances 6.3, 8.3).
/// </summary>
/// <remarks>
/// A change may set any property <see cref="SceneSchema.Changeable"/> lists, to a value its own rule accepts: the
/// override rule of a property with a source, the property's descriptor otherwise.
/// </remarks>
internal static class InstanceChangeWrites
{
    /// <summary>Gets the instance component of an instance root.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The component, or <see langword="null"/> when the node is not an instance root.</returns>
    public static SceneInstanceComponent? InstanceOf(SceneNode? node) => node?.Components.OfType<SceneInstanceComponent>().FirstOrDefault();

    /// <summary>Checks a change of a node an instance brings in.</summary>
    /// <param name="scope">The scene.</param>
    /// <param name="root">The instance root.</param>
    /// <param name="path">The node below the root.</param>
    /// <param name="write">The write.</param>
    /// <returns>Why the write is refused, or <see langword="null"/> when it applies.</returns>
    public static SceneDiagnostic? Check(SceneEditScope scope, SceneNode? root, NodePath path, OverrideWrite write)
    {
        // A deserialized write can still carry nulls.
        if (write.Property is not { } property || !SceneSchema.Changeable.TryGetValue(property, out var type))
        {
            return SceneDiagnostic.Error(SceneEditCodes.PropertyUnknown, $"{write.Property} cannot be changed on a node an instance brings in.", property: write.Property);
        }

        if (path.IsEmpty)
        {
            return Invalid(property, "A change names a node below the instance root.");
        }

        if (property.PartKind is null
            ? write.Part is not null
            : write.Part is not { } part || !string.Equals(part.Kind, property.PartKind, StringComparison.Ordinal) || part.Id == Guid.Empty)
        {
            return Invalid(property, property.PartKind is null ? $"{property} belongs to the component, not to a part." : $"{property} needs a {property.PartKind} part.");
        }

        if (root is null || InstanceOf(root) is null)
        {
            return SceneDiagnostic.Error(SceneEditCodes.ComponentMissing, "The node is not an instance root, so it brings in no node to change.", property: property);
        }

        if (write is { PartOwner: { } owner, Part: { } witnessed }
            && PartOwners.Resolve(scope, root, path, witnessed.Kind) != scope.Resolve(owner))
        {
            return SceneDiagnostic.Error(SceneEditCodes.PartStale, "The node no longer uses the asset the part was chosen from.", property: property);
        }

        return write.Value is null ? null : CheckValue(property, type, write);

        static SceneDiagnostic Invalid(PropertyId property, string message) => SceneDiagnostic.Error(SceneEditCodes.OverrideInvalid, message, property: property);
    }

    /// <summary>Sets or removes one change.</summary>
    /// <param name="instance">The instance root's component.</param>
    /// <param name="path">The node below the root.</param>
    /// <param name="write">The write, checked by <see cref="Check"/>; its value has its assets resolved.</param>
    /// <param name="names">
    /// The node's names in the source, which the change records as its last known names, or <see langword="null"/> when
    /// the source's structure does not hold the node now.
    /// </param>
    /// <param name="previous">The change before the write, or <see langword="null"/> when there was none.</param>
    /// <returns>Whether the changes are different now.</returns>
    public static bool Write(SceneInstanceComponent instance, NodePath path, OverrideWrite write, string? names, out SourceOverride? previous)
    {
        ArgumentNullException.ThrowIfNull(instance);
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(write);
        var node = instance.Changes.Of(path);
        var component = node.Of(write.Property.ComponentKind);
        previous = component.Find(write.Property, write.Part);
        var updated = write.Value is null
            ? component.Without(write.Property, write.Part)
            : component.With(new SourceOverride(write.Property, write.Part, write.LayoutRevision, write.Value));
        if (updated.Equals(component))
        {
            return false;
        }

        instance.Changes = instance.Changes.With(path, names is null ? node.With(updated) : node.With(updated).WithNames(names));
        return true;
    }

    private static SceneDiagnostic? CheckValue(PropertyId property, Type type, OverrideWrite write)
    {
        if (!type.IsInstanceOfType(write.Value))
        {
            return SceneDiagnostic.Error(SceneEditCodes.OverrideInvalid, $"{property} takes a {type.Name}.", property: property);
        }

        if (property.PartKind is not null && !SourceOverrides.IsLayoutRevision(write.LayoutRevision))
        {
            return SceneDiagnostic.Error(SceneEditCodes.OverrideInvalid, "Select an existing part: the layout revision it was chosen from is missing or invalid.", property: property);
        }

        if (OverrideRules.Properties.Contains(property))
        {
            return OverrideRules.Check(property, write.Value!) is { IsValid: false } refused
                ? SceneDiagnostic.Error(refused.Code, refused.Message, property: property)
                : null;
        }

        return SceneProperties.RulesOf(property.ComponentKind) is { } rules
            && rules.Descriptors.TryGetValue(property, out var descriptor)
            && descriptor.ValidateBoxed(write.Value) is { IsValid: false } invalid
            ? SceneDiagnostic.Error(SceneEditCodes.PropertyOutOfRange, invalid.Message, property: property)
            : null;
    }
}
