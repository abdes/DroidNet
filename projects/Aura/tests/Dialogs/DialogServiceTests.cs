// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Mvvm;
using DroidNet.Tests;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
///     UI tests for <see cref="DialogService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public sealed partial class DialogServiceTests : WindowManagerServiceTestsBase
{
    private static readonly TimeSpan OpenTimeout = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan CloseTimeout = TimeSpan.FromSeconds(5);

    [TestMethod]
    [DataRow(DialogButton.None, ContentDialogButton.None)]
    [DataRow(DialogButton.Primary, ContentDialogButton.Primary)]
    [DataRow(DialogButton.Secondary, ContentDialogButton.Secondary)]
    [DataRow(DialogButton.Close, ContentDialogButton.Close)]
    public void MapDefaultButton_MapsEachButtonToTheSameRole(DialogButton button, ContentDialogButton expected)
        => _ = DialogService.MapDefaultButton(button).Should().Be(expected, "a dialog without a default button has none");

    [TestMethod]
    public async Task MessageAndConfirm_ExplicitMissingOwner_ThrowWithoutUsingTheActiveWindowAsync()
    {
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.ActiveWindow).Returns(Mock.Of<IManagedWindow>());
        var service = new DialogService(windows.Object);

        Func<Task> message = () => service.ShowMessageAsync("Title", "Message", new WindowId(42), this.TestContext.CancellationToken);
        Func<Task> confirm = () => service.ConfirmAsync("Title", "Message", new WindowId(42), this.TestContext.CancellationToken);

        _ = await message.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(false);
        _ = await confirm.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(false);
        windows.VerifyGet(value => value.ActiveWindow, Times.Never());
    }

    [TestMethod]
    public Task ConfirmAsync_WithOwnerWindowId_ShowsInThatWindow_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);

        var confirm = new DialogService(windowManager).ConfirmAsync("Delete", "Delete the asset?", window.Id, this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        InvokeButton(await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "OK", OpenTimeout).ConfigureAwait(true));

        _ = (await confirm.WaitAsync(CloseTimeout, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().BeTrue();
    });

    [TestMethod]
    public Task ShowMessageAsync_WithOwnerWindowId_ShowsInThatWindow_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);

        var message = new DialogService(windowManager).ShowMessageAsync("Import", "Done.", window.Id, this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        InvokeButton(await this.WaitForDialogButtonAsync(dialog, DialogButton.Close, "OK", OpenTimeout).ConfigureAwait(true));

        await message.WaitAsync(CloseTimeout, this.TestContext.CancellationToken).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowMessageAsync_WhenNoActiveWindow_Throws_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var dialogService = new DialogService(windowManager);

        Func<Task> act = () => dialogService.ShowMessageAsync("Title", "Message", this.TestContext.CancellationToken);

        _ = await act.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_WithOwnerWindowId_ShowsDialogAndReturnsPrimary_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);

        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Confirm", "Body")
            {
                PrimaryButtonText = "Yes",
                CloseButtonText = "No",
                DefaultButton = DialogButton.Primary,
            },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        _ = dialog.Should().BeOfType<AuraDialog>();
        _ = dialog.Title.Should().Be("Confirm");
        InvokeButton(await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Yes", OpenTimeout).ConfigureAwait(true));

        _ = (await showTask.WaitAsync(CloseTimeout, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Be(DialogButton.Primary);
        await this.WaitForNoDialogsAsync(root, OpenTimeout).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_WithMaxWidth_RaisesTheDialogWidthCap_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);

        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Wide", "Body") { MaxWidth = 760 },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);

        _ = dialog.Resources["ContentDialogMaxWidth"].Should().Be(760.0);
        await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_ViewModelWithoutLocatorOrConverter_Throws_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        _ = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);

        Func<Task> act = () => new DialogService(windowManager).ShowAsync(
            new DialogSpec("VM Dialog", Content: null) { ViewModel = new object() },
            this.TestContext.CancellationToken);

        _ = await act.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_ViewModelWithoutLocator_UsesTheApplicationConverter_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var viewModel = new object();
        Application.Current.Resources["VmToViewConverter"] = new TestVmToViewConverter();
        try
        {
            var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
            var showTask = new DialogService(windowManager).ShowAsync(
                new DialogSpec("VM Dialog", Content: null) { ViewModel = viewModel },
                window.Id,
                this.TestContext.CancellationToken);
            var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);

            var content = dialog.Content.Should().BeOfType<TextBlock>().Which;
            _ = content.Text.Should().Be("Hello from converter");
            _ = content.DataContext.Should().BeSameAs(viewModel);
            await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
        }
        finally
        {
            _ = Application.Current.Resources.Remove("VmToViewConverter");
        }
    });

    [TestMethod]
    public Task ShowAsync_ViewModel_UsesTheInjectedViewLocator_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var viewModel = new AcceptState();
        var locator = new Mock<IViewLocator>();
        _ = locator.Setup(value => value.ResolveView(viewModel)).Returns(() => new TestView());

        var showTask = new DialogService(windowManager, locator.Object).ShowAsync(
            new DialogSpec("VM Dialog", Content: null) { ViewModel = viewModel },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);

        var view = dialog.Content.Should().BeOfType<TestView>().Which;
        _ = view.ViewModel.Should().BeSameAs(viewModel, "the view locator's views get their view model");
        _ = dialog.DataContext.Should().BeSameAs(viewModel);
        await this.CloseAsync(dialog, showTask).ConfigureAwait(true);
    });

    private static async Task<(XamlRoot ownerXamlRoot, IManagedWindow managedWindow)> PrepareOwnerWindowAsync(WindowManagerService windowManager)
    {
        ArgumentNullException.ThrowIfNull(windowManager);

        var content = new Grid
        {
            Width = 1,
            Height = 1,
        };

        await LoadTestContentAsync(content).ConfigureAwait(true);

        var testWindow = VisualUserInterfaceTestsApp.MainWindow;
        var managedWindow = await windowManager.RegisterWindowAsync(testWindow).ConfigureAwait(true);

        var xamlRoot = content.XamlRoot;
        _ = xamlRoot.Should().NotBeNull("test content must be loaded so XamlRoot is available");

        return (xamlRoot!, managedWindow);
    }

    private static void InvokeButton(Button button)
    {
        ArgumentNullException.ThrowIfNull(button);

        _ = button.Focus(FocusState.Programmatic);

        var peer = FrameworkElementAutomationPeer.FromElement(button) ?? new ButtonAutomationPeer(button);
        if (peer.GetPattern(PatternInterface.Invoke) is not IInvokeProvider invokeProvider)
        {
            throw new InvalidOperationException("Button does not support invoke automation pattern.");
        }

        invokeProvider.Invoke();
    }

    private static T Part<T>(DependencyObject root, string name)
        where T : FrameworkElement
        => root.FindDescendant<T>(element => string.Equals(element.Name, name, StringComparison.Ordinal))
            ?? throw new InvalidOperationException($"The dialog has no '{name}' part.");

    private static List<ContentDialog> GetOpenDialogs(XamlRoot xamlRoot)
    {
        var results = new List<ContentDialog>();
        foreach (var popup in VisualTreeHelper.GetOpenPopupsForXamlRoot(xamlRoot))
        {
            if (popup.Child is null)
            {
                continue;
            }

            if (popup.Child is ContentDialog rootDialog)
            {
                results.Add(rootDialog);
                continue;
            }

            var nestedDialog = popup.Child.FindDescendant<ContentDialog>();
            if (nestedDialog is not null)
            {
                results.Add(nestedDialog);
            }
        }

        return results;
    }

    private static Button? FindDialogButton(ContentDialog dialog, DialogButton button, string expectedText)
    {
        ArgumentNullException.ThrowIfNull(dialog);
        ArgumentException.ThrowIfNullOrWhiteSpace(expectedText);

        var name = button switch
        {
            DialogButton.Primary => "PrimaryButton",
            DialogButton.Secondary => "SecondaryButton",
            DialogButton.Close => "CloseButton",
            _ => string.Empty,
        };

        if (!string.IsNullOrEmpty(name))
        {
            var byName = dialog.FindDescendant<Button>(b => string.Equals(b.Name, name, StringComparison.Ordinal));
            if (byName is not null)
            {
                return byName;
            }
        }

        var byContent = dialog.FindDescendant<Button>(b => string.Equals(b.Content?.ToString(), expectedText, StringComparison.Ordinal));
        return byContent;
    }

    private static string DescribeDialogButtons(ContentDialog dialog)
    {
        ArgumentNullException.ThrowIfNull(dialog);

        var primary = dialog.FindDescendant<Button>(b => string.Equals(b.Name, "PrimaryButton", StringComparison.Ordinal));
        var secondary = dialog.FindDescendant<Button>(b => string.Equals(b.Name, "SecondaryButton", StringComparison.Ordinal));
        var close = dialog.FindDescendant<Button>(b => string.Equals(b.Name, "CloseButton", StringComparison.Ordinal));

        return "Dialog buttons: " + string.Join(
            " | ",
            new[]
            {
                Describe(primary),
                Describe(secondary),
                Describe(close),
            }.Where(s => !string.IsNullOrEmpty(s)));

        static string Describe(Button? b)
            => b is null
                ? string.Empty
                : $"Name='{b.Name}', Content='{b.Content}', IsEnabled={b.IsEnabled}, IsLoaded={b.IsLoaded}";
    }

    private async Task CloseAsync(ContentDialog dialog, Task showTask)
    {
        dialog.Hide();
        await showTask.WaitAsync(CloseTimeout, this.TestContext.CancellationToken).ConfigureAwait(true);
    }

    private async Task<Button> WaitForDialogButtonAsync(ContentDialog dialog, DialogButton button, string expectedText, TimeSpan timeout)
    {
        ArgumentNullException.ThrowIfNull(dialog);
        ArgumentException.ThrowIfNullOrWhiteSpace(expectedText);

        var sw = Stopwatch.StartNew();
        while (sw.Elapsed < timeout)
        {
            await WaitForRenderAsync().ConfigureAwait(true);

            var found = FindDialogButton(dialog, button, expectedText);
            if (found?.IsLoaded == true && found.IsEnabled)
            {
                return found;
            }

            await Task.Delay(20, this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        throw new TimeoutException(DescribeDialogButtons(dialog));
    }

    private async Task<ContentDialog> WaitForSingleDialogAsync(XamlRoot xamlRoot, TimeSpan timeout)
    {
        ArgumentNullException.ThrowIfNull(xamlRoot);

        var sw = Stopwatch.StartNew();
        while (sw.Elapsed < timeout)
        {
            await WaitForRenderAsync().ConfigureAwait(true);

            var dialogs = GetOpenDialogs(xamlRoot);
            if (dialogs.Count >= 1)
            {
                return dialogs[0];
            }

            await Task.Delay(20, this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        throw new TimeoutException("Timed out waiting for a ContentDialog to open.");
    }

    private async Task WaitForNoDialogsAsync(XamlRoot xamlRoot, TimeSpan timeout)
    {
        ArgumentNullException.ThrowIfNull(xamlRoot);

        var sw = Stopwatch.StartNew();
        while (sw.Elapsed < timeout)
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            if (GetOpenDialogs(xamlRoot).Count == 0)
            {
                return;
            }

            await Task.Delay(20, this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        throw new TimeoutException("Timed out waiting for ContentDialogs to close.");
    }

    private sealed partial class TestVmToViewConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, string language)
        {
            _ = value;
            _ = targetType;
            _ = parameter;
            _ = language;

            return new TextBlock
            {
                Text = "Hello from converter",
            };
        }

        public object ConvertBack(object value, Type targetType, object parameter, string language)
            => throw new NotSupportedException();
    }

    private sealed partial class TestView : UserControl, IViewFor
    {
        public object? ViewModel { get; set; }
    }
}
