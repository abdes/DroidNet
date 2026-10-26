// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Runtime.CompilerServices;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Windowing;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     WinUI 3 implementation of <see cref="IDialogService"/>.
/// </summary>
/// <remarks>
///     Dialogs are <see cref="AuraDialog"/> instances. A window shows one dialog at a time, so each window's dialogs
///     wait for each other, in request order, while dialogs of other windows are independent. View models become views
///     through the injected <see cref="IViewLocator"/>; without one, through the application resource
///     <c>VmToViewConverter</c>.
/// </remarks>
public sealed partial class DialogService : IDialogService
{
    private const string ApplicationViewConverterKey = "VmToViewConverter";
    private const string ButtonSpacingResourceKey = "ContentDialogButtonSpacing";

    // ContentDialog allows one open dialog per XamlRoot, whichever service instance shows it.
    private static readonly ConditionalWeakTable<XamlRoot, SemaphoreSlim> DialogGates = [];

    private readonly IWindowManagerService windowManager;
    private readonly IValueConverter? viewConverter;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DialogService"/> class.
    /// </summary>
    /// <param name="windowManager">The Aura window manager service.</param>
    /// <param name="viewLocator">
    ///     The view locator that resolves the views of dialog view models; <see langword="null"/> to use the application
    ///     resource <c>VmToViewConverter</c>.
    /// </param>
    public DialogService(IWindowManagerService windowManager, IViewLocator? viewLocator = null)
    {
        ArgumentNullException.ThrowIfNull(windowManager);
        this.windowManager = windowManager;
        this.viewConverter = viewLocator is null ? null : new ViewModelToView(viewLocator);
    }

