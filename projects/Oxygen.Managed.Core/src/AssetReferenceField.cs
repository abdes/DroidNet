// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;

namespace Oxygen.Managed.Core;

/// <summary>
/// A field of an authored JSON format that holds an asset reference: an asset URI or a virtual path naming an asset's
/// identity. A format names its reference fields, so tools read and rewrite references there and nowhere else; a
/// string elsewhere is text, even one shaped like a path.
/// </summary>
/// <param name="Path">
/// The field's path from the document root: property names separated by <c>/</c>, where <c>*</c> matches every
/// element of an array or every property of an object, and <c>**</c> matches any number of levels, for a tree such as
/// a scene's nodes (<c>RootNodes/**/Components/*/GeometryUri</c>).
/// </param>
public sealed record AssetReferenceField(string Path)
{
    private const string AnyMember = "*";
    private const string AnyDepth = "**";

    /// <summary>
    /// Gets the property of the field's object that must hold <see cref="WhenValue"/> for the field to be a reference,
    /// such as the <c>Type</c> of a script parameter value; null when the field always is one.
    /// </summary>
    public string? WhenProperty { get; init; }

    /// <summary>Gets the value <see cref="WhenProperty"/> must hold.</summary>
    public string? WhenValue { get; init; }

    /// <summary>Finds the field's string values in a document.</summary>
    /// <param name="root">The document.</param>
    /// <returns>The values, each once, in document order.</returns>
    public IReadOnlyList<JsonValue> Select(JsonNode? root)
    {
        var found = new List<JsonValue>();
        this.Collect(root, this.Path.Split('/'), 0, owner: null, found);
        return [.. found.Distinct(ReferenceEqualityComparer.Instance).Cast<JsonValue>()];
    }

    // The members of an object, with the object as their owner, or the elements of an array, which have none.
    private static IEnumerable<(JsonNode? Child, JsonObject? Owner)> Children(JsonNode? node) => node switch
    {
        JsonObject value => value.Select(property => (property.Value, (JsonObject?)value)),
        JsonArray array => array.Select(static item => (item, (JsonObject?)null)),
        _ => [],
    };

    private void Collect(JsonNode? node, string[] segments, int index, JsonObject? owner, List<JsonValue> found)
    {
        if (index == segments.Length)
        {
            if (node is JsonValue value && value.TryGetValue<string>(out _) && this.Applies(owner))
            {
                found.Add(value);
            }

            return;
        }

        var segment = segments[index];
        if (string.Equals(segment, AnyDepth, StringComparison.Ordinal))
        {
            this.Collect(node, segments, index + 1, owner, found);
            foreach (var (child, parent) in Children(node))
            {
                this.Collect(child, segments, index, parent, found);
            }

            return;
        }

        if (string.Equals(segment, AnyMember, StringComparison.Ordinal))
        {
            foreach (var (child, parent) in Children(node))
            {
                this.Collect(child, segments, index + 1, parent, found);
            }
        }
        else if (node is JsonObject value && value.TryGetPropertyValue(segment, out var child))
        {
            this.Collect(child, segments, index + 1, value, found);
        }
    }

    private bool Applies(JsonObject? owner)
        => this.WhenProperty is null
            || (owner?[this.WhenProperty] is JsonValue condition && condition.TryGetValue<string>(out var text)
                && string.Equals(text, this.WhenValue, StringComparison.Ordinal));
}
