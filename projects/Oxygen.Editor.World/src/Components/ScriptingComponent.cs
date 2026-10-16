// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using System.Numerics;
using System.Text.Json;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World;

/// <summary>
/// The scripts attached to a node, in slot order. Scripts run in the player, never in editor viewports.
/// </summary>
public sealed partial class ScriptingComponent : GameComponent
{
    /// <summary>The component's default name.</summary>
    public const string DefaultName = "Scripting";

    private ImmutableArray<ScriptSlot> slots = [];

    static ScriptingComponent()
    {
        Register<ScriptingData>(d =>
        {
            var c = new ScriptingComponent { Name = d.Name };
            c.Hydrate(d);
            return c;
        });
    }

    /// <summary>Gets or sets the slots; the array order is the slot position.</summary>
    public ImmutableArray<ScriptSlot> Slots
    {
        get => this.slots;
        set => _ = this.SetProperty(ref this.slots, value.IsDefault ? [] : value);
    }

    /// <summary>Gets the persisted type name of a value kind.</summary>
    /// <param name="kind">The kind.</param>
    /// <returns>The type name, such as <c>vec3</c>; the cooker uses the same names.</returns>
    public static string GetTypeName(ScriptValueKind kind)
        => kind switch
        {
            ScriptValueKind.Bool => "bool",
            ScriptValueKind.Int32 => "int32",
            ScriptValueKind.Float => "float",
            ScriptValueKind.String => "string",
            ScriptValueKind.Vec2 => "vec2",
            ScriptValueKind.Vec3 => "vec3",
            ScriptValueKind.Vec4 => "vec4",
            ScriptValueKind.Asset => "asset",
            ScriptValueKind.Node => "node",
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown script value kind."),
        };

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);
        if (data is not ScriptingData scripting)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            this.Slots = [.. scripting.Slots.Select(HydrateSlot)];
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new ScriptingData
        {
            Id = this.Id,
            Name = this.Name,
            Slots = [.. this.Slots.Select(DehydrateSlot)],
        };

    private static ScriptSlot HydrateSlot(ScriptSlotData data)
    {
        if (data.Id == Guid.Empty)
        {
            throw new JsonException("A script slot needs an identity.");
        }

        var overrides = ImmutableSortedDictionary.CreateBuilder<string, ScriptValue>(StringComparer.Ordinal);
        foreach (var (name, value) in data.Overrides ?? new Dictionary<string, ScriptValueData>(StringComparer.Ordinal))
        {
            overrides[name] = HydrateValue(name, value);
        }

        return new ScriptSlot
        {
            Id = data.Id,
            Script = data.Script,
            ExecutionOrder = data.ExecutionOrder,
            Overrides = overrides.ToImmutable(),
        };
    }

    private static ScriptSlotData DehydrateSlot(ScriptSlot slot)
        => new()
        {
            Id = slot.Id,
            Script = slot.Script,
            ExecutionOrder = slot.ExecutionOrder,
            Overrides = slot.Overrides.IsEmpty
                ? null
                : slot.Overrides.ToDictionary(static entry => entry.Key, static entry => DehydrateValue(entry.Value), StringComparer.Ordinal),
        };

    private static ScriptValue HydrateValue(string name, ScriptValueData data)
    {
        var value = data.Value;
        try
        {
            return data.Type switch
            {
                "bool" => ScriptValue.FromBool(value.GetBoolean()),
                "int32" => ScriptValue.FromInt(value.GetInt32()),
                "float" => ScriptValue.FromFloat(value.GetSingle()),
                "string" => ScriptValue.FromString(value.GetString() ?? throw new JsonException()),
                "vec2" => ScriptValue.FromVector(ScriptValueKind.Vec2, ReadVector(value, 2)),
                "vec3" => ScriptValue.FromVector(ScriptValueKind.Vec3, ReadVector(value, 3)),
                "vec4" => ScriptValue.FromVector(ScriptValueKind.Vec4, ReadVector(value, 4)),
                "asset" => ScriptValue.FromAsset(new Uri(value.GetString() ?? throw new JsonException(), UriKind.Absolute)),
                "node" => ScriptValue.FromNode(NodeId.FromValue(Guid.Parse(value.GetString() ?? throw new JsonException(), CultureInfo.InvariantCulture))),
                _ => throw new JsonException(),
            };
        }
        catch (Exception error) when (error is InvalidOperationException or FormatException or ArgumentException or JsonException)
        {
            throw new JsonException($"Script parameter '{name}' has an invalid {data.Type} value.", error);
        }
    }

    private static Vector4 ReadVector(JsonElement value, int size)
    {
        if (value.ValueKind != JsonValueKind.Array || value.GetArrayLength() != size)
        {
            throw new JsonException();
        }

        var components = new float[4];
        var index = 0;
        foreach (var component in value.EnumerateArray())
        {
            components[index++] = component.GetSingle();
        }

        return new Vector4(components[0], components[1], components[2], components[3]);
    }

    private static ScriptValueData DehydrateValue(ScriptValue value)
        => new()
        {
            Type = GetTypeName(value.Kind),
            Value = value.Kind switch
            {
                ScriptValueKind.Bool => JsonSerializer.SerializeToElement(value.BoolValue, SceneJsonContext.Default.Boolean),
                ScriptValueKind.Int32 => JsonSerializer.SerializeToElement(value.IntValue, SceneJsonContext.Default.Int32),
                ScriptValueKind.Float => JsonSerializer.SerializeToElement(value.FloatValue, SceneJsonContext.Default.Single),
                ScriptValueKind.String => JsonSerializer.SerializeToElement(value.StringValue, SceneJsonContext.Default.String),
                ScriptValueKind.Vec2 or ScriptValueKind.Vec3 or ScriptValueKind.Vec4
                    => JsonSerializer.SerializeToElement(value.GetVectorComponents(), SceneJsonContext.Default.SingleArray),
                ScriptValueKind.Asset => JsonSerializer.SerializeToElement(value.AssetUri!.ToString(), SceneJsonContext.Default.String),
                ScriptValueKind.Node => JsonSerializer.SerializeToElement(value.NodeId.ToString(), SceneJsonContext.Default.String),
                _ => throw new InvalidOperationException($"Unknown script value kind {value.Kind}."),
            },
        };
}