    /// <inheritdoc/>
    public Task<DialogButton> ShowAsync(DialogSpec dialog, CancellationToken cancellationToken = default)
        => this.ShowAsync(dialog, ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task<DialogButton> ShowAsync(DialogSpec dialog, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.ShowAsync(dialog, (WindowId?)ownerWindowId, cancellationToken);

    /// <inheritdoc/>
    public Task ShowMessageAsync(string title, string message, CancellationToken cancellationToken = default)
        => this.ShowAsync(CreateMessage(title, message), ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task ShowMessageAsync(string title, string message, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.ShowAsync(CreateMessage(title, message), ownerWindowId, cancellationToken);

    /// <inheritdoc/>
    public Task<bool> ConfirmAsync(string title, string message, CancellationToken cancellationToken = default)
        => this.ConfirmAsync(title, message, ownerWindowId: null, cancellationToken);

    /// <inheritdoc/>
    public Task<bool> ConfirmAsync(string title, string message, WindowId ownerWindowId, CancellationToken cancellationToken = default)
        => this.ConfirmAsync(title, message, (WindowId?)ownerWindowId, cancellationToken);

    /// <summary>
    ///     Maps a dialog's default button to the <see cref="ContentDialog"/> one.
    /// </summary>
    /// <param name="button">The default button of a <see cref="DialogSpec"/>.</param>
    /// <returns>The <see cref="ContentDialogButton"/> with the same role.</returns>
    internal static ContentDialogButton MapDefaultButton(DialogButton button)
        => button switch
        {
            DialogButton.Primary => ContentDialogButton.Primary,
            DialogButton.Secondary => ContentDialogButton.Secondary,
            DialogButton.Close => ContentDialogButton.Close,
            DialogButton.None => ContentDialogButton.None,
            _ => throw new ArgumentOutOfRangeException(nameof(button), button, message: null),
        };

    private static DialogSpec CreateMessage(string title, string message)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(title);
        ArgumentNullException.ThrowIfNull(message);
        return new DialogSpec(title, message)
        {
            CloseButtonText = "OK",
            DefaultButton = DialogButton.Close,
        };
    }

    private static void Validate(DialogSpec dialog)
    {
        ArgumentNullException.ThrowIfNull(dialog);
        if (string.IsNullOrWhiteSpace(dialog.Title))
        {
            throw new ArgumentException("Dialog title cannot be null or whitespace.", nameof(dialog));
        }

        if (dialog.FooterCommands is null)
        {
            throw new ArgumentException("Dialog footer commands cannot be null.", nameof(dialog));
        }

        foreach (var command in dialog.FooterCommands)
        {
            if (command is null || string.IsNullOrWhiteSpace(command.Label) || command.Command is null)
            {
                throw new ArgumentException("Each footer command needs a label and a command.", nameof(dialog));
            }
        }
    }

    private static XamlRoot GetXamlRoot(IManagedWindow owner)
    {
        if (owner.Window.Content is not UIElement rootElement)
        {
            throw new DialogServiceException("The owner window has no root XAML content.");
        }

        return rootElement.XamlRoot ?? throw new DialogServiceException("The owner window root content has no XamlRoot.");
    }

    private static StackPanel? CreateFooter(IReadOnlyList<DialogCommand> commands)
    {
        if (commands.Count == 0)
        {
            return null;
        }

        var footer = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = Application.Current.Resources.TryGetValue(ButtonSpacingResourceKey, out var spacing) && spacing is GridLength length
                ? length.Value
                : 0,
        };
        foreach (var command in commands)
        {
            footer.Children.Add(new Button { Content = command.Label, Command = command.Command });
        }

        return footer;
    }

    private static IValueConverter GetApplicationViewConverter()
        => Application.Current.Resources.TryGetValue(ApplicationViewConverterKey, out var resource) && resource is IValueConverter converter
            ? converter
            : throw new DialogServiceException(
                $"No view locator was injected, and the application resource '{ApplicationViewConverterKey}' is not a value converter.");

    private async Task<bool> ConfirmAsync(string title, string message, WindowId? ownerWindowId, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(title);
        ArgumentNullException.ThrowIfNull(message);
        var confirmation = new DialogSpec(title, message)
        {
            PrimaryButtonText = "OK",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Close,
        };
        return await this.ShowAsync(confirmation, ownerWindowId, cancellationToken).ConfigureAwait(false) == DialogButton.Primary;
    }

    private async Task<DialogButton> ShowAsync(DialogSpec dialog, WindowId? ownerWindowId, CancellationToken cancellationToken)
    {
        Validate(dialog);
        cancellationToken.ThrowIfCancellationRequested();
        var owner = this.ResolveOwnerWindow(ownerWindowId);
        return await owner.DispatcherQueue
            .EnqueueAsync(() => this.ShowOnOwnerThreadAsync(owner, dialog, cancellationToken))
            .ConfigureAwait(false);
    }

    private async Task<DialogButton> ShowOnOwnerThreadAsync(IManagedWindow owner, DialogSpec spec, CancellationToken cancellationToken)
    {
        var xamlRoot = GetXamlRoot(owner);
        var gate = DialogGates.GetValue(xamlRoot, static _ => new SemaphoreSlim(1, 1));
        await gate.WaitAsync(cancellationToken).ConfigureAwait(true);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var session = new DialogSession(this.CreateDialog(xamlRoot, spec), spec, owner.DispatcherQueue, cancellationToken);
            return await session.RunAsync().ConfigureAwait(true);
        }
        finally
        {
            _ = gate.Release();
        }
    }

    private AuraDialog CreateDialog(XamlRoot xamlRoot, DialogSpec spec)
    {
        var dialog = new AuraDialog
        {
            XamlRoot = xamlRoot,
            Title = spec.Title,
            Content = spec.Content ?? (spec.ViewModel is { } viewModel ? this.ResolveView(viewModel) : null),
            PrimaryButtonText = spec.PrimaryButtonText,
            SecondaryButtonText = spec.SecondaryButtonText,
            CloseButtonText = spec.CloseButtonText,
            DefaultButton = MapDefaultButton(spec.DefaultButton),
            FooterContent = CreateFooter(spec.FooterCommands),
            PrimaryButtonFlyout = spec.PrimaryButtonOptions is { } options ? this.CreateOptionsFlyout(options) : null,
        };
        if (spec.ViewModel is { } dataContext)
        {
            dialog.DataContext = dataContext;
        }

        // ContentDialog reads its size caps from these theme resources when its template applies.
        if (spec.MaxWidth is { } maxWidth && double.IsFinite(maxWidth) && maxWidth > 0)
        {
            dialog.Resources["ContentDialogMaxWidth"] = maxWidth;
        }

        if (spec.MaxHeight is { } maxHeight && double.IsFinite(maxHeight) && maxHeight > 0)
        {
            dialog.Resources["ContentDialogMaxHeight"] = maxHeight;
        }

        return dialog;
    }

    private Flyout CreateOptionsFlyout(object options)
        => new()
        {
            Content = options as UIElement ?? this.ResolveView(options),
            Placement = FlyoutPlacementMode.TopEdgeAlignedRight,
        };

    private UIElement ResolveView(object viewModel)
    {
        var converter = this.viewConverter ?? GetApplicationViewConverter();
        if (converter.Convert(viewModel, typeof(object), parameter: null, CultureInfo.CurrentUICulture.Name) is not UIElement view)
        {
            throw new DialogServiceException($"No view was resolved for the view model '{viewModel.GetType().FullName}'.");
        }

        if (view is FrameworkElement { DataContext: null } element)
        {
            element.DataContext = viewModel;
        }

        return view;
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
