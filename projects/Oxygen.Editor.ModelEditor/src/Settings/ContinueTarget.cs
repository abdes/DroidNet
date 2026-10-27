// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>A node <em>Continue…</em> offers for a vanished anchor: a node of the last import.</summary>
/// <param name="Anchor">The node's anchor.</param>
/// <param name="IsNew">Whether the node is new in the last import.</param>
/// <param name="Continues">The anchor the node already continues, which disables it; none when it continues none.</param>
/// <param name="Vanished">The vanished anchor the node would continue.</param>
public sealed record ContinueTarget(string Anchor, bool IsNew, string? Continues, string Vanished)
{
    /// <summary>Gets a value indicating whether the node can continue the vanished anchor: it continues no other.</summary>
    public bool IsEnabled => this.Continues is null;

    /// <summary>Gets what the choice says beside the anchor: "continues Door_FL" for a node that already continues
    /// another, "new" for a node new in the last import.</summary>
    public string Note => this.Continues is { } continued ? $"continues {NameOf(continued)}" : this.IsNew ? "new" : string.Empty;

    /// <summary>Gets the choice's opacity: dimmed when it cannot be chosen.</summary>
    public double Opacity => this.IsEnabled ? 1.0 : 0.5;

    /// <summary>Gets what choosing the node does to the changes scenes made.</summary>
    /// <remarks>
    /// The node takes the vanished node's ID, so the changes made on it apply to the node; a node that existed before the
    /// last import has its own changes too, which become kept changes under its old ID.
    /// </remarks>
    public string Caution
    {
        get
        {
            var takes = $"{NameOf(this.Anchor)} takes the ID of {NameOf(this.Vanished)}: the changes made on {NameOf(this.Vanished)}, in every scene, apply to it.";
            return this.IsNew ? takes : $"{takes} The changes made on {NameOf(this.Anchor)} under its own ID become kept changes.";
        }
    }

    // An anchor's last name, as the scenes' rows show the node.
    private static string NameOf(string anchor) => anchor[(anchor.LastIndexOf('/') + 1)..];
}
