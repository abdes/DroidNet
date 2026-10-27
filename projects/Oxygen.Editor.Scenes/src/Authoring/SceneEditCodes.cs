// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>The stable codes of scene edit diagnostics, one per rule category.</summary>
/// <remarks>A new rule reuses a category; a new category is a change to the scene edit design.</remarks>
public static class SceneEditCodes
{
    /// <summary>The scene was closed or replaced by a reload.</summary>
    public const string Retired = DiagnosticCodes.ScenePrefix + "Edit.Retired";

    /// <summary>A target node no longer exists.</summary>
    public const string TargetMissing = DiagnosticCodes.ScenePrefix + "Target.Missing";

    /// <summary>A target node, or one of its ancestors, is locked; the diagnostic names the target.</summary>
    public const string TargetLocked = DiagnosticCodes.ScenePrefix + "Target.Locked";

    /// <summary>A batch has no edits.</summary>
    public const string BatchEmpty = DiagnosticCodes.ScenePrefix + "Batch.Empty";

    /// <summary>An edit names no element to act on, such as no node to remove or hide.</summary>
    public const string NothingToEdit = DiagnosticCodes.ScenePrefix + "Edit.Empty";

    /// <summary>A property is not one of its kind's, or the edit mixes kinds.</summary>
    public const string PropertyUnknown = DiagnosticCodes.ScenePrefix + "Property.Unknown";

    /// <summary>A value breaks its descriptor's rule, non-finite and zero scale included; the diagnostic names the property.</summary>
    public const string PropertyOutOfRange = DiagnosticCodes.ScenePrefix + "Property.OutOfRange";

    /// <summary>Values break a rule across fields or nodes, such as near before far, cascade order or spot cones.</summary>
    public const string PropertiesInconsistent = DiagnosticCodes.ScenePrefix + "Properties.Inconsistent";

    /// <summary>A node lacks the component an edit addresses.</summary>
    public const string ComponentMissing = DiagnosticCodes.ScenePrefix + "Component.Missing";

    /// <summary>A node already has a component of the family, or the scene a component with the identity.</summary>
    public const string ComponentDuplicate = DiagnosticCodes.ScenePrefix + "Component.Duplicate";

    /// <summary>The component cannot be removed: the transform.</summary>
    public const string ComponentNotRemovable = DiagnosticCodes.ScenePrefix + "Component.NotRemovable";

    /// <summary>The component kind cannot be added.</summary>
    public const string ComponentUnsupported = DiagnosticCodes.ScenePrefix + "Component.Unsupported";

    /// <summary>Another light owns the atmosphere slot.</summary>
    public const string AtmosphereSlotOccupied = DiagnosticCodes.ScenePrefix + "AtmosphereSlot.Occupied";

    /// <summary>A name is empty.</summary>
    public const string NameEmpty = DiagnosticCodes.ScenePrefix + "Name.Empty";

    /// <summary>The scene already has a node with the identity.</summary>
    public const string NodeExists = DiagnosticCodes.ScenePrefix + "Node.Exists";

    /// <summary>A folder, or a layout entry or container, is no longer in the layout.</summary>
    public const string FolderMissing = DiagnosticCodes.ScenePrefix + "Folder.Missing";

    /// <summary>The layout already has a folder with the identity.</summary>
    public const string FolderExists = DiagnosticCodes.ScenePrefix + "Folder.Exists";

    /// <summary>A folder removed without promoting its entries is not empty.</summary>
    public const string FolderNotEmpty = DiagnosticCodes.ScenePrefix + "Folder.NotEmpty";

    /// <summary>An index is outside <c>[0, count]</c>, or entries are no longer where an edit expects them.</summary>
    public const string PositionOutOfRange = DiagnosticCodes.ScenePrefix + "Position.OutOfRange";

    /// <summary>A node or a folder would move into itself or under its own descendant.</summary>
    public const string HierarchyCycle = DiagnosticCodes.ScenePrefix + "Hierarchy.Cycle";

    /// <summary>An instance would instance the scene it is in, directly or through the sources it references.</summary>
    public const string InstanceCycle = DiagnosticCodes.ScenePrefix + "Instance.Cycle";

