// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Tests;

/// <summary>
///     The primary action, cancellation and ordering contracts of <see cref="DialogService"/>: cancellation closes a
///     dialog that is still open, after its running action returns, unless the action accepted it; each window shows
///     its dialogs one at a time, independently of other windows.
/// </summary>
public sealed partial class DialogServiceTests
{
    [TestMethod]
    public Task ShowAsync_FailedPrimaryActionKeepsDialogOpenForRetry_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var firstSave = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var attempt = 0;
        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Unsaved changes", "A dirty document")
            {
                PrimaryButtonText = "Save",
                SecondaryButtonText = "Discard",
                CloseButtonText = "Cancel",
                PrimaryAction = _ => ++attempt == 1 ? firstSave.Task : Task.FromResult(true),
            },
            window.Id,
            this.TestContext.CancellationToken);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        InvokeButton(await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Save", OpenTimeout).ConfigureAwait(true));
        _ = dialog.IsPrimaryButtonEnabled.Should().BeFalse();
        _ = dialog.IsSecondaryButtonEnabled.Should().BeFalse();
        dialog.Hide();
        _ = showTask.IsCompleted.Should().BeFalse("the user cannot dismiss the dialog while its action runs");
        firstSave.SetResult(false);

        var save = await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Save", OpenTimeout).ConfigureAwait(true);
        _ = showTask.IsCompleted.Should().BeFalse();
        InvokeButton(save);
        _ = (await showTask.WaitAsync(CloseTimeout, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Be(DialogButton.Primary);
        _ = attempt.Should().Be(2);
    });

    [TestMethod]
    public Task ShowAsync_CancelledWhileOpen_ClosesAndThrows_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        var showTask = new DialogService(windowManager).ShowAsync(new DialogSpec("Open", "Body"), window.Id, cancellation.Token);
        _ = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);

        await cancellation.CancelAsync().ConfigureAwait(true);

        _ = await FluentActions.Awaiting(() => showTask.WaitAsync(CloseTimeout, this.TestContext.CancellationToken))
            .Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(true);
        await this.WaitForNoDialogsAsync(root, OpenTimeout).ConfigureAwait(true);
    });

    [TestMethod]
    [DataRow(false, DisplayName = "an action that stops on cancellation")]
    [DataRow(true, DisplayName = "an action that ignores cancellation and keeps the dialog open")]
    public Task ShowAsync_CancelledDuringPrimaryAction_ActionSeesTheTokenAndTheDialogCloses_Async(bool ignoresCancellation) => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        var release = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Import", "Body")
            {
                PrimaryButtonText = "Import",
                PrimaryAction = token => ignoresCancellation ? release.Task : release.Task.WaitAsync(token),
            },
            window.Id,
            cancellation.Token);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        InvokeButton(await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Import", OpenTimeout).ConfigureAwait(true));

        await cancellation.CancelAsync().ConfigureAwait(true);
        if (ignoresCancellation)
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = showTask.IsCompleted.Should().BeFalse("the dialog waits for its running action");
            release.SetResult(false);
        }

        _ = await FluentActions.Awaiting(() => showTask.WaitAsync(CloseTimeout, this.TestContext.CancellationToken))
            .Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(true);
        await this.WaitForNoDialogsAsync(root, OpenTimeout).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ShowAsync_CancelledDuringAnActionThatAccepts_ReportsPrimary_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        var release = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var showTask = new DialogService(windowManager).ShowAsync(
            new DialogSpec("Save", "Body") { PrimaryButtonText = "Save", PrimaryAction = _ => release.Task },
            window.Id,
            cancellation.Token);
        var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
        InvokeButton(await this.WaitForDialogButtonAsync(dialog, DialogButton.Primary, "Save", OpenTimeout).ConfigureAwait(true));

        await cancellation.CancelAsync().ConfigureAwait(true);
        release.SetResult(true);

        _ = (await showTask.WaitAsync(CloseTimeout, this.TestContext.CancellationToken).ConfigureAwait(true))
            .Should().Be(DialogButton.Primary, "an action that completed and accepted is never reported as cancelled");
    });

    [TestMethod]
    public Task ShowAsync_DialogsOfOneWindowWait_DialogsOfAnotherWindowDoNot_Async() => EnqueueAsync(async () =>
    {
        using var windowManager = this.CreateService();
        var (root, window) = await PrepareOwnerWindowAsync(windowManager).ConfigureAwait(true);
        var (other, otherRoot, otherManaged) = await OpenSecondOwnerWindowAsync(windowManager).ConfigureAwait(true);
        try
        {
            var service = new DialogService(windowManager);
            var first = service.ShowAsync(new DialogSpec("First", "Body"), window.Id, this.TestContext.CancellationToken);
            var dialog = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
            var queued = service.ShowAsync(new DialogSpec("Queued", "Body"), window.Id, this.TestContext.CancellationToken);
            var elsewhere = service.ShowAsync(new DialogSpec("Elsewhere", "Body"), otherManaged.Id, this.TestContext.CancellationToken);

            var otherDialog = await this.WaitForSingleDialogAsync(otherRoot, OpenTimeout).ConfigureAwait(true);
            _ = otherDialog.Title.Should().Be("Elsewhere", "a dialog in one window does not block another window");
            _ = GetOpenDialogs(root).Should().ContainSingle().Which.Title.Should().Be("First", "a window shows one dialog at a time");

            await this.CloseAsync(dialog, first).ConfigureAwait(true);
            var next = await this.WaitForSingleDialogAsync(root, OpenTimeout).ConfigureAwait(true);
            _ = next.Title.Should().Be("Queued");
            await this.CloseAsync(next, queued).ConfigureAwait(true);
            await this.CloseAsync(otherDialog, elsewhere).ConfigureAwait(true);
        }
        finally
        {
            other.Close();
        }
    });

    private static async Task<(Window Window, XamlRoot Root, IManagedWindow Managed)> OpenSecondOwnerWindowAsync(WindowManagerService windowManager)
    {
        var window = MakeSmallWindow("Second dialog owner");
        var content = new Grid();
        var loaded = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        content.Loaded += (_, _) => _ = loaded.TrySetResult();
        window.Content = content;
        window.Activate();
        await loaded.Task.WaitAsync(OpenTimeout).ConfigureAwait(true);
        var managed = await windowManager.RegisterWindowAsync(window).ConfigureAwait(true);
        return (window, content.XamlRoot!, managed);
    }
}
