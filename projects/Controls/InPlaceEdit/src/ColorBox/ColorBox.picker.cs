// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>Turns picker input into one edit session per pick.</summary>
public partial class ColorBox
{
    // The picker's parts handle their own input, so the box listens to handled events too.
    private void AttachPickerGestures(ColorPicker target)
    {
        target.AddHandler(PointerPressedEvent, new PointerEventHandler(this.OnPickerPointerPressed), handledEventsToo: true);
        target.AddHandler(PointerReleasedEvent, new PointerEventHandler(this.OnPickerPointerReleased), handledEventsToo: true);
        target.AddHandler(PointerCaptureLostEvent, new PointerEventHandler(this.OnPickerPointerCaptureLost), handledEventsToo: true);
        target.AddHandler(PointerCanceledEvent, new PointerEventHandler(this.OnPickerPointerCanceled), handledEventsToo: true);
        target.AddHandler(KeyDownEvent, new KeyEventHandler(this.OnPickerKeyDown), handledEventsToo: true);
        target.LostFocus += this.OnPickerLostFocus;
    }

    private void OnPickerPointerPressed(object sender, PointerRoutedEventArgs args)
        => this.BeginPick(NumberBoxEditInteractionKind.PointerDrag);

    private void OnPickerPointerReleased(object sender, PointerRoutedEventArgs args)
        => this.EndPick(NumberBoxEditCompletionKind.Commit);

    // The spectrum releases capture inside its PointerReleased handler, before that handled event reaches the
    // picker. A released pointer commits; losing capture while still pressed cancels the pick.
    private void OnPickerPointerCaptureLost(object sender, PointerRoutedEventArgs args)
        => this.EndPick(args.Pointer.IsInContact ? NumberBoxEditCompletionKind.Cancel : NumberBoxEditCompletionKind.Commit);

    private void OnPickerPointerCanceled(object sender, PointerRoutedEventArgs args)
        => this.EndPick(NumberBoxEditCompletionKind.Cancel);

    private void OnPickerLostFocus(object sender, RoutedEventArgs args)
        => this.EndPick(NumberBoxEditCompletionKind.Commit);

    private void OnPickerKeyDown(object sender, KeyRoutedEventArgs args)
    {
        switch (args.Key)
        {
            case VirtualKey.Escape when this.pick is not null:
                // Escape first cancels the pick and keeps the picker open on the restored color; a second one closes it.
                this.EndPick(NumberBoxEditCompletionKind.Cancel);
                args.Handled = true;
                break;
            case VirtualKey.Escape:
                break;
            case VirtualKey.Enter:
                this.EndPick(NumberBoxEditCompletionKind.Commit);
                break;
            default:
                this.BeginPick(NumberBoxEditInteractionKind.Text);
                break;
        }
    }
}
