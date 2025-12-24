// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Aura.Tests;

/// <summary>
///     UI tests for <see cref="DialogService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public sealed partial class DialogServiceTests : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task ShowMessageAsync_WhenNoActiveWindow_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var windowManager = this.CreateService();
        var dialogService = new DialogService(windowManager);

        try
        {
            // Act
            Func<Task> act = () => dialogService.ShowMessageAsync("Title", "Message", this.TestContext.CancellationToken);

            // Assert
            _ = await act.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(true);
        }
        finally
        {
            windowManager.Dispose();
        }
    });

    [TestMethod]
    public Task ShowAsync_WithOwnerWindowId_ShowsDialogAndReturnsPrimary_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var windowManager = this.CreateService();

        try
        {
            var (ownerXamlRoot, managedWindow) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);

            var dialogService = new DialogService(windowManager);

            // Act
            var showTask = dialogService.ShowAsync(
                new DialogSpec("Confirm", "Body")
                {
                    PrimaryButtonText = "Yes",
                    CloseButtonText = "No",
                    DefaultButton = DialogButton.Primary,
                },
                managedWindow.Id,
                this.TestContext.CancellationToken);

            var dialog = await this.WaitForSingleDialogAsync(ownerXamlRoot, TimeSpan.FromSeconds(2)).ConfigureAwait(true);
            _ = dialog.Title.Should().Be("Confirm");

            // Assert
            var primaryButton = await this.WaitForDialogButtonAsync(
                dialog,
                DialogButton.Primary,
                expectedText: "Yes",
                timeout: TimeSpan.FromSeconds(2)).ConfigureAwait(true);
            InvokeButton(primaryButton);

            var result = await showTask.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = result.Should().Be(DialogButton.Primary);

            await this.WaitForNoDialogsAsync(ownerXamlRoot, TimeSpan.FromSeconds(2)).ConfigureAwait(true);
        }
        finally
        {
            windowManager.Dispose();
        }
    });

    [TestMethod]
    public Task ShowAsync_ViewModelDialogWithoutConverter_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var windowManager = this.CreateService();

        try
        {
            _ = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);

            var dialogService = new DialogService(windowManager);
            var spec = new ViewModelDialogSpec("VM Dialog", new object());

            // Act
            Func<Task> act = () => dialogService.ShowAsync(spec, this.TestContext.CancellationToken);

            // Assert
            _ = await act.Should().ThrowAsync<DialogServiceException>().ConfigureAwait(true);
        }
        finally
        {
            windowManager.Dispose();
        }
    });

    [TestMethod]
    public Task ShowAsync_ViewModelDialog_UsesConverterAndSetsDataContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var windowManager = this.CreateService();
        var viewModel = new object();

        try
        {
            var (ownerXamlRoot, managedWindow) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);

            Application.Current.Resources["VmToViewConverter"] = new TestVmToViewConverter();

            var dialogService = new DialogService(windowManager);

            // Act
            var showTask = dialogService.ShowAsync(
                new ViewModelDialogSpec("VM Dialog", viewModel)
                {
                    PrimaryButtonText = "OK",
                    CloseButtonText = "Cancel",
                    DefaultButton = DialogButton.Primary,
                },
                managedWindow.Id,
                this.TestContext.CancellationToken);

            var dialog = await this.WaitForSingleDialogAsync(ownerXamlRoot, TimeSpan.FromSeconds(2)).ConfigureAwait(true);

            // Assert
            _ = dialog.Content.Should().BeOfType<TextBlock>();
            var content = (TextBlock)dialog.Content;
            _ = content.Text.Should().Be("Hello from converter");
            _ = content.DataContext.Should().BeSameAs(viewModel);

            var primaryButton = await this.WaitForDialogButtonAsync(
                dialog,
                DialogButton.Primary,
                expectedText: "OK",
                timeout: TimeSpan.FromSeconds(2)).ConfigureAwait(true);
            InvokeButton(primaryButton);

            var result = await showTask.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = result.Should().Be(DialogButton.Primary);
        }
        finally
        {
            _ = Application.Current.Resources.Remove("VmToViewConverter");
            windowManager.Dispose();
        }
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
}
