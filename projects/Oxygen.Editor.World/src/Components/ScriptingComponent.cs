// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

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
            ScriptValueKind.BoolValue => "bool",
            ScriptValueKind.Int32Value => "int32",
            ScriptValueKind.FloatValue => "float",
            ScriptValueKind.StringValue => "string",
            ScriptValueKind.Vec2Value => "vec2",
            ScriptValueKind.Vec3Value => "vec3",
            ScriptValueKind.Vec4Value => "vec4",
            ScriptValueKind.AssetValue => ScriptValueData.AssetType,
            ScriptValueKind.NodeValue => "node",
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
            overrides[name] = ScriptValueJson.Read(name, value);
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
                : slot.Overrides.ToDictionary(static entry => entry.Key, static entry => ScriptValueJson.Write(entry.Value), StringComparer.Ordinal),
        };
}
