// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Represents the base class for module settings, providing functionality to save and load settings asynchronously.
/// </summary>
/// <remarks>
/// The <see cref="ModuleSettings"/> class is an abstract base class that provides common
/// functionality for managing module settings. It includes methods to save and load settings, as
/// well as properties to track the state of the settings.
/// <para>
/// Properties marked with the <see cref="PersistedAttribute"/> will be saved and loaded by the
/// <see cref="EditorSettingsManager"/>. These properties are automatically persisted to and retrieved from
/// the underlying storage when the <see cref="SaveAsync"/> and <see cref="LoadAsync"/> methods are
/// called.
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
///     [Persisted]
///     public int MySetting { get; set; }
///
///     public MyModuleSettings(EditorSettingsManager settingsManager, string moduleName)
///         : base(settingsManager, moduleName)
///     {
///     }
/// }
///
/// // Usage
/// var settingsManager = new EditorSettingsManager(context);
/// var mySettings = new MyModuleSettings(settingsManager, "MyModule");
/// await mySettings.LoadAsync();
/// mySettings.MySetting = 42;
/// await mySettings.SaveAsync();
/// ]]>
/// </example>
public abstract class ModuleSettings(IEditorSettingsManager settingsManager, string moduleName) : INotifyPropertyChanged
{
    private readonly HashSet<string> modifiedProperties = [];
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
    /// Gets a value indicating whether the settings have been loaded.
    /// </summary>
    public bool IsLoaded
    {
        get => this.isLoaded;
        private set => this.SetProperty(ref this.isLoaded, value);
    }

    /// <summary>
    /// Gets a value indicating whether the settings have been modified.
    /// </summary>
    public bool IsDirty
    {
        get => this.isDirty;
        private set => this.SetProperty(ref this.isDirty, value);
    }

    /// <summary>
    /// Saves the settings asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <exception cref="InvalidOperationException">Thrown if a property cannot be saved.</exception>
    public async Task SaveAsync()
    {
        this.OnSaving();

        foreach (var property in this.modifiedProperties)
        {
            var propertyInfo = this.GetType().GetProperty(property);
            if (propertyInfo?.GetCustomAttribute<PersistedAttribute>() != null)
            {
                try
                {
                    var value = propertyInfo.GetValue(this);
                    if (value != null)
                    {
                        await settingsManager.SaveSettingAsync(this.ModuleName, property, value).ConfigureAwait(true);
                    }
                }
                catch (Exception ex) when (ex is ArgumentException or TargetException or MethodAccessException or TargetInvocationException)
                {
                    throw new InvalidOperationException($"Failed to save property '{property}'", ex);
                }
            }
        }

        this.modifiedProperties.Clear();
        this.IsDirty = false;
    }

    /// <summary>
    /// Loads the settings asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <exception cref="InvalidOperationException">Thrown if a property cannot be loaded.</exception>
    public async Task LoadAsync()
    {
        this.IsLoaded = false;

        foreach (var property in this.GetType().GetProperties())
        {
            if (property.GetCustomAttribute<PersistedAttribute>() == null)
            {
                continue;
            }

            try
            {
                var defaultValue = property.GetValue(this);
                var loadSettingMethod = typeof(IEditorSettingsManager).GetMethod(nameof(IEditorSettingsManager.LoadSettingAsync), [typeof(string), typeof(string), property.PropertyType]);
                if (loadSettingMethod != null)
                {
                    var genericMethod = loadSettingMethod.MakeGenericMethod(property.PropertyType);
                    var task = genericMethod.Invoke(settingsManager, [this.ModuleName, property.Name, defaultValue]) as Task;
                    await task!.ConfigureAwait(true);

                    var resultProperty = task.GetType().GetProperty("Result");
                    if (resultProperty != null)
                    {
                        var value = resultProperty.GetValue(task);
                        property.SetValue(this, value);
                    }
                }
            }
            catch (Exception ex) when (ex is ArgumentException or TargetException or MethodAccessException or TargetInvocationException)
            {
                throw new InvalidOperationException($"Failed to load property '{property.Name}'", ex);
            }
        }

        this.OnLoaded();

        this.IsDirty = false;
        this.IsLoaded = true;
    }

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

        // Validate the property value
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
    }

    /// <summary>
    /// Called before the settings are saved.
    /// </summary>
    [SuppressMessage("ReSharper", "VirtualMemberNeverOverridden.Global", Justification = "necessary for the dervided classes")]
    protected virtual void OnSaving()
    {
    }

    /// <summary>
    /// Called after the settings are loaded.
    /// </summary>
    [SuppressMessage("ReSharper", "VirtualMemberNeverOverridden.Global", Justification = "necessary for the dervided classes")]
    protected virtual void OnLoaded()
    {
    }
}
