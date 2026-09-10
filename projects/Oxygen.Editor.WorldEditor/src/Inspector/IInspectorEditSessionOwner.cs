// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Connects existing field controls to the document's gesture transaction.</summary>
public interface IInspectorEditSessionOwner
{
    /// <summary>Gets the current document/selection binding identity.</summary>
    public Guid EditScopeId { get; }

    /// <summary>Begins editing a field using the current stable target set.</summary>
    /// <param name="field">The control's field key.</param>
    /// <param name="interaction">The control interaction.</param>
    public void BeginEditSession(string field, NumberBoxEditInteractionKind interaction);

    /// <summary>Completes a numeric edit or schedules its wheel idle commit.</summary>
    /// <param name="args">The control completion.</param>
    public void CompleteEditSession(NumberBoxEditSessionEventArgs args);

    /// <summary>Commits or cancels the active color/text/drag transaction.</summary>
    /// <param name="completion">The requested terminal action.</param>
    public void EndEditSession(NumberBoxEditCompletionKind completion);
}
