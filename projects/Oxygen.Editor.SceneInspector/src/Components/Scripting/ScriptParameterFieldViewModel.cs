// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using System.Numerics;
using System.Text;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Scripting;

/// <summary>
/// One field of a script slot: the value of one declared parameter, edited with the control its type and hints call
/// for. A value equal to the declared default is not stored, so the field shows the override marker only for values
/// that differ.
/// </summary>
public sealed partial class ScriptParameterFieldViewModel : ObservableObject
{
    /// <summary>The longest string value, in UTF-8 bytes, the cooked parameter record holds.</summary>
    internal const int MaxStringBytes = 59;

    private readonly ScriptSlotCardViewModel card;
    private ScriptValue? value;
    private bool updating;
    private bool inSession;

    /// <summary>Initializes a new instance of the <see cref="ScriptParameterFieldViewModel"/> class.</summary>
    /// <param name="card">The slot card that owns the field.</param>
    /// <param name="parameter">The declared parameter.</param>
    internal ScriptParameterFieldViewModel(ScriptSlotCardViewModel card, ScriptParameter parameter)
    {
        this.card = card;
        this.Parameter = parameter;
        this.Kind = ResolveKind(parameter);
        this.Dimension = ScriptValue.VectorSize(parameter.ValueKind ?? ScriptValueKind.BoolValue);
    }

    /// <summary>Gets the declared parameter.</summary>
    public ScriptParameter Parameter { get; private set; }

    /// <summary>Gets the editor kind.</summary>
    public ScriptFieldKind Kind { get; }

    /// <summary>Gets the parameter name.</summary>
    public string Name => this.Parameter.Name;

    /// <summary>Gets the label, made readable from the parameter name.</summary>
    public string Label => Humanize(this.Parameter.Name);

    /// <summary>Gets the tooltip: the parameter's description, or its name.</summary>
    public string ToolTip => this.Parameter.Description.Length > 0 ? this.Parameter.Description : this.Parameter.Name;

    /// <summary>Gets the unit shown after a number.</summary>
    public string Unit => this.Parameter.Unit;

    /// <summary>Gets the number format: whole numbers for integers, the step's precision for floats.</summary>
    public string NumberMask => this.Parameter.Kind == ScriptParameterKind.Int32Parameter ? "~." : "~." + new string('#', Math.Clamp(Decimals(this.Parameter.Step), 1, 4));

    /// <summary>Gets the declared choices of a string parameter.</summary>
    public IReadOnlyList<string> Options => this.Parameter.Options;

    /// <summary>Gets the number of components of a vector or color parameter: 2, 3 or 4.</summary>
    public int Dimension { get; }

    /// <summary>Gets a value indicating whether a color parameter has alpha: a <c>vec4</c> color.</summary>
    public bool HasAlpha => this.Dimension == 4;

    /// <summary>Gets the assets an asset parameter accepts, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel AssetChoices { get; } = new(static asset => AssetChoice.For(asset, asset.TypeDisplayName, "\uE8A5"));

    /// <summary>Gets the nodes a node parameter offers for the current search.</summary>
    public ObservableCollection<ScriptNodeChoice> NodeChoices { get; } = [];

    /// <summary>Gets a value indicating whether the value differs from the declared default.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowsOverrideMarker))]
    public partial bool IsOverridden { get; private set; }

    /// <summary>Gets or sets the value of a boolean parameter.</summary>
    [ObservableProperty]
    public partial bool BoolValue { get; set; }

    /// <summary>Gets or sets the value of a number parameter.</summary>
    [ObservableProperty]
    public partial double NumberValue { get; set; }

    /// <summary>Gets or sets the value of a string parameter.</summary>
    [ObservableProperty]
    public partial string TextValue { get; set; } = string.Empty;

    /// <summary>Gets or sets the chosen option of a string parameter with options.</summary>
    [ObservableProperty]
    public partial string? SelectedOption { get; set; }

    /// <summary>Gets or sets the first component of a vector, or the red channel of a linear color.</summary>
    [ObservableProperty]
    public partial double X { get; set; }

    /// <summary>Gets or sets the second component of a vector, or the green channel of a linear color.</summary>
    [ObservableProperty]
    public partial double Y { get; set; }

    /// <summary>Gets or sets the third component of a vector, or the blue channel of a linear color.</summary>
    [ObservableProperty]
    public partial double Z { get; set; }

    /// <summary>Gets or sets the fourth component of a vector, or the alpha of a color.</summary>
    [ObservableProperty]
    public partial double W { get; set; }

    /// <summary>Gets the name of the referenced node or asset, or <c>None</c>.</summary>
    [ObservableProperty]
    public partial string ReferenceText { get; private set; } = "None";

    /// <summary>Gets a value indicating whether the referenced node or asset no longer exists.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowsOverrideMarker))]
    public partial bool IsReferenceMissing { get; private set; }

    /// <summary>Gets or sets the node search text.</summary>
    [ObservableProperty]
    public partial string NodeQuery { get; set; } = string.Empty;

    /// <summary>Gets the reason the last value was refused, or empty.</summary>
    [ObservableProperty]
    public partial string ErrorText { get; private set; } = string.Empty;

