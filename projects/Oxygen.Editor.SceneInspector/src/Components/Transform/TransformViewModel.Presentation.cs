// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Transform;

/// <summary>Ends hidden transform gestures without discarding field feedback.</summary>
public sealed partial class TransformViewModel
{
    private readonly Dictionary<PropertyId, Dictionary<NodeId, object?>> displayedSources = [];

    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.diagnostics;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled)
    {
        this.controller.SetInputEnabled(enabled);
        this.flagEdits?.SetInputEnabled(enabled);
    }

    private void RefreshSourceFeedback(PropertyDescriptor descriptor, Dictionary<NodeId, object?> targets)
    {
        var current = targets.Where(static item => item.Value is not null)
            .ToDictionary(static item => item.Key, item => descriptor.ReadBoxed(item.Value!));
        if (this.displayedSources.TryGetValue(descriptor.Id, out var previous)
            && (previous.Count != current.Count || current.Any(item => !previous.TryGetValue(item.Key, out var value) || !Equals(value, item.Value))))
        {
            this.diagnostics.Invalidate(descriptor.Id);
        }

        this.displayedSources[descriptor.Id] = current;
    }
}
