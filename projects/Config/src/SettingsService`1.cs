// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

using System.ComponentModel;
using System.IO.Abstractions;
using System.Reactive.Linq;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;

/// <summary>
/// Provides an abstract base class for managing settings in a configuration section corresponding
/// the type <typeparamref name="TSettings" />. Provides common functionality for property change
/// notifications, monitoring of the settings using <see cref="IOptionsMonitor{TOptions}" />, and
/// serialization of the live settings to a Json file.
/// </summary>
/// <typeparam name="TSettings">The type of the settings.</typeparam>
/// <remarks>
/// Implementations of this interface are expected to provide the following functionality:
/// <list>
/// <item>
/// <term>Property Change Notifications</term>
/// <description>
/// Implementations must notify listeners when any property of the settings object changes. This can
/// be achieved by raising the <see cref="INotifyPropertyChanged.PropertyChanged" /> event whenever
/// a property is updated. The property setters should invoke this event, and a method like
/// <see cref="SetField{T}" /> can be used to compare old and new values before triggering the
/// notification.
/// </description>
/// </item>
/// <item>
/// <term>Disposable Functionality</term>
/// <description>
/// Implementations must manage resources properly by implementing the <see cref="IDisposable" />
/// interface. This includes disposing of any subscriptions or unmanaged resources and suppressing
/// finalization. A typical implementation should override the <see cref="IDisposable.Dispose" />
/// method to dispose of resources and mark the instance as disposed to prevent further usage.
/// </description>
/// </item>
/// </list>
/// </remarks>
public abstract partial class SettingsService<TSettings> : ISettingsService<TSettings>
    where TSettings : class
{
    /// <summary>
    /// By default, the serialized Json has the numeric value of the enums. Using the custom
    /// converter will make it serialize the enum name instead, using the original case of the enum
    /// value.
    /// </summary>
    private readonly JsonSerializerOptions jsonSerializerOptions = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter(new OriginalCaseNamingPolicy(), allowIntegerValues: false) },
    };

    private readonly ILogger logger;
    private readonly IFileSystem fs;
    private readonly IDisposable settingsChangedSubscription;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsService{TSettings}" /> class.
    /// </summary>
    /// <param name="settingsMonitor">The settings monitor.</param>
    /// <param name="fs">The file system.</param>
    /// <param name="loggerFactory">
    /// The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    /// cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    protected SettingsService(
        IOptionsMonitor<TSettings> settingsMonitor,
        IFileSystem fs,
        ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<SettingsService<TSettings>>() ??
                      NullLoggerFactory.Instance.CreateLogger<SettingsService<TSettings>>();
        this.fs = fs;

        // Throttle because the implementation of Microsoft.Extensions.Configuration fires multiple
        // times for the same change
        // https://github.com/dotnet/runtime/issues/109445
        IDisposable? onChangeToken = null;
        this.settingsChangedSubscription = Observable.FromEvent<Action<TSettings, string?>, Tuple<TSettings, string?>>(
                conversion: rxOnNext => (settings, name) => rxOnNext(Tuple.Create(settings, name)),
                addHandler: handler => onChangeToken = settingsMonitor.OnChange(handler),
                removeHandler: _ => onChangeToken?.Dispose())
            .Throttle(TimeSpan.FromMilliseconds(500))
            .Subscribe(t => this.UpdateProperties(t.Item1));

        /*
         * Note: we're using the OnChange(TSettings, string?) version because Moq cannot mock
         * extension methods. We are also doing a conversion of the arguments to a tuple, because Rx
         * cannot handle multiple arguments.
         */
    }

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc />
    public bool IsDirty { get; private set; }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        if (this.IsDirty)
        {
            this.LogDisposedWhileDirty(typeof(TSettings).Name);
        }

        this.isDisposed = true;
        this.settingsChangedSubscription.Dispose();
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    /// <remarks>
    /// This implementation uses Json as the serialization format, and places the settings
    /// properties in a section named with the type name of the <typeparamref name="TSettings" />
    /// type parameter.
    /// <para>
    /// If the configuration file exists, it will be silently overwritten. If any directories in the
    /// configuration file path do not exist, they will be created.
    /// </para>
    /// </remarks>
    public bool SaveSettings()
    {
        if (!this.IsDirty)
        {
            return true;
        }

        var settings = this.GetSettingsSnapshot();
        var configFilePath = this.GetConfigFilePath();
        var sectionName = this.GetConfigSectionName();

        try
        {
            string configText;
            if (string.IsNullOrEmpty(sectionName))
            {
                // No section name, just serialize the settings directly
                configText = JsonSerializer.Serialize(settings, this.jsonSerializerOptions);
            }
            else
            {
                // Wrap the settings inside the section name
                var configObject = new Dictionary<string, object>(StringComparer.Ordinal) { { sectionName, settings } };
                configText = JsonSerializer.Serialize(configObject, this.jsonSerializerOptions);
            }

            // Ensure all directories in the path exist
            var directoryPath = this.fs.Path.GetDirectoryName(configFilePath);
            if (!Directory.Exists(directoryPath))
            {
                this.fs.Directory.CreateDirectory(configFilePath);
            }

            this.fs.File.WriteAllText(configFilePath, configText);

            this.IsDirty = false;
        }
        catch (Exception ex)
        {
            this.LogSettingsSaveError(typeof(TSettings).Name, configFilePath, ex);
            return false;
        }

        this.LogSettingsSaved(typeof(TSettings).Name, configFilePath);
        return true;
    }

    /// <summary>
    /// Updates the properties of the settings. Called when the <see cref="IOptionsMonitor{TOptions}">
    /// options monitor</see> reports changes to the configuration, after it is reloaded.
    /// Implementations of this class are expected to update their properties, with change
    /// notifications, using the values provided in <paramref name="newSettings" />.
    /// </summary>
    /// <param name="newSettings">The new settings.</param>
    protected abstract void UpdateProperties(TSettings newSettings);

    /// <summary>
    /// Gets the full path to the configuration file. The file may or may not exist, and when it
    /// exists, it will be silently overwritten. Non-existing directories in the path will be
    /// created as needed.
    /// </summary>
    /// <returns>The configuration file path.</returns>
    protected abstract string GetConfigFilePath();

    /// <summary>
    /// Gets the section name corresponding to the settings of type <typeparamref name="TSettings" />
    /// within the configuration file.
    /// </summary>
    /// <returns>The section name to use or <see langword="null" /> to not use a section.</returns>
    protected abstract string? GetConfigSectionName();

    /// <summary>
    /// Sets the field and notifies listeners of the property change.
    /// </summary>
    /// <typeparam name="T">The type of the field.</typeparam>
    /// <param name="field">The field to set.</param>
    /// <param name="value">The value to set.</param>
    /// <param name="propertyName">The name of the property. This is optional and can be omitted.</param>
    /// <returns>
    /// <see langword="true" /> if the field was set; otherwise, <see langword="false" />.
    /// </returns>
    protected bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        this.IsDirty = true;
        this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
    }

    /// <summary>
    /// Gets a snapshot of the current settings. Future modifications of the settings will not be
    /// reflected in the returned snapshot.
    /// </summary>
    /// <returns>A snapshot of the settings.</returns>
    protected abstract TSettings GetSettingsSnapshot();

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "SettingsService for `{ConfigName}` disposed of while dirty")]
    private partial void LogDisposedWhileDirty(string configName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Settings for `{ConfigName}` saved to file `{ConfigFilePath}`")]
    private partial void LogSettingsSaved(string configName, string configFilePath);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to save settings for `{ConfigName}` to file `{ConfigFilePath}`")]
    private partial void LogSettingsSaveError(string configName, string configFilePath, Exception error);

    /// <summary>
    /// Represents a naming policy, for the <see cref="JsonSerializer" /> that retains the original
    /// case of the enum names.
    /// </summary>
    private sealed class OriginalCaseNamingPolicy : JsonNamingPolicy
    {
        /// <inheritdoc />
        public override string ConvertName(string name) => name;
    }
}
