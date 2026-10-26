// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.Controls.Testing;

/// <summary>
///     Finds and drives the parts of the library's property editors as a user would see them, for UI tests of the
///     controls and of the modules that show them. The part names are the templates' own; a template change is made
///     here once, not in each test.
/// </summary>
public static class PropertyEditorProbes
{
    /// <summary>Gets a field's row card, from the field's template; its feedback text sits below the card.</summary>
    /// <param name="field">A realized field, such as a <see cref="NumberField"/> or a <see cref="ColorField"/>.</param>
    /// <returns>The field's <see cref="PropertyCard"/>.</returns>
    /// <remarks>A collapsed field has not applied its template; the probe applies it, so a hidden row's card is found too.</remarks>
    public static PropertyCard CardOf(Control field)
    {
        ArgumentNullException.ThrowIfNull(field);
        _ = field.ApplyTemplate();
        return field.FindDescendant<PropertyCard>()
            ?? throw new InvalidOperationException($"The {field.GetType().Name} has no property card.");
    }

    /// <summary>Gets the swatch button of a color field's ColorBox, which opens its picker.</summary>
    /// <param name="field">A realized color field.</param>
    /// <returns>The swatch button.</returns>
    public static Button SwatchOf(ColorField field)
        => field.FindDescendant<Button>(button => string.Equals(button.Name, "PartSwatchButton", StringComparison.Ordinal))
            ?? throw new InvalidOperationException("The color field has no realized swatch button.");

    /// <summary>Gets the color a color field's swatch shows.</summary>
    /// <param name="field">A realized color field.</param>
    /// <returns>The swatch's display color.</returns>
    public static Windows.UI.Color SwatchColor(ColorField field)
    {
        var fill = field.FindDescendant<Border>(border => string.Equals(border.Name, "PartSwatchFill", StringComparison.Ordinal))
            ?? throw new InvalidOperationException("The color field has no realized swatch fill.");
        return ((SolidColorBrush)fill.Background).Color;
    }

    /// <summary>Opens a swatch's picker, picks <paramref name="color"/> and closes the picker, which commits the pick.</summary>
    /// <param name="swatch">The swatch button, from <see cref="SwatchOf"/>.</param>
    /// <param name="color">The display color to pick.</param>
    /// <returns>A task that completes after the picker closed and the next frame rendered.</returns>
    public static async Task PickDisplayColorAsync(Button swatch, Windows.UI.Color color)
    {
        ArgumentNullException.ThrowIfNull(swatch);
        var flyout = (Flyout)swatch.Flyout;
        var opened = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnOpened(object? sender, object args) => opened.TrySetResult();
        void OnClosed(object? sender, object args) => closed.TrySetResult();
        flyout.Opened += OnOpened;
        flyout.Closed += OnClosed;
        try
        {
            flyout.ShowAt(swatch);
            await opened.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
            ((ColorPicker)flyout.Content).Color = color;
            flyout.Hide();
            await closed.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
            await NextFrameAsync().ConfigureAwait(true);
        }
        finally
        {
            flyout.Opened -= OnOpened;
            flyout.Closed -= OnClosed;
            flyout.Hide();
        }
    }

    /// <summary>Begins a text edit on a NumberBox and types <paramref name="value"/> into its edit box.</summary>
    /// <param name="number">A realized NumberBox, such as a <see cref="NumberField"/>'s.</param>
    /// <param name="value">The text to type; the caller commits it, for example with <c>CompletePendingTextEdit</c>.</param>
    /// <returns>A task that completes when the edit box shows the text.</returns>
    public static async Task EnterTextAsync(NumberBox number, string value)
    {
        ArgumentNullException.ThrowIfNull(number);
        _ = number.ApplyTemplate();
        number.BeginTextEdit();
        var input = number.FindDescendant<TextBox>(element => string.Equals(element.Name, "PartEditBox", StringComparison.Ordinal))
            ?? throw new InvalidOperationException("The NumberBox has no realized edit box.");
        if (string.Equals(input.Text, value, StringComparison.Ordinal))
        {
            return;
        }

        var changed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void TextChanged(object sender, TextChangedEventArgs args)
        {
            if (string.Equals(input.Text, value, StringComparison.Ordinal))
            {
                _ = changed.TrySetResult();
            }
        }

        input.TextChanged += TextChanged;
        try
        {
            input.Text = value;
            await changed.Task.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(true);
        }
        finally
        {
            input.TextChanged -= TextChanged;
        }
    }

    // Waits until the window renders its next frame, so a value set while a flyout closed has reached the view.
    private static Task NextFrameAsync()
    {
        var rendered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        void OnRendering(object? sender, object args)
        {
            CompositionTarget.Rendering -= OnRendering;
            _ = rendered.TrySetResult();
        }

        CompositionTarget.Rendering += OnRendering;
        return rendered.Task.WaitAsync(TimeSpan.FromSeconds(10));
    }
}
