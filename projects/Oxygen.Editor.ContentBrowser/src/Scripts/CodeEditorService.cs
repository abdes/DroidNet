// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Dialogs;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Opens scripts and input files in the user's code editor; the choice is a per-user editor setting.
/// </summary>
/// <param name="settings">The editor settings store.</param>
/// <param name="dialogs">The dialog service that asks which editor to use.</param>
/// <param name="projects">The active project, whose folder VS Code opens as its workspace.</param>
/// <param name="operationResults">The operation-result publisher for launch failures.</param>
/// <param name="loggerFactory">The logger factory.</param>
public sealed partial class CodeEditorService(
    IEditorSettingsManager settings,
    IDialogService dialogs,
    IProjectContextService projects,
    IOperationResultPublisher operationResults,
    ILoggerFactory? loggerFactory = null) : ICodeEditorService
{
    /// <summary>The per-user code editor choice in the editor database.</summary>
    internal static readonly SettingKey<CodeEditorPreference> Key = new("ContentBrowser", "CodeEditor");

    private readonly ILogger logger = loggerFactory?.CreateLogger<CodeEditorService>() ?? NullLogger<CodeEditorService>.Instance;

    /// <inheritdoc />
    public async Task<bool> OpenAsync(string path, int? line = null, int? column = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var preference = await this.LoadAsync().ConfigureAwait(true) ?? await this.AskAsync(path, current: null).ConfigureAwait(true);
        return preference is not null && this.Launch(preference, path, line, column);
    }

    /// <inheritdoc />
    public async Task<bool> OpenWithAsync(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var current = await this.LoadAsync().ConfigureAwait(true);
        var preference = await this.AskAsync(path, current).ConfigureAwait(true);
        return preference is not null && this.Launch(preference, path, line: null, column: null);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "An unreadable preference is logged and asked again.")]
    private async Task<CodeEditorPreference?> LoadAsync()
    {
        try
        {
            var stored = await settings.LoadSettingAsync(Key, SettingContext.Application()).ConfigureAwait(true);
            if (stored is { IsValid: false })
            {
                this.LogPreferenceDiscarded(stored.Version);
                return null;
            }

            return stored;
        }
        catch (Exception failure)
        {
            this.LogPreferenceUnreadable(failure);
            return null;
        }
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed preference write is logged; the choice still applies to this launch.")]
    private async Task SaveAsync(CodeEditorPreference preference)
    {
        try
        {
            await settings.SaveSettingAsync(Key, preference, SettingContext.Application()).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.LogPreferenceNotSaved(failure);
        }
    }

    private async Task<CodeEditorPreference?> AskAsync(string path, CodeEditorPreference? current)
    {
        var visualStudioCode = CodeEditorLaunch.FindVisualStudioCode();
        var vsCode = new RadioButton
        {
            Content = visualStudioCode is null ? "Visual Studio Code (not found)" : "Visual Studio Code",
            IsEnabled = visualStudioCode is not null,
        };
        var extension = Path.GetExtension(path);
        var systemDefault = new RadioButton { Content = $"System default for {extension} files" };
        var custom = new RadioButton { Content = "Custom command" };
        var command = new TextBox
        {
            PlaceholderText = CodeEditorLaunch.ExampleCustomCommand,
            Text = current?.CustomCommand ?? string.Empty,
            Margin = new Thickness(28, 0, 0, 0),
            MinWidth = 360,
        };
        var hint = new TextBlock
        {
            Text = "Placeholders: {file}, {line}, {column}, {project}",
            Margin = new Thickness(28, 0, 0, 0),
            Opacity = 0.7,
            Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
        };
        var remember = new CheckBox { Content = "Remember my choice", IsChecked = true };
        var error = new TextBlock { TextWrapping = TextWrapping.Wrap, Visibility = Visibility.Collapsed };
        error.Foreground = Application.Current.Resources.TryGetValue("SystemFillColorCriticalBrush", out var brush) ? brush as Microsoft.UI.Xaml.Media.Brush : null;

        var initial = current?.Kind ?? (visualStudioCode is null ? CodeEditorKind.SystemDefault : CodeEditorKind.VisualStudioCode);
        var selected = initial == CodeEditorKind.VisualStudioCode && visualStudioCode is null ? CodeEditorKind.SystemDefault : initial;
        vsCode.IsChecked = selected == CodeEditorKind.VisualStudioCode;
        systemDefault.IsChecked = selected == CodeEditorKind.SystemDefault;
        custom.IsChecked = selected == CodeEditorKind.Custom;
        command.IsEnabled = selected == CodeEditorKind.Custom;
        custom.Checked += (_, _) => command.IsEnabled = true;
        custom.Unchecked += (_, _) => command.IsEnabled = false;

        var options = new StackPanel { Spacing = 4, Children = { vsCode, systemDefault, custom, command, hint } };
        var content = new StackPanel { Spacing = 12, Children = { options, remember, error } };

        CodeEditorPreference? chosen = null;
        var spec = new DialogSpec("Open scripts with", content)
        {
            PrimaryButtonText = "Open",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
            PrimaryAction = () =>
            {
                var kind = vsCode.IsChecked == true ? CodeEditorKind.VisualStudioCode
                    : custom.IsChecked == true ? CodeEditorKind.Custom
                    : CodeEditorKind.SystemDefault;
                var text = command.Text.Trim();
                var invalid = kind == CodeEditorKind.Custom && text.Length == 0;
                error.Text = invalid ? "Enter the command that opens a file, for example " + CodeEditorLaunch.ExampleCustomCommand + "." : string.Empty;
                error.Visibility = invalid ? Visibility.Visible : Visibility.Collapsed;
                chosen = invalid ? null : new CodeEditorPreference(CodeEditorPreference.CurrentVersion, kind, kind == CodeEditorKind.Custom ? text : current?.CustomCommand);
                return Task.FromResult(!invalid);
            },
        };

        if (await dialogs.ShowAsync(spec).ConfigureAwait(true) != DialogButton.Primary || chosen is null)
        {
            return null;
        }

        if (remember.IsChecked == true)
        {
            await this.SaveAsync(chosen).ConfigureAwait(true);
        }

        return chosen;
    }

    private bool Launch(CodeEditorPreference preference, string path, int? line, int? column)
    {
        ProcessStartInfo? start = null;
        try
        {
            start = CodeEditorLaunch.Create(preference, path, line, column, projects.ActiveProject?.ProjectRoot.FullPath, CodeEditorLaunch.FindVisualStudioCode());
            using var process = Process.Start(start);
            this.LogEditorStarted(path, start);
            return true;
        }
        catch (Exception failure) when (failure is Win32Exception or InvalidOperationException or FileNotFoundException)
        {
            var commandLine = start is null ? preference.Kind.ToString() : CodeEditorLaunch.Describe(start);
            ScriptOperationResults.PublishFailure(
                operationResults,
                ScriptOperationKinds.OpenInCodeEditor,
                ScriptDiagnosticCodes.EditorLaunchFailed,
                "Code editor did not start",
                $"Could not open '{Path.GetFileName(path)}' with: {commandLine}. Choose another editor with Open with.",
                path,
                failure);
            return false;
        }
    }
}
