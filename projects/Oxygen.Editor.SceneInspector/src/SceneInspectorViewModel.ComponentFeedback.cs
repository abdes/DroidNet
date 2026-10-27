// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.SceneInspector.Editing;

namespace Oxygen.Editor.SceneInspector;

/// <summary>Keeps component feedback discoverable independently of the visible property section.</summary>
public sealed partial class SceneInspectorViewModel
{
    private readonly List<(InspectorFieldDiagnostics Source, EventHandler Handler)> feedbackSubscriptions = [];

    private void ObserveComponentFeedback(Type type, ComponentPropertyEditor editor)
    {
        if (editor.ValidationFeedback is not { } feedback)
        {
            return;
        }

        void Handler(object? sender, EventArgs args) => this.UpdateComponentFeedback(type, feedback);
        feedback.FeedbackChanged += Handler;
        this.feedbackSubscriptions.Add((feedback, Handler));
        this.UpdateComponentFeedback(type, feedback);
    }

    private void UpdateComponentFeedback(Type type, InspectorFieldDiagnostics feedback)
    {
        void Update()
        {
            if (!this.isDisposed && this.ComponentFilters.FirstOrDefault(option => option.ComponentType == type) is { } option)
            {
                option.ValidationMessage = feedback.Summary;
            }
        }

        if (this.dispatcher is { HasThreadAccess: false })
        {
            _ = this.dispatcher.TryEnqueue(Update);
        }
        else
        {
            Update();
        }
    }

    private void StopObservingComponentFeedback()
    {
        foreach (var (source, handler) in this.feedbackSubscriptions)
        {
            source.FeedbackChanged -= handler;
        }

        this.feedbackSubscriptions.Clear();
    }
}
