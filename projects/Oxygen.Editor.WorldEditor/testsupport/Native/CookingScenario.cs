// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.ModelImportScenario;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed class CookingScenario : DroidNet.Tests.VisualUserInterfaceTests
{
    internal static async Task WaitForBlockedMaterialAsync(CookingPanelViewModel panel, Guid documentId, CancellationToken cancellationToken)
    {
        while (panel.SelectedRun?.Snapshot is not { State: CookRunState.NeedsSave } run || !run.UnsavedDocuments.Any(document => document.DocumentId == documentId))
        {
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }
    }

    internal static async Task InvokeCookingSaveAsync(CookingPanelViewModel panel, CookingPanelView view, CancellationToken cancellationToken)
    {
        await WaitForRenderAsync().WaitAsync(cancellationToken).ConfigureAwait(true);
        InvokeImportButton(view.FindDescendant<Button>(button => string.Equals(button.Content as string, "Save listed & Cook", StringComparison.Ordinal))!);
        if (panel.SaveListedAndCookCommand.ExecutionTask is { } pending)
        {
            await pending.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
    }
}