    /// <summary>
    /// A structural command names a node an instance brought in, whose structure belongs to the instance's source.
    /// </summary>
    public const string InstancedStructure = DiagnosticCodes.ScenePrefix + "Instance.Structure";

    /// <summary>
    /// An edit needs the contents of an instance's source, which are not available yet: whether an ancestor of a node
    /// an instance brings in is locked, where a swap finds a change's node by name, or the nodes and values Unpack and
    /// Copy make owned nodes of. The diagnostic names the instance root.
    /// </summary>
    public const string InstanceUnavailable = DiagnosticCodes.ScenePrefix + "Instance.Unavailable";

    /// <summary>
    /// A change of an instance names a node path its source does not hold, a kept change: Unpack drops it and lists it in
    /// a warning naming the instance root, as a swap that keeps changes by name lists those it keeps and <i>Reassign</i>
    /// in the model lists the changes it makes kept; <i>Reassign</i> refuses a node the source does not hold.
    /// </summary>
    public const string NodePathUnresolved = DiagnosticCodes.ScenePrefix + "Instance.NodePathUnresolved";

    /// <summary>
    /// The file of a scene an edit changes outside the open scene, the source <i>Apply to source</i> writes, no longer
    /// holds what the edit expects: it changed since it was read, or since the edit wrote it.
    /// </summary>
    public const string SourceChanged = DiagnosticCodes.ScenePrefix + "Instance.SourceChanged";

    /// <summary>A layout place is in another scene scope than the node's scene parent.</summary>
    public const string LayoutScopeMismatch = DiagnosticCodes.ScenePrefix + "Layout.ScopeMismatch";

    /// <summary>Keeping the world transform would give an unrepresentable local transform.</summary>
    public const string TransformUnrepresentable = DiagnosticCodes.ScenePrefix + "Transform.Unrepresentable";

    /// <summary>A geometry component needs a geometry asset; remove the component to detach geometry.</summary>
    public const string GeometryRequired = DiagnosticCodes.ScenePrefix + "Geometry.Required";

    /// <summary>An asset reference has the wrong scheme or type, or is not a valid path.</summary>
    public const string AssetReferenceInvalid = DiagnosticCodes.ScenePrefix + "AssetReference.Invalid";

    /// <summary>A warning: a restored reference names an asset deleted in this session, so it shows as missing.</summary>
    public const string AssetReferenceDeleted = DiagnosticCodes.ScenePrefix + "AssetReference.Deleted";

    /// <summary>A surface edit was built against another geometry or inventory revision.</summary>
    public const string SurfaceStale = DiagnosticCodes.ScenePrefix + "Surface.Stale";

    /// <summary>An override names a part chosen from an asset the node's component no longer uses.</summary>
    public const string PartStale = DiagnosticCodes.ScenePrefix + "Part.Stale";

    /// <summary>An override does not fit its property: a missing or wrong part, an invalid layout revision, or a value of another type.</summary>
    public const string OverrideInvalid = DiagnosticCodes.ScenePrefix + "Override.Invalid";

    /// <summary>The geometry's surface inventory could not be read.</summary>
    public const string SurfaceInventoryUnavailable = DiagnosticCodes.ScenePrefix + "Surface.InventoryUnavailable";

    /// <summary>The starting camera is not a camera node of the scene.</summary>
    public const string StartingCameraInvalid = DiagnosticCodes.ScenePrefix + "StartingCamera.Invalid";

    /// <summary>An entry repeats, such as an input context or a script slot identity.</summary>
    public const string EntryDuplicate = DiagnosticCodes.ScenePrefix + "Entry.Duplicate";

    /// <summary>A list entry is missing, or a script slot has no identity.</summary>
    public const string EntryMissing = DiagnosticCodes.ScenePrefix + "Entry.Missing";

    /// <summary>No workspace keeps editor visibility.</summary>
    public const string InteractionPortMissing = DiagnosticCodes.ScenePrefix + "InteractionPort.Missing";

    /// <summary>A gesture call does not fit the gesture: it ended, or names other targets or properties.</summary>
    public const string GestureMismatch = DiagnosticCodes.ScenePrefix + "Gesture.Mismatch";

    /// <summary>A gesture's target was removed or replaced, so the gesture was cancelled.</summary>
    public const string GestureCancelled = DiagnosticCodes.ScenePrefix + "Gesture.Cancelled";
}
