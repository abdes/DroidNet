// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Mvvm;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
///     The button bar of a dialog: footer commands on its left, and a chevron of the primary button that opens its
///     options and stays enabled while the primary button is not.
/// </summary>
public sealed partial class DialogServiceTests
{
    [TestMethod]
    public Task ShowAsync_FooterCommands_ComeFirstOnTheLeftAndExecute_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var opened = 0;
        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Import model", "Body")
            {
                PrimaryButtonText = "Import",
                CloseButtonText = "Cancel",
                FooterCommands = [new DialogCommand("Open session…", new RelayCommand(() => opened++)), new DialogCommand("Save session…", new RelayCommand(() => { }))],
            },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        _ = await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Import", OpenTimeout).ConfigureAwait(true);

        var buttonBar = Part<Grid>(dialog, "ButtonBar");
        var open = dialog.FindDescendant<Button>(button => Equals(button.Content, "Open session…"))!;
        _ = FocusManager.FindFirstFocusableElement(buttonBar).Should().BeSameAs(open, "the footer comes first in reading and tab order");
        _ = FocusManager.FindLastFocusableElement(buttonBar).Should().BeSameAs(Part<Button>(dialog, "CloseButton"));
        _ = Left(open, dialog).Should().BeLessThan(Left(Part<Grid>(dialog, "CommandSpace"), dialog));
        InvokeButton(open);
        _ = opened.Should().Be(1);

        await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_WideFooterAndThreeButtons_DoNotOverlap_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Unsaved changes", "Body")
            {
                PrimaryButtonText = "Save",
                SecondaryButtonText = "Don't Save",
                CloseButtonText = "Cancel",
                FooterCommands = [.. Enumerable.Range(0, 6).Select(index => new DialogCommand($"Footer command {index}", new RelayCommand(() => { })))],
            },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        _ = await this.WaitForDialogButtonAsync(dialog, DialogButton.Close, "Cancel", OpenTimeout).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var footer = Part<ContentPresenter>(dialog, AuraDialog.FooterPresenterPartName);
        var commands = Part<Grid>(dialog, "CommandSpace");
        _ = (Left(footer, dialog) + footer.ActualWidth).Should().BeLessThanOrEqualTo(Left(commands, dialog), "the footer takes only the width the buttons leave");
        foreach (var name in new[] { "PrimaryButton", "SecondaryButton", "CloseButton" })
        {
            var button = Part<Button>(dialog, name);
            _ = button.ActualWidth.Should().BeGreaterThanOrEqualTo(button.DesiredSize.Width - 1, $"{name} keeps its width");
        }

        await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_PrimaryButtonOptions_ChevronStaysEnabledWhileThePrimaryIsNot_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var state = new AcceptState();
        var options = new TextBlock { Text = "Import run" };
        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Import model", "Body")
            {
                PrimaryButtonText = "Import",
                CloseButtonText = "Cancel",
                DefaultButton = DialogButton.Primary,
                ViewModel = state,
                IsPrimaryButtonEnabled = () => state.CanAccept,
                PrimaryButtonOptions = options,
            },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        _ = await this.WaitForDialogButtonAsync(dialog, DialogButton.Close, "Cancel", OpenTimeout).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var primary = Part<Button>(dialog, "PrimaryButton");
        var chevron = Part<Button>(dialog, AuraDialog.PrimaryButtonFlyoutButtonPartName);
        _ = chevron.Visibility.Should().Be(Visibility.Visible);
        _ = chevron.Flyout.Should().BeOfType<Flyout>().Which.Content.Should().BeSameAs(options);
        _ = primary.IsEnabled.Should().BeFalse();
        _ = chevron.IsEnabled.Should().BeTrue("the run's options can change while the import cannot start");

        state.CanAccept = true;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = primary.IsEnabled.Should().BeTrue("the predicate is evaluated again when the view model changes");

