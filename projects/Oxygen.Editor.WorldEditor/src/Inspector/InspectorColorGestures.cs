// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Preserves color gesture scope even when child controls handle routed input.</summary>
internal static class InspectorColorGestures
{
    private static readonly ConditionalWeakTable<ColorPicker, Gesture> Gestures = [];

    /// <summary>Binds a color picker to its current inspector lifetime.</summary>
    /// <param name="picker">The loaded picker.</param>
    /// <param name="owner">The bound edit-session owner.</param>
    /// <param name="field">The edited field key.</param>
    public static void Attach(ColorPicker picker, IInspectorEditSessionOwner? owner, string field)
    {
        if (owner is null)
        {
            return;
        }

        var gesture = Gestures.GetValue(picker, value => new Gesture(value));
        gesture.Bind(owner, field);
    }

    /// <summary>Applies a color sample only to the lifetime captured at binding.</summary>
    /// <param name="picker">The originating picker.</param>
    /// <param name="apply">The authoring action for the captured owner.</param>
    public static void Apply(ColorPicker picker, Action<IInspectorEditSessionOwner> apply)
    {
        if (Gestures.TryGetValue(picker, out var gesture))
        {
            gesture.Apply(apply);
        }
    }

    private sealed class Gesture
    {
        private IInspectorEditSessionOwner? owner;
        private Guid scope;
        private string field = string.Empty;
        private bool active;

        public Gesture(ColorPicker picker)
        {
            picker.AddHandler(UIElement.PointerPressedEvent, new PointerEventHandler(this.Pressed), handledEventsToo: true);
            picker.AddHandler(UIElement.PointerReleasedEvent, new PointerEventHandler(this.Released), handledEventsToo: true);
            picker.AddHandler(UIElement.PointerCaptureLostEvent, new PointerEventHandler(this.CaptureLost), handledEventsToo: true);
            picker.AddHandler(UIElement.PointerCanceledEvent, new PointerEventHandler(this.Canceled), handledEventsToo: true);
            picker.AddHandler(UIElement.KeyDownEvent, new KeyEventHandler(this.KeyDown), handledEventsToo: true);
            picker.LostFocus += this.LostFocus;
            picker.Unloaded += this.Unloaded;
        }

        public void Bind(IInspectorEditSessionOwner nextOwner, string nextField)
        {
            this.End(NumberBoxEditCompletionKind.Cancel);
            this.owner = nextOwner;
            this.scope = nextOwner.EditScopeId;
            this.field = nextField;
        }

        public void Apply(Action<IInspectorEditSessionOwner> apply)
        {
            if (this.owner is null || this.owner.EditScopeId != this.scope)
            {
                return;
            }

            // ColorChanged may run before the handled PointerPressed bubbles.
            this.Begin(NumberBoxEditInteractionKind.PointerDrag);
            apply(this.owner);
        }

        private void Begin(NumberBoxEditInteractionKind kind)
        {
            if (!this.active && this.owner?.EditScopeId == this.scope)
            {
                this.active = true;
                this.owner.BeginEditSession(this.field, kind);
            }
        }

        private void End(NumberBoxEditCompletionKind completion)
        {
            if (this.active)
            {
                this.active = false;
                if (this.owner?.EditScopeId == this.scope)
                {
                    this.owner.EndEditSession(completion);
                }
            }
        }

        private void Pressed(object sender, PointerRoutedEventArgs args) => this.Begin(NumberBoxEditInteractionKind.PointerDrag);

        private void Released(object sender, PointerRoutedEventArgs args) => this.End(NumberBoxEditCompletionKind.Commit);

        // ColorSpectrum releases capture inside its PointerReleased handler,
        // before that handled event reaches the picker. A released pointer
        // commits; losing capture while still pressed cancels the preview.
        private void CaptureLost(object sender, PointerRoutedEventArgs args)
            => this.End(args.Pointer.IsInContact ? NumberBoxEditCompletionKind.Cancel : NumberBoxEditCompletionKind.Commit);

        private void Canceled(object sender, PointerRoutedEventArgs args) => this.End(NumberBoxEditCompletionKind.Cancel);

        private void LostFocus(object sender, RoutedEventArgs args) => this.End(NumberBoxEditCompletionKind.Commit);

        private void Unloaded(object sender, RoutedEventArgs args)
        {
            this.End(NumberBoxEditCompletionKind.Commit);
            this.owner = null;
        }

        private void KeyDown(object sender, KeyRoutedEventArgs args)
        {
            if (args.Key == VirtualKey.Escape)
            {
                this.End(NumberBoxEditCompletionKind.Cancel);
                args.Handled = true;
            }
            else if (args.Key == VirtualKey.Enter)
            {
                this.End(NumberBoxEditCompletionKind.Commit);
            }
            else
            {
                this.Begin(NumberBoxEditInteractionKind.Text);
            }
        }
    }
}
