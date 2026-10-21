// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using DroidNet.Controls;
using Oxygen.Editor.Controls;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>Routes <see cref="InspectorColorField"/> edits to an inspector edit owner.</summary>
/// <remarks>
///     A channel edit uses the field's <c>Tag</c> plus the channel name as its key, a pick uses the <c>Tag</c>. A pick
///     only applies within the binding scope it began in, so a picker left open while the inspector binds other
///     targets never edits them, and an old field finishing late never ends the current field's pick.
/// </remarks>
internal static class InspectorColorEdits
{
    private static readonly ConditionalWeakTable<InspectorColorField, Pick> Picks = [];

    /// <summary>Begins a channel edit or a pick.</summary>
    /// <param name="owner">The edit owner bound now.</param>
    /// <param name="sender">The field that raised the event.</param>
    /// <param name="args">The field's start event.</param>
    public static void Begin(IInspectorEditSessionOwner? owner, object? sender, ColorBoxEditSessionEventArgs args)
    {
        if (owner is null || sender is not InspectorColorField { Tag: string field } color)
        {
            return;
        }

        if (args.Channel is { } channel)
        {
            owner.BeginEditSession(field + ColorBox.ChannelName(channel), args.InteractionKind);
            return;
        }

        Picks.AddOrUpdate(color, new Pick(owner, owner.EditScopeId));
        owner.BeginEditSession(field, args.InteractionKind);
    }

    /// <summary>Completes a channel edit or ends a pick that is still in its scope.</summary>
    /// <param name="owner">The edit owner bound now.</param>
    /// <param name="sender">The field that raised the event.</param>
    /// <param name="args">The field's completion event.</param>
    public static void Complete(IInspectorEditSessionOwner? owner, object? sender, ColorBoxEditSessionEventArgs args)
    {
        if (args.Channel is not null)
        {
            owner?.CompleteEditSession(new NumberBoxEditSessionEventArgs(args.InteractionKind, args.CompletionKind, args.InputText));
            return;
        }

        if (sender is InspectorColorField color && Picks.TryGetValue(color, out var pick))
        {
            _ = Picks.Remove(color);
            if (pick.IsCurrent)
            {
                pick.Owner.EndEditSession(args.CompletionKind ?? NumberBoxEditCompletionKind.Commit);
            }
        }
    }

    /// <summary>Applies a picked color to the owner the pick began on, while it is still in that pick's scope.</summary>
    /// <typeparam name="TOwner">The owner type that can apply the color.</typeparam>
    /// <param name="sender">The field that raised the event.</param>
    /// <param name="args">The pick; always marked handled, so the field never writes channels on its own.</param>
    /// <param name="apply">Applies the whole color as one edit.</param>
    public static void Apply<TOwner>(object? sender, ColorBoxColorPickedEventArgs args, Action<TOwner, ColorChannels> apply)
        where TOwner : class, IInspectorEditSessionOwner
    {
        args.Handled = true;
        if (sender is InspectorColorField color && Picks.TryGetValue(color, out var pick) && pick.IsCurrent && pick.Owner is TOwner owner)
        {
            apply(owner, args.Color);
        }
    }

    private sealed class Pick(IInspectorEditSessionOwner owner, Guid scope)
    {
        public IInspectorEditSessionOwner Owner { get; } = owner;

        public bool IsCurrent => this.Owner.EditScopeId == scope;
    }
}
