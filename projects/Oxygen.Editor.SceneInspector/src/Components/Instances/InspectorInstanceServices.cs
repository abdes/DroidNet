// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Dialogs;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>The services the Scene Instance section swaps and unpacks an instance with, those the host provides.</summary>
/// <param name="Assets">The content catalog the source picker lists models and scenes from.</param>
/// <param name="References">The saved authored references, which leave out the scenes that would make a cycle.</param>
/// <param name="Structures">What the instances of the shown scene contain.</param>
/// <param name="Nodes">
/// Reads a new source's structure before its changes are kept by name, and the values of the nodes an unpack makes
/// owned nodes of.
/// </param>
/// <param name="Dialogs">Asks whether to keep or discard the changes.</param>
/// <param name="OperationResults">Tells the user about the changes an unpack dropped.</param>
/// <param name="StatusReducer">The reducer paired with <paramref name="OperationResults"/>.</param>
internal sealed record InspectorInstanceServices(
    IAssetFeed? Assets,
    IAuthoredReferences? References = null,
    ISceneStructures? Structures = null,
    IInstancedNodeReader? Nodes = null,
    IDialogService? Dialogs = null,
    IOperationResultPublisher? OperationResults = null,
    IStatusReducer? StatusReducer = null);