    /// <summary>Gets a value indicating whether the field shows an error.</summary>
    public bool HasError => this.ErrorText.Length > 0;

    /// <summary>Gets a value indicating whether the override marker shows; a missing node shows in the critical
    /// color instead.</summary>
    public bool ShowsOverrideMarker => this.IsOverridden && !this.IsReferenceMissing;

    /// <summary>Gets a value indicating whether the slot can be edited.</summary>
    public bool CanEdit => this.card.CanEdit;

    /// <summary>Starts an interactive edit, such as a drag or a pick; the value is committed when it completes.</summary>
    public void BeginSession() => this.inSession = true;

    /// <summary>Completes an interactive edit.</summary>
    /// <param name="commit">Whether to commit the edited value; otherwise the stored value is shown again.</param>
    public void CompleteSession(bool commit)
    {
        this.inSession = false;
        if (commit)
        {
            this.CommitNumbers();
        }
        else
        {
            this.ShowValue();
        }
    }

    /// <summary>Picks the referenced node.</summary>
    /// <param name="choice">The node, or null for none.</param>
    public void SelectNode(ScriptNodeChoice? choice)
        => this.Commit(choice is null ? null : ScriptValue.FromNode(choice.Id));

    /// <summary>Picks the referenced asset.</summary>
    /// <param name="asset">The asset.</param>
    public void SelectAsset(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        this.Commit(ScriptValue.FromAsset(asset));
    }

    /// <summary>Accepts nodes dragged from the Scene Explorer; the first one of this scene is referenced.</summary>
    /// <param name="nodes">The dragged node identities.</param>
    /// <returns>Whether a node was accepted.</returns>
    public bool DropNodes(IReadOnlyList<NodeId> nodes)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        var node = nodes.FirstOrDefault(this.card.Owner.ContainsNode);
        if (node.IsEmpty)
        {
            this.SetError("Drop a node of this scene.");
            return false;
        }

