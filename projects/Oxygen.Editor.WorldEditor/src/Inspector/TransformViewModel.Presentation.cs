// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Ends hidden transform gestures without discarding field feedback.</summary>
public sealed partial class TransformViewModel
{
    private readonly Dictionary<PropertyId, Dictionary<Guid, float>> displayedSources = [];

    /// <inheritdoc />
    internal override InspectorFieldDiagnostics? ValidationFeedback => this.diagnostics;

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled)
    {
        if (!enabled)
        {
            foreach (var (field, session) in this.activeSessions.ToArray())
            {
                this.supersededFields.Add(field);
                var completion = session.Interaction == NumberBoxEditInteractionKind.Text
                    ? NumberBoxEditCompletionKind.Commit : NumberBoxEditCompletionKind.Cancel;
                _ = this.CompleteActiveSessionAsync(field, completion);
            }
        }
    }

    private void RefreshSourceFeedback(PropertyBinding<float> binding, Dictionary<Guid, object?> targets)
    {
        var current = targets.Where(static item => item.Value is not null)
            .ToDictionary(static item => item.Key, item => binding.Descriptor.Read(item.Value!));
        if (this.displayedSources.TryGetValue(binding.Id.Id, out var previous)
            && (previous.Count != current.Count || current.Any(item => !previous.TryGetValue(item.Key, out var value) || !value.Equals(item.Value))))
        {
            this.diagnostics.Invalidate(binding.Id.Id);
        }

        this.displayedSources[binding.Id.Id] = current;
    }
}
