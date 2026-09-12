// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Maintains target-lifetime and property-scoped diagnostic ordering.</summary>
internal sealed class InspectorFieldDiagnostics
{
    private readonly Dictionary<PropertyId, InspectorFieldDiagnostic> fields = [];
    private readonly Dictionary<PropertyId, long> latest = [];
    private readonly Dictionary<PropertyId, HashSet<PropertyId>> dependents = [];
    private long sequence;
    private Guid scope = Guid.NewGuid();

    /// <summary>Occurs when current field feedback changes.</summary>
    public event EventHandler? FeedbackChanged;

    /// <summary>Gets the distinct current errors for the component entry.</summary>
    public string Summary => string.Join(Environment.NewLine, this.fields.Values.Select(static diagnostic => diagnostic.Message)
        .Where(static message => message.Length != 0).Distinct(StringComparer.Ordinal));

    /// <summary>Gets stable bindable feedback for a property.</summary>
    /// <param name="property">The originating property.</param>
    /// <returns>The current field diagnostic.</returns>
    public InspectorFieldDiagnostic Get(PropertyId property)
    {
        if (!this.fields.TryGetValue(property, out var result))
        {
            result = new();
            this.fields[property] = result;
        }

        return result;
    }

    /// <summary>Groups properties whose validity depends on each other.</summary>
    /// <param name="first">The first related property.</param>
    /// <param name="second">The second related property.</param>
    public void Relate(PropertyId first, PropertyId second)
    {
        this.dependents[first] = [first, second];
        this.dependents[second] = [first, second];
    }

    /// <summary>Captures ordering and document scope before an edit is submitted.</summary>
    /// <param name="properties">The edited properties.</param>
    /// <param name="revision">The originating authoring revision.</param>
    /// <returns>The ticket required to deliver the result.</returns>
    public Ticket Begin(IEnumerable<PropertyId> properties, long revision)
    {
        var affected = properties.SelectMany(property => this.dependents.TryGetValue(property, out var group) ? group : [property]).ToHashSet();
        var requestSequence = ++this.sequence;
        foreach (var property in affected)
        {
            this.latest[property] = requestSequence;
        }

        return new(this.scope, requestSequence, revision, affected);
    }

    /// <summary>Applies feedback only if the request is still current.</summary>
    /// <param name="ticket">The originating edit ticket.</param>
    /// <param name="result">The command validation result.</param>
    public void Complete(Ticket ticket, SceneCommandResult result)
    {
        if (ticket.Scope != this.scope)
        {
            return;
        }

        foreach (var property in ticket.Properties)
        {
            if (!this.latest.TryGetValue(property, out var latest) || latest != ticket.Sequence)
            {
                continue;
            }

            var field = this.Get(property);
            field.Revision = ticket.Revision;
            field.Code = result.Succeeded ? string.Empty : result.ValidationCode ?? "PROPERTY_REJECTED";
            field.Message = result.Succeeded ? string.Empty : result.ValidationMessage ?? "This value was rejected.";
        }

        this.FeedbackChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Clears feedback superseded by a model refresh.</summary>
    /// <param name="property">The refreshed property.</param>
    public void Invalidate(PropertyId property)
    {
        var ticket = this.Begin([property], revision: 0);
        this.Complete(ticket, SceneCommandResult.Success);
    }

    /// <summary>Invalidates pending delivery and clears feedback when rebinding.</summary>
    public void Reset()
    {
        this.scope = Guid.NewGuid();
        this.latest.Clear();
        foreach (var field in this.fields.Values)
        {
            field.Code = string.Empty;
            field.Message = string.Empty;
        }

        this.FeedbackChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Identifies the source scope, edit order, revision and affected fields.</summary>
    internal sealed record Ticket(Guid Scope, long Sequence, long Revision, IReadOnlySet<PropertyId> Properties);
}