        this.Commit(ScriptValue.FromNode(node));
        return true;
    }

    /// <summary>Refreshes the node choices for the current search.</summary>
    public void RefreshNodeChoices()
    {
        this.NodeChoices.Clear();
        foreach (var node in this.card.Owner.FindNodes(this.NodeQuery))
        {
            this.NodeChoices.Add(node);
        }
    }

    /// <summary>Shows a parameter's declaration and value without editing it.</summary>
    /// <param name="parameter">The declared parameter.</param>
    /// <param name="current">The slot's value for it, or null when it takes the default.</param>
    internal void Update(ScriptParameter parameter, ScriptValue? current)
    {
        this.Parameter = parameter;
        this.value = current;
        this.IsOverridden = current is not null;
        this.SetError(string.Empty);
        this.OnPropertyChanged(nameof(this.CanEdit));
        this.OnPropertyChanged(nameof(this.ToolTip));
        if (this.Kind == ScriptFieldKind.Asset)
        {
            this.RefreshAssetChoices();
        }

        if (!this.inSession)
        {
            this.ShowValue();
        }
    }

    private static ScriptFieldKind ResolveKind(ScriptParameter parameter)
        => parameter.Kind switch
        {
            ScriptParameterKind.BoolParameter => ScriptFieldKind.Toggle,
            ScriptParameterKind.Int32Parameter or ScriptParameterKind.FloatParameter => ScriptFieldKind.Number,
            ScriptParameterKind.StringParameter => parameter.Options.Count > 0 ? ScriptFieldKind.Choice : ScriptFieldKind.Text,
            ScriptParameterKind.Vec3Parameter or ScriptParameterKind.Vec4Parameter when parameter.IsColor => ScriptFieldKind.Color,
            ScriptParameterKind.Vec2Parameter or ScriptParameterKind.Vec3Parameter or ScriptParameterKind.Vec4Parameter => ScriptFieldKind.Vector,
            ScriptParameterKind.AssetParameter => ScriptFieldKind.Asset,
            _ => ScriptFieldKind.Node,
        };

    private static string Humanize(string name)
    {
        var builder = new StringBuilder(name.Length + 4);
        for (var index = 0; index < name.Length; index++)
        {
            var character = name[index];
            if (character is '_' or '-')
            {
                _ = builder.Append(' ');
                continue;
            }

            if (index > 0 && char.IsUpper(character) && char.IsLower(name[index - 1]))
            {
                _ = builder.Append(' ');
            }

            _ = builder.Append(builder.Length == 0 ? char.ToUpper(character, CultureInfo.CurrentCulture) : char.ToLower(character, CultureInfo.CurrentCulture));
        }

        return builder.ToString().Trim();
    }

    private static int Decimals(double? step)
    {
        if (step is not { } increment || increment <= 0 || increment >= 1)
        {
            return 3;
        }

        return (int)Math.Ceiling(-Math.Log10(increment) - 1e-9);
    }

    partial void OnBoolValueChanged(bool value)
    {
        if (!this.updating)
        {
            this.Commit(ScriptValue.FromBool(value));
        }
    }

    partial void OnNumberValueChanged(double value)
    {
        _ = value;
        if (!this.updating && !this.inSession)
        {
            this.CommitNumbers();
        }
    }

    partial void OnXChanged(double value)
    {
        _ = value;
        this.OnComponentChanged();
    }

    partial void OnYChanged(double value)
    {
        _ = value;
        this.OnComponentChanged();
    }

    partial void OnZChanged(double value)
    {
        _ = value;
        this.OnComponentChanged();
    }

    partial void OnWChanged(double value)
    {
        _ = value;
        this.OnComponentChanged();
    }

    partial void OnTextValueChanged(string value)
    {
        if (this.updating)
        {
            return;
        }

        if (Encoding.UTF8.GetByteCount(value) > MaxStringBytes)
        {
            this.SetError(string.Create(CultureInfo.CurrentCulture, $"Use at most {MaxStringBytes} characters; the cooked parameter holds no more."));
            return;
        }

        this.Commit(ScriptValue.FromString(value));
    }

    partial void OnSelectedOptionChanged(string? value)
    {
        if (!this.updating && value is not null)
        {
            this.Commit(ScriptValue.FromString(value));
        }
    }

    partial void OnNodeQueryChanged(string value)
    {
        _ = value;
        this.RefreshNodeChoices();
    }

    partial void OnErrorTextChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasError));
    }

    [RelayCommand]
    private void Reset() => this.Commit(candidate: null);

    private void CommitNumbers()
    {
        switch (this.Parameter.Kind)
        {
            case ScriptParameterKind.Int32Parameter:
                this.Commit(ScriptValue.FromInt((int)Math.Round(this.Clamp(this.NumberValue))));
                break;
            case ScriptParameterKind.FloatParameter:
                this.Commit(ScriptValue.FromFloat((float)this.Clamp(this.NumberValue)));
                break;
            case ScriptParameterKind.Vec2Parameter or ScriptParameterKind.Vec3Parameter or ScriptParameterKind.Vec4Parameter:
                // Components the vector does not have stay zero, as the stored value expects.
                this.Commit(ScriptValue.FromVector(
                    this.Parameter.ValueKind!.Value,
                    new Vector4(
                        (float)this.Clamp(this.X),
                        (float)this.Clamp(this.Y),
                        this.Dimension > 2 ? (float)this.Clamp(this.Z) : 0f,
                        this.Dimension > 3 ? (float)this.Clamp(this.W) : 0f)));
                break;
            default:
                break;
        }
    }

    private void OnComponentChanged()
    {
        if (!this.updating && !this.inSession)
        {
            this.CommitNumbers();
        }
    }

    private double Clamp(double number)
        => Math.Min(Math.Max(number, this.Parameter.Min ?? double.NegativeInfinity), this.Parameter.Max ?? double.PositiveInfinity);

    private void Commit(ScriptValue? candidate)
    {
        this.SetError(string.Empty);
        if (Equals(candidate, this.value ?? this.Parameter.Default) && candidate is not null)
        {
            this.ShowValue();
            return;
        }

        _ = this.card.SetValueAsync(this.Parameter, candidate);
    }

    private void ShowValue()
    {
        var shown = this.value ?? this.Parameter.Default;
        this.updating = true;
        try
        {
            switch (this.Kind)
            {
                case ScriptFieldKind.Toggle:
                    this.BoolValue = shown?.BoolValue ?? false;
                    break;
                case ScriptFieldKind.Number:
                    this.NumberValue = shown?.Kind == ScriptValueKind.Int32Value ? shown.IntValue : shown?.FloatValue ?? 0f;
                    break;
                case ScriptFieldKind.Choice:
                    this.SelectedOption = shown?.StringValue is { } option && this.Options.Contains(option, StringComparer.Ordinal) ? option : null;
                    break;
                case ScriptFieldKind.Text:
                    this.TextValue = shown?.StringValue ?? string.Empty;
                    break;
                case ScriptFieldKind.Vector or ScriptFieldKind.Color:
                    var vector = shown?.VectorValue ?? Vector4.Zero;
                    (this.X, this.Y, this.Z, this.W) = (vector.X, vector.Y, vector.Z, vector.W);
                    break;
                case ScriptFieldKind.Asset:
                    var asset = shown?.Asset;
                    var picked = asset is null ? null : this.AssetChoices.Find(asset);
                    this.IsReferenceMissing = asset is not null && picked is null;
                    this.ReferenceText = asset is null ? "None" : picked?.Name ?? ("Missing: " + asset.VirtualPath);
                    break;
                case ScriptFieldKind.Node:
                    var node = shown?.NodeId;
                    var name = node is { } id ? this.card.Owner.DescribeNode(id) : null;
                    this.IsReferenceMissing = node is not null && name is null;
                    this.ReferenceText = node is null ? "None" : name ?? "Missing node";
                    break;
                default:
                    break;
            }
        }
        finally
        {
            this.updating = false;
        }
    }

    private void RefreshAssetChoices() => this.AssetChoices.Update(this.card.Owner.FindAssets(this.Parameter.AssetType));

    private void SetError(string message) => this.ErrorText = message;
}
