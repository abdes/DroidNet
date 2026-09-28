// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;
using Json.Schema;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Retains the native slot-allocation record without recomputing its identities or hashes.</summary>
[JsonConverter(typeof(NativeMaterialSlotProvenance.Converter))]
public sealed class NativeMaterialSlotProvenance
{
    private static readonly Lazy<JsonSchema> Schema = new(LoadSchema);
    private readonly JsonElement value;

    private NativeMaterialSlotProvenance(JsonElement value)
    {
        this.value = value.Clone();
        this.SourceIdentity = value.GetProperty("source_identity").GetGuid();
    }

    /// <summary>Gets the retained source namespace, distinct from cooked-container identity.</summary>
    public Guid SourceIdentity { get; }

    /// <summary>Creates an empty allocation record for a newly retained source.</summary>
    /// <returns>The source namespace to persist before its first import.</returns>
    public static NativeMaterialSlotProvenance Create()
        => Parse(JsonSerializer.SerializeToElement(new
        {
            schema_version = 1,
            source_identity = Guid.CreateVersion7(),
            geometries = Array.Empty<object>(),
        }));

    /// <summary>Validates the native transport schema and owns the supplied record.</summary>
    /// <param name="value">Native provenance from source settings or a successful cook.</param>
    /// <returns>The retained record; allocation semantics remain native-owned.</returns>
    public static NativeMaterialSlotProvenance Parse(JsonElement value)
        => Schema.Value.Evaluate(value).IsValid
            ? new(value)
            : throw new InvalidDataException("Invalid native material-slot provenance.");

    /// <summary>Gets the immutable native descriptor for manifest transport.</summary>
    /// <returns>The native JSON value, with no managed hash or identity conversion.</returns>
    public JsonElement ToJson() => this.value;

    private static JsonSchema LoadSchema()
    {
        using var stream = typeof(NativeMaterialSlotProvenance).Assembly.GetManifestResourceStream(
            "oxygen.material-slot-provenance.schema.json")
            ?? throw new InvalidOperationException("The native slot-provenance schema is missing.");
        using var reader = new StreamReader(stream);
        return JsonSchema.FromText(reader.ReadToEnd());
    }

    internal sealed class Converter : JsonConverter<NativeMaterialSlotProvenance>
    {
        public Converter()
        {
        }

        public override NativeMaterialSlotProvenance Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            using var document = JsonDocument.ParseValue(ref reader);
            return Parse(document.RootElement);
        }

        public override void Write(Utf8JsonWriter writer, NativeMaterialSlotProvenance value, JsonSerializerOptions options)
            => value.value.WriteTo(writer);
    }
}
