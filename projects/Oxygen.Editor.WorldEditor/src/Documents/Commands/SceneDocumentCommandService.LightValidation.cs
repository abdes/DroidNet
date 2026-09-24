// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

public sealed partial class SceneDocumentCommandService
{
    private static ValidationIssue? ValidateDirectionalLightCandidates(
        Scene scene, IReadOnlyDictionary<Guid, DirectionalLightEdit> edits)
    {
        var candidates = new Dictionary<Guid, DirectionalLightData>();
        var assignmentChanged = false;
        foreach (var (nodeId, edit) in edits)
        {
            var source = FindNode(scene, nodeId)?.Components.OfType<DirectionalLightComponent>().FirstOrDefault();
            if (source is null) continue;
            var candidate = new DirectionalLightComponent { Name = source.Name };
            candidate.Hydrate(source.Dehydrate());
            ApplyDirectionalLightEdit(candidate, edit);
            var data = (DirectionalLightData)candidate.Dehydrate();
            if (LightValidation.Validate(data) is { } error)
            {
                return new("LIGHT_CANDIDATE_INVALID", "Light was not edited", error, IsFailure: true);
            }
            candidates[nodeId] = data;
            assignmentChanged |= data.AtmosphereSlot != source.AtmosphereSlot;
        }
        if (assignmentChanged && LightValidation.ValidateScene(scene, candidates) is { } conflict)
        {
            return new("LIGHT_ATMOSPHERE_SLOT_OCCUPIED", "Light was not edited", conflict, IsFailure: true);
        }
        return null;
    }
}
