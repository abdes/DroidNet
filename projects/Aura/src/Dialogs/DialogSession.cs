// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.ExceptionServices;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Shows one <see cref="AuraDialog"/> for a <see cref="DialogSpec"/>: runs its primary action, keeps its button
///     states current, and turns the way it closed into a result.
/// </summary>
/// <remarks>
///     Cancellation closes a dialog that is still open, once its running primary action returns, and is reported as
///     <see cref="OperationCanceledException"/>. A dialog that closed with a button before the cancellation, or whose
///     primary action returned <see langword="true"/>, reports that button. The session subscribes to the dialog and to
///     the spec's view models only while the dialog is shown.
/// </remarks>
/// <param name="dialog">The dialog created for <paramref name="spec"/>.</param>
/// <param name="spec">The dialog's specification.</param>
/// <param name="dispatcher">The dispatcher of the dialog's window.</param>
/// <param name="cancellationToken">The token that cancels the dialog.</param>
internal sealed class DialogSession(AuraDialog dialog, DialogSpec spec, DispatcherQueue dispatcher, CancellationToken cancellationToken)
{
    private readonly INotifyPropertyChanged[] observedSources = [.. new[] { spec.ViewModel, spec.PrimaryButtonOptions }.OfType<INotifyPropertyChanged>().Distinct()];

    private bool isOpen;
    private bool isBusy;
    private bool isAccepted;
    private bool isClosedByCancellation;
    private Exception? actionFailure;

    /// <summary>
    ///     Shows the dialog on the dispatcher's thread and waits until it closes.
    /// </summary>
    /// <returns>The button that dismissed the dialog.</returns>
    /// <exception cref="OperationCanceledException">The token cancelled the dialog.</exception>
    public async Task<DialogButton> RunAsync()
    {
        ContentDialogResult result;
        this.Attach();
        try
        {
            var registration = cancellationToken.Register(() => _ = dispatcher.TryEnqueue(this.OnCancellationRequested));
            await using (registration.ConfigureAwait(true))
            {
                this.isOpen = true;
                this.Refresh();
                result = await dialog.ShowAsync();
            }
        }
        finally
        {
            this.isOpen = false;
            this.Detach();
        }

        if (this.actionFailure is { } failure)
        {
            ExceptionDispatchInfo.Throw(failure);
        }

        return this.isClosedByCancellation
            ? throw new OperationCanceledException(cancellationToken)
            : MapResult(result);
    }

    private static DialogButton MapResult(ContentDialogResult result)
        => result switch
        {
            ContentDialogResult.Primary => DialogButton.Primary,
            ContentDialogResult.Secondary => DialogButton.Secondary,
            _ => DialogButton.None,
        };

    private void Attach()
    {
        dialog.Closing += this.OnClosing;
        if (spec.PrimaryAction is not null)
        {
            dialog.PrimaryButtonClick += this.OnPrimaryButtonClick;
        }

        foreach (var source in this.observedSources)
        {
            source.PropertyChanged += this.OnSourcePropertyChanged;
        }
    }

    private void Detach()
    {
        dialog.Closing -= this.OnClosing;
        dialog.PrimaryButtonClick -= this.OnPrimaryButtonClick;
        foreach (var source in this.observedSources)
        {
            source.PropertyChanged -= this.OnSourcePropertyChanged;
        }
    }

    private void Refresh()
    {
        dialog.IsPrimaryButtonEnabled = !this.isBusy && (spec.IsPrimaryButtonEnabled?.Invoke() ?? true);
        dialog.IsSecondaryButtonEnabled = !this.isBusy;
        dialog.IsPrimaryButtonFlyoutIndicatorVisible = spec.HasModifiedPrimaryButtonOptions?.Invoke() ?? false;
    }

    private void RefreshIfOpen()
    {
        if (this.isOpen)
        {
            this.Refresh();
        }
    }

    private void OnSourcePropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (dispatcher.HasThreadAccess)
        {
            this.RefreshIfOpen();
        }
        else
        {
            _ = dispatcher.TryEnqueue(this.RefreshIfOpen);
        }
    }

    // A running primary action holds the dialog open; it has the token, and the dialog closes when it returns.
    private void OnCancellationRequested()
    {
        if (this.isOpen && !this.isBusy)
        {
            dialog.Hide();
        }
    }

    private void OnClosing(ContentDialog sender, ContentDialogClosingEventArgs args)
    {
        if (this.isBusy)
        {
            args.Cancel = true;
            return;
        }

        this.isClosedByCancellation = cancellationToken.IsCancellationRequested && !this.isAccepted && this.actionFailure is null;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Exceptions from the async-void button event are transferred to the awaited dialog task.")]
    private async void OnPrimaryButtonClick(ContentDialog sender, ContentDialogButtonClickEventArgs args)
    {
        var deferral = args.GetDeferral();
        this.isBusy = true;
        this.Refresh();
        try
        {
            this.isAccepted = await spec.PrimaryAction!(cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            this.isAccepted = false;
        }
        catch (Exception ex)
        {
            this.actionFailure = ex;
        }
        finally
        {
            this.isBusy = false;
            this.Refresh();
            args.Cancel = !this.isAccepted && this.actionFailure is null && !cancellationToken.IsCancellationRequested;
            deferral.Complete();
        }
    }
}