        await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_PrimaryButtonOptions_UseTheThemeCornersAndKeepThePrimaryMargin_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Import model", "Body") { PrimaryButtonText = "Import", PrimaryButtonOptions = new TextBlock() },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        var primary = await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Import", OpenTimeout).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var theme = (CornerRadius)Application.Current.Resources["ControlCornerRadius"];
        var chevron = Part<Button>(dialog, AuraDialog.PrimaryButtonFlyoutButtonPartName);
        _ = primary.Margin.Should().Be(default(Thickness), "the chevron has a column of its own");
        _ = primary.CornerRadius.Should().Be(new CornerRadius(theme.TopLeft, 0, 0, theme.BottomLeft));
        _ = chevron.CornerRadius.Should().Be(new CornerRadius(0, theme.TopRight, theme.BottomRight, 0));
        _ = Left(chevron, dialog).Should().BeGreaterThanOrEqualTo(Left(primary, dialog) + primary.ActualWidth);

        await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_ModifiedPrimaryButtonOptions_ShowADotThatFollowsTheOptions_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var viewModel = new AcceptState();
        var options = new AcceptState();
        var locator = new Mock<IViewLocator>();
        _ = locator.Setup(value => value.ResolveView(It.IsAny<object>())).Returns(() => new TestView());
        var showTask = new DialogService(windowManager, locator.Object).ShowAsync(
            new DialogSpec("Import model", "Body")
            {
                PrimaryButtonText = "Import",
                ViewModel = viewModel,
                PrimaryButtonOptions = options,
                HasModifiedPrimaryButtonOptions = () => options.IsModified,
            },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = (AuraDialog)await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        _ = await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Import", OpenTimeout).ConfigureAwait(true);

        var flyout = Part<Button>(dialog, AuraDialog.PrimaryButtonFlyoutButtonPartName).Flyout.Should().BeOfType<Flyout>().Which;
        _ = flyout.Content.Should().BeOfType<TestView>().Which.ViewModel.Should().BeSameAs(options, "a view model's view is resolved");
        _ = dialog.IsPrimaryButtonFlyoutIndicatorVisible.Should().BeFalse();
        options.IsModified = true;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = dialog.IsPrimaryButtonFlyoutIndicatorVisible.Should().BeTrue("the options view model changed");
        _ = Part<FrameworkElement>(dialog, "PrimaryButtonFlyoutIndicator").Visibility.Should().Be(Visibility.Visible);

        await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
        _ = viewModel.SubscriberCount.Should().Be(0, "the dialog stops observing its view model when it closes");
        _ = options.SubscriberCount.Should().Be(0, "the dialog stops observing its options when it closes");
    });

    [TestMethod]
    public Task ShowAsync_PrimaryActionThatKeepsTheDialogOpen_KeepsThePredicateInCharge_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var state = new AcceptState { CanAccept = true };
        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Import model", "Body")
            {
                PrimaryButtonText = "Import",
                CloseButtonText = "Cancel",
                ViewModel = state,
                PrimaryAction = _ => Task.FromResult(false),
                IsPrimaryButtonEnabled = () => state.CanAccept,
            },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        InvokeButton(await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Import", OpenTimeout).ConfigureAwait(true));
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = dialog.IsPrimaryButtonEnabled.Should().BeTrue();

        state.CanAccept = false;
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = dialog.IsPrimaryButtonEnabled.Should().BeFalse("the predicate still drives the primary button after its action");

        await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
    });

    private static double Left(UIElement element, UIElement relativeTo)
        => element.TransformToVisual(relativeTo).TransformPoint(default).X;

    private sealed partial class AcceptState : INotifyPropertyChanged
    {
        private PropertyChangedEventHandler? propertyChanged;
        private bool canAccept;
        private bool isModified;

        public event PropertyChangedEventHandler? PropertyChanged
        {
            add => this.propertyChanged += value;
            remove => this.propertyChanged -= value;
        }

        public int SubscriberCount => this.propertyChanged?.GetInvocationList().Length ?? 0;

        public bool CanAccept
        {
            get => this.canAccept;
            set
            {
                this.canAccept = value;
                this.propertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.CanAccept)));
            }
        }

        public bool IsModified
        {
            get => this.isModified;
            set
            {
                this.isModified = value;
                this.propertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.IsModified)));
            }
        }
    }
}
