// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.CompilerServices;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Represents the base class for module settings, providing functionality to save and load settings asynchronously.
/// </summary>
/// <remarks>
/// The <see cref="ModuleSettings"/> class is an abstract base class that provides common
/// functionality for managing module settings. It includes methods to save and load settings, as
/// well as properties to track the state of the settings.
/// <para>
/// Properties will be saved and loaded only when they are registered via a <see cref="SettingDescriptor{T}"/>.
/// This class intentionally does not fall back to attribute-based persistence; only descriptors control
/// persistence and validation behavior.
/// </para>
/// <para>
/// The <see cref="IsLoaded"/> property indicates whether the settings have been successfully loaded, and the <see cref="IsDirty"/> property indicates whether the settings have been modified since they were last loaded or saved.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <![CDATA[
/// public class MyModuleSettings : ModuleSettings
/// {
///     // This setting requires a SettingDescriptor to be persisted.
///     public int MySetting { get; set; }
///
///     public MyModuleSettings(string moduleName)
///         : base(moduleName)
///     {
///     }
/// }
///
/// // Usage
/// var settingsManager = new EditorSettingsManager(context);
/// var mySettings = new MyModuleSettings("MyModule");
/// await mySettings.LoadAsync(settingsManager);
/// mySettings.MySetting = 42;
/// await mySettings.SaveAsync(settingsManager);
/// ]]>
/// </example>
public abstract class ModuleSettings(string moduleName) : INotifyPropertyChanged
{
    private readonly HashSet<string> modifiedProperties = new(StringComparer.Ordinal);
    private bool isLoaded;
    private bool isDirty = true;

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Gets the name of the module.
    /// </summary>
    [SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "helpful for debugging and UI display of settings")]
    public string ModuleName { get; } = moduleName;

    /// <summary>
    /// Gets or sets a value indicating whether the module settings have been loaded.
    /// </summary>
    public bool IsLoaded
    {
        get => this.isLoaded;
        protected set => this.SetProperty(ref this.isLoaded, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the module settings have been modified.
    /// </summary>
    public bool IsDirty
    {
        get => this.isDirty;
        protected set => this.SetProperty(ref this.isDirty, value);
    }

    /// <summary>
    /// Gets the collection of modified property names (for extension methods).
    /// </summary>
    /// <returns>The set of modified property names.</returns>
    internal IReadOnlyCollection<string> GetModifiedPropertiesInternal() => this.modifiedProperties;

    /// <summary>
    /// Clears the collection of modified property names (for extension methods).
    /// </summary>
    internal void ClearModifiedPropertiesInternal() => this.modifiedProperties.Clear();

    /// <summary>
    /// Sets the IsLoaded property (for extension methods).
    /// </summary>
    /// <param name="value">The new value for IsLoaded.</param>
    internal void SetIsLoadedInternal(bool value) => this.isLoaded = value;

    /// <summary>
    /// Sets the IsDirty property (for extension methods).
    /// </summary>
    /// <param name="value">The new value for IsDirty.</param>
    internal void SetDirtyInternal(bool value) => this.isDirty = value;

    /// <summary>
    /// Calls the OnSaving protected method (for extension methods).
    /// </summary>
    internal void OnSavingInternal() => this.OnSaving();

    /// <summary>
    /// Calls the OnLoaded protected method (for extension methods).
    /// </summary>
    internal void OnLoadedInternal() => this.OnLoaded();

    /// <summary>
    /// Sets the value of a property and raises the <see cref="PropertyChanged"/> event.
    /// </summary>
    /// <typeparam name="T">The type of the property value.</typeparam>
    /// <param name="field">The field storing the property value.</param>
    /// <param name="value">The new value of the property.</param>
    /// <param name="propertyName">The name of the property. This is optional and will be automatically set by the compiler.</param>
    protected void SetProperty<T>(ref T field, T value, [CallerMemberName] string propertyName = "")
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return;
        }

        // Validate the property value. For Point/Size validation, a dedicated attribute is used
        // (PointBoundsAttribute) instead of RangeAttribute which is not compatible with non-primitive value types.
        var validationContext = new ValidationContext(this) { MemberName = propertyName };
        Validator.ValidateProperty(value, validationContext);

        field = value;
        this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

        // Exclude our own properties from the modified properties list and do not let them
        // mark the object as dirty.
        if (propertyName is nameof(this.IsDirty) or nameof(this.IsLoaded))
        {
            return;
        }

        this.modifiedProperties.Add(propertyName);
        this.IsDirty = true;

        // If we're in a batch context, queue the change instead of persisting immediately
        var batch = Settings.SettingsBatch.Current;
        if (batch != null)
        {
            this.QueuePropertyChangeInBatch(propertyName, value, batch);
        }
    }

    /// <summary>
    /// Returns whether a property was marked as modified since the last save or load.
    /// </summary>
    /// <returns>True if the property was modified; otherwise false.</returns>
    /// <param name="propertyName">The name of the property to check.</param>
    protected bool IsPropertyModified(string propertyName) => this.modifiedProperties.Contains(propertyName);

    /// <summary>
    /// Clears the set of modified properties. This is typically done after a successful save.
    /// </summary>
    protected void ClearModifiedProperties() => this.modifiedProperties.Clear();

    /// <summary>
    /// Called before the settings are saved.
    /// </summary>
    [SuppressMessage("ReSharper", "VirtualMemberNeverOverridden.Global", Justification = "necessary for the derived classes")]
    protected virtual void OnSaving()
    {
    }

    /// <summary>
    /// Called after the settings are loaded.
    /// </summary>
    [SuppressMessage("ReSharper", "VirtualMemberNeverOverridden.Global", Justification = "necessary for the derived classes")]
    protected virtual void OnLoaded()
    {
    }

    /// <summary>
    /// Queues a property change in the active batch.
    /// </summary>
    /// <typeparam name="T">The type of the property value.</typeparam>
    /// <param name="propertyName">The name of the property.</param>
    /// <param name="value">The new value.</param>
    /// <param name="batch">The batch to queue the change in.</param>
    private void QueuePropertyChangeInBatch<T>(string propertyName, T? value, SettingsBatch batch)
    {
        var descriptorField = this.GetType()
            .GetNestedType("Descriptors", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static)
            ?.GetField(propertyName, BindingFlags.Public | BindingFlags.Static);

        if (descriptorField?.GetValue(null) is not SettingDescriptor<T> descriptor)
        {
            throw new InvalidOperationException(
                $"No descriptor found for property '{propertyName}' in {this.GetType().Name}. "
                + "Properties must have a corresponding descriptor in the nested Descriptors class to be persisted.");
        }

        _ = batch.QueuePropertyChange(descriptor, value!);
    }
}
