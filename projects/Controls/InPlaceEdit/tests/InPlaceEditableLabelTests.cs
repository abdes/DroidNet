// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using AwesomeAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("InPlaceEditableLabel")]
[TestCategory("UITest")]
public class InPlaceEditableLabelTests : VisualUserInterfaceTests
{
    private InPlaceEditableLabel? inPlaceEditableLabel;
    private TestVisualStateManager? vsm;

    [TestMethod]
    public Task UpdatesDisplayTextWhenTextChanges_Async() => EnqueueAsync(() =>
    {
        // Act
        this.inPlaceEditableLabel!.Text = null;

        // Assert
        _ = this.inPlaceEditableLabel.DisplayText.Should().Be("Multiple Values");
    });

    [TestMethod]
    public Task TransitionsToEditingState_Async() => EnqueueAsync(() =>
    {
        // Act
        this.inPlaceEditableLabel!.StartEdit();

        // Assert
        _ = this.vsm!.GetCurrentStates(this.inPlaceEditableLabel).Should().Contain("Editing");
    });

    [TestMethod]
    public Task TransitionsToNormalState_Async() => EnqueueAsync(() =>
    {
        // Setup
        this.inPlaceEditableLabel!.StartEdit();

        // Act
        this.inPlaceEditableLabel.CommitEdit();

        // Assert
        _ = this.vsm!.GetCurrentStates(this.inPlaceEditableLabel).Should().Contain("Normal");
    });

    [TestMethod]
    public Task TransitionsToInvalidValueState_Async() => EnqueueAsync(() =>
    {
        // Setup
        this.inPlaceEditableLabel!.StartEdit();
        this.inPlaceEditableLabel.Validate += (_, args) => args.IsValid = false;

        // Act
        this.inPlaceEditableLabel.OnTextChanged();

        // Assert
        _ = this.vsm!.GetCurrentStates(this.inPlaceEditableLabel).Should().Contain("InvalidValue");
    });

    [TestMethod]
    public Task FiresValidateEvent_Async() => EnqueueAsync(() =>
    {
        // Setup
        var eventFired = false;
        this.inPlaceEditableLabel!.Validate += (_, _) => eventFired = true;

        // Act
        this.inPlaceEditableLabel.StartEdit();
        this.inPlaceEditableLabel.OnTextChanged();

        // Assert
        _ = eventFired.Should().BeTrue();
    });

    [TestMethod]
    public Task SetsTemplatePartsCorrectly_Async() => EnqueueAsync(() =>
    {
        // Assert
        _ = this.inPlaceEditableLabel!
            .FindDescendant<TextBox>(e =>
                string.Equals(e.Name, InPlaceEditableLabel.EditBoxPartName, StringComparison.Ordinal))
            .Should().NotBeNull();
        _ = this.inPlaceEditableLabel!.FindDescendant<ContentPresenter>(e =>
                string.Equals(e.Name, InPlaceEditableLabel.LabelContentPresenterPartName, StringComparison.Ordinal))
            .Should().NotBeNull();
        _ = this.inPlaceEditableLabel!.FindDescendant<FontIcon>(e =>
                string.Equals(e.Name, InPlaceEditableLabel.ValueErrorPartName, StringComparison.Ordinal))
            .Should().NotBeNull();
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(async () =>
        {
            this.inPlaceEditableLabel = new InPlaceEditableLabel
            {
                Text = "Test",
                Content = new TextBlock { Name = "Label" },
            };
            await LoadTestContentAsync(this.inPlaceEditableLabel).ConfigureAwait(true);

            this.vsm = new TestVisualStateManager();
            var vsmTarget = this.inPlaceEditableLabel.FindDescendant<Grid>(e =>
                string.Equals(e.Name, "PartRootGrid", StringComparison.Ordinal));
            _ = vsmTarget.Should().NotBeNull();
            VisualStateManager.SetCustomVisualStateManager(vsmTarget, this.vsm);

            taskCompletionSource.SetResult();
        });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }
}
