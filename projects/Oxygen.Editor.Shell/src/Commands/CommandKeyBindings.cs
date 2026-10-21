// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;
using Windows.UI.Core;

namespace Oxygen.Editor.Shell.Commands;

/// <summary>Binds a window frame's keys to its commands.</summary>
public static class CommandKeyBindings
{
    /// <summary>
    /// Adds a keyboard accelerator to a window frame for each key gesture of each command; a gesture that comes after the
    /// focused part runs from the keys the parts leave unhandled.
    /// </summary>
    /// <param name="frame">The window's frame, the element that holds every part of the window.</param>
    /// <param name="commands">The window's commands.</param>
    public static void Attach(UIElement frame, ICommandService commands)
    {
        ArgumentNullException.ThrowIfNull(frame);
        ArgumentNullException.ThrowIfNull(commands);
        var work = new WorkTracker(frame);
        frame.KeyboardAcceleratorPlacementMode = KeyboardAcceleratorPlacementMode.Hidden;
        var late = new List<(CommandKeyGesture Gesture, string CommandId)>();
        foreach (var command in commands.Commands)
        {
            foreach (var gesture in command.KeyGestures)
            {
                if (gesture.AfterFocusedPart)
                {
                    late.Add((gesture, command.Id));
                    continue;
                }

                var accelerator = new KeyboardAccelerator { Key = gesture.Key, Modifiers = gesture.Modifiers };
                accelerator.Invoked += (_, args) => OnInvoked(frame, work, commands, command.Id, args);
                frame.KeyboardAccelerators.Add(accelerator);
            }
        }

        if (late.Count > 0)
        {
            frame.KeyDown += (_, args) => OnKeyDown(frame, work, commands, late, args);
        }
    }

    // Text entry undoes its own text, and a menu or flyout that has the focus keeps the keys while it is open. Otherwise
    // the command goes to the part the user last worked in.
    private static void OnInvoked(UIElement frame, WorkTracker work, ICommandService commands, string commandId, KeyboardAcceleratorInvokedEventArgs args)
    {
        var root = frame.XamlRoot;
        var focus = root is not null ? FocusManager.GetFocusedElement(root) as DependencyObject : null;
        if (IsTextEntry(focus) || (root is not null && IsInOpenPopup(focus, root)))
        {
            return;
        }

        args.Handled = true;
        var context = work.Context ?? (focus is not null && IsInside(focus, frame) ? focus : null);
        _ = commands.ExecuteAsync(commandId, context);
    }

    // A key the focused part left unhandled, that is not held down and not typed into text, runs its command when
    // something runs it.
    private static void OnKeyDown(UIElement frame, WorkTracker work, ICommandService commands, IReadOnlyList<(CommandKeyGesture Gesture, string CommandId)> gestures, KeyRoutedEventArgs args)
    {
        var root = frame.XamlRoot;
        var focus = root is not null ? FocusManager.GetFocusedElement(root) as DependencyObject : null;
        if (args.Handled || args.KeyStatus.WasKeyDown || IsTextEntry(focus))
        {
            return;
        }

        var modifiers = CurrentModifiers();
        var commandId = gestures.FirstOrDefault(entry => entry.Gesture.Key == args.Key && entry.Gesture.Modifiers == modifiers).CommandId;
        var context = work.Context ?? (focus is not null && IsInside(focus, frame) ? focus : null);
        if (commandId is not null && commands.CanExecute(commandId, context))
        {
            args.Handled = true;
            _ = commands.ExecuteAsync(commandId, context);
        }
    }

    private static VirtualKeyModifiers CurrentModifiers()
        => (IsDown(VirtualKey.Control) ? VirtualKeyModifiers.Control : VirtualKeyModifiers.None)
            | (IsDown(VirtualKey.Shift) ? VirtualKeyModifiers.Shift : VirtualKeyModifiers.None)
            | (IsDown(VirtualKey.Menu) ? VirtualKeyModifiers.Menu : VirtualKeyModifiers.None);

    private static bool IsDown(VirtualKey key)
        => InputKeyboardSource.GetKeyStateForCurrentThread(key).HasFlag(CoreVirtualKeyStates.Down);

    private static bool IsTextEntry(DependencyObject? element)
    {
        for (var current = element; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (current is TextBox or RichEditBox or PasswordBox or AutoSuggestBox)
            {
                return true;
            }
        }

        return false;
    }

    private static bool IsInOpenPopup(DependencyObject? element, XamlRoot root)
        => element is not null && VisualTreeHelper.GetOpenPopupsForXamlRoot(root).Any(popup => popup.Child is { } child && IsInside(element, child));

    private static bool IsInside(DependencyObject element, DependencyObject ancestor)
    {
        for (var current = element; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (ReferenceEquals(current, ancestor))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// Remembers where the user last worked in a frame: the control they last pressed or moved the focus to, whether or
    /// not pressing it took the focus.
    /// </summary>
    private sealed class WorkTracker
    {
        private readonly UIElement frame;
        private WeakReference<DependencyObject>[] parts = [];

        public WorkTracker(UIElement frame)
        {
            this.frame = frame;
            frame.GotFocus += (_, args) => this.Record(args.OriginalSource as DependencyObject);
            frame.AddHandler(UIElement.PointerPressedEvent, new PointerEventHandler((_, args) => this.Record(args.OriginalSource as DependencyObject)), handledEventsToo: true);
        }

        /// <summary>Gets the innermost of the control and the views around it that is still in the frame.</summary>
        public DependencyObject? Context
        {
            get
            {
                foreach (var part in this.parts)
                {
                    if (part.TryGetTarget(out var element) && IsInside(element, this.frame))
                    {
                        return element;
                    }
                }

                return null;
            }
        }

        // The control may leave the frame, such as a list's row when the list shows another folder, while the views
        // around it stay; they keep the user's place. A menu or flyout is not a place to work in: its controls are
        // outside the frame and leave the place as it was.
        private void Record(DependencyObject? control)
        {
            var found = new List<WeakReference<DependencyObject>>();
            for (var current = control; current is not null; current = VisualTreeHelper.GetParent(current))
            {
                if (ReferenceEquals(current, this.frame))
                {
                    this.parts = [.. found];
                    return;
                }

                if (ReferenceEquals(current, control) || current is IViewFor)
                {
                    found.Add(new(current));
                }
            }
        }
    }
}
