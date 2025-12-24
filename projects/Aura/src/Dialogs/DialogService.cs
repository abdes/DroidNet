// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     WinUI 3 implementation of <see cref="IDialogService"/>.
/// </summary>
public sealed class DialogService : IDialogService
{
    private static readonly SemaphoreSlim DialogGate = new(1, 1);

    private readonly IWindowManagerService windowManager;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DialogService"/> class.
    /// </summary>
    /// <param name="windowManager">The Aura window manager service.</param>
    public DialogService(IWindowManagerService windowManager)
    {
        ArgumentNullException.ThrowIfNull(windowManager);
        this.windowManager = windowManager;
    }

    /// <inheritdoc/>
    public Task<DialogButton> ShowAsync(DialogSpec dialog, CancellationToken cancellationToken = default)
        => this.ShowAsync(dialog, ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task<DialogButton> ShowAsync(DialogSpec dialog, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.ShowAsync(dialog, (WindowId?)ownerWindowId, cancellationToken);

    /// <inheritdoc/>
    public Task<DialogButton> ShowAsync(ViewModelDialogSpec dialog, CancellationToken cancellationToken = default)
        => this.ShowAsync(dialog, ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task<DialogButton> ShowAsync(ViewModelDialogSpec dialog, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.ShowAsync(dialog, (WindowId?)ownerWindowId, cancellationToken);

    /// <inheritdoc/>
    public async Task ShowMessageAsync(string title, string message, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(title);
        ArgumentNullException.ThrowIfNull(message);

        _ = await this.ShowAsync(
            new DialogSpec(title, message)
            {
                CloseButtonText = "OK",
                DefaultButton = DialogButton.Close,
            },
            cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task<bool> ConfirmAsync(string title, string message, CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(title);
        ArgumentNullException.ThrowIfNull(message);

        var result = await this.ShowAsync(
            new DialogSpec(title, message)
            {
                PrimaryButtonText = "OK",
                CloseButtonText = "Cancel",
                DefaultButton = DialogButton.Close,
            },
            cancellationToken).ConfigureAwait(false);

        return result == DialogButton.Primary;
    }

    private static DialogButton MapResult(ContentDialogResult result)
        => result switch
        {
            ContentDialogResult.Primary => DialogButton.Primary,
            ContentDialogResult.Secondary => DialogButton.Secondary,
            _ => DialogButton.None,
        };

    private static ContentDialogButton MapDefaultButton(DialogButton button)
        => button switch
        {
            DialogButton.Primary => ContentDialogButton.Primary,
            DialogButton.Secondary => ContentDialogButton.Secondary,
            DialogButton.Close => ContentDialogButton.Close,
            _ => ContentDialogButton.Close,
        };

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code clarity")]
    private static ContentDialog CreateContentDialog(IManagedWindow owner, DialogSpec spec)
    {
        ArgumentNullException.ThrowIfNull(owner);
        ArgumentNullException.ThrowIfNull(spec);

        if (owner.Window.Content is not UIElement rootElement)
        {
            throw new DialogServiceException("The owner window has no root XAML content.");
        }

        if (rootElement.XamlRoot is null)
        {
            throw new DialogServiceException("The owner window root content has no XamlRoot.");
        }

        return new ContentDialog
        {
            XamlRoot = rootElement.XamlRoot,
            Title = spec.Title,
            Content = spec.Content,
            PrimaryButtonText = spec.PrimaryButtonText,
            SecondaryButtonText = spec.SecondaryButtonText,
            CloseButtonText = spec.CloseButtonText,
            DefaultButton = MapDefaultButton(spec.DefaultButton),
        };
    }

    private static UIElement ResolveViewFromViewModel(object viewModel)
    {
        ArgumentNullException.ThrowIfNull(viewModel);

        var application = Application.Current;

        if (!application.Resources.TryGetValue("VmToViewConverter", out var converterObject))
        {
            throw new DialogServiceException("Application resource 'VmToViewConverter' was not found.");
        }

        if (converterObject is not IValueConverter converter)
        {
            throw new DialogServiceException("Application resource 'VmToViewConverter' is not an IValueConverter.");
        }

        var view = converter.Convert(viewModel, typeof(object), parameter: null, CultureInfo.CurrentUICulture.Name);
        if (view is not UIElement element)
        {
            throw new DialogServiceException("VmToViewConverter did not return a UIElement.");
        }

        if (element is FrameworkElement frameworkElement && frameworkElement.DataContext is null)
        {
            frameworkElement.DataContext = viewModel;
        }

        return element;
    }

    private async Task<DialogButton> ShowAsync(DialogSpec dialog, WindowId? ownerWindowId, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(dialog);
        if (string.IsNullOrWhiteSpace(dialog.Title))
        {
            throw new ArgumentException("Dialog title cannot be null or whitespace.", nameof(dialog));
        }

        var owner = this.ResolveOwnerWindow(ownerWindowId);

        await DialogGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            return await owner.DispatcherQueue.EnqueueAsync(async () =>
            {
                cancellationToken.ThrowIfCancellationRequested();

                var contentDialog = CreateContentDialog(owner, dialog);

                var registration = cancellationToken.Register(() => owner.DispatcherQueue.TryEnqueue(contentDialog.Hide));
                await using (registration.ConfigureAwait(false))
                {
                    var result = await contentDialog.ShowAsync();

                    cancellationToken.ThrowIfCancellationRequested();
                    return MapResult(result);
                }
            }).ConfigureAwait(false);
        }
        finally
        {
            _ = DialogGate.Release();
        }
    }

    private async Task<DialogButton> ShowAsync(ViewModelDialogSpec dialog, WindowId? ownerWindowId, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(dialog);
        if (string.IsNullOrWhiteSpace(dialog.Title))
        {
            throw new ArgumentException("Dialog title cannot be null or whitespace.", nameof(dialog));
        }

        if (dialog.ViewModel is null)
        {
            throw new ArgumentException("Dialog ViewModel cannot be null.", nameof(dialog));
        }

        var owner = this.ResolveOwnerWindow(ownerWindowId);

        await DialogGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            return await owner.DispatcherQueue.EnqueueAsync(async () =>
            {
                cancellationToken.ThrowIfCancellationRequested();

                var view = ResolveViewFromViewModel(dialog.ViewModel);
                var spec = new DialogSpec(dialog.Title, view)
                {
                    PrimaryButtonText = dialog.PrimaryButtonText,
                    SecondaryButtonText = dialog.SecondaryButtonText,
                    CloseButtonText = dialog.CloseButtonText,
                    DefaultButton = dialog.DefaultButton,
                };

                var contentDialog = CreateContentDialog(owner, spec);

                var registration = cancellationToken.Register(() => owner.DispatcherQueue.TryEnqueue(contentDialog.Hide));
                await using (registration.ConfigureAwait(false))
                {
                    var result = await contentDialog.ShowAsync();

                    cancellationToken.ThrowIfCancellationRequested();
                    return MapResult(result);
                }
            }).ConfigureAwait(false);
        }
        finally
        {
            _ = DialogGate.Release();
        }
    }

    private IManagedWindow ResolveOwnerWindow(WindowId? ownerWindowId)
    {
        var owner = ownerWindowId.HasValue
            ? this.windowManager.GetWindow(ownerWindowId.Value)
            : this.windowManager.ActiveWindow;

        return owner ?? throw new DialogServiceException(
            ownerWindowId.HasValue
                ? "No Aura window with the specified id is available to own the dialog."
                : "No active Aura window is available to own the dialog.");
    }
}
