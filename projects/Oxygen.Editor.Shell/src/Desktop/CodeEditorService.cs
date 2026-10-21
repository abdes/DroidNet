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
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Desktop;

/// <summary>
/// Opens scripts and input files in the user's code editor; the choice is a per-user editor setting.
/// </summary>
/// <param name="settings">The editor settings store.</param>
/// <param name="dialogs">The dialog service that asks which editor to use.</param>
/// <param name="project">The project's folder, which VS Code opens as its workspace.</param>
/// <param name="operationResults">The operation-result publisher for launch failures.</param>
/// <param name="loggerFactory">The logger factory.</param>
public sealed partial class CodeEditorService(
    IEditorSettingsManager settings,
    IDialogService dialogs,
    IProjectFolder project,
    IOperationResultPublisher operationResults,
    ILoggerFactory? loggerFactory = null) : ICodeEditorService
{
    /// <summary>The code of a failure to start the code editor.</summary>
    public const string EditorLaunchFailed = DiagnosticCodes.ScriptPrefix + "EDITOR_LAUNCH_FAILED";

    /// <summary>The per-user code editor choice in the editor database.</summary>
    internal static readonly SettingKey<CodeEditorPreference> Key = new("CodeEditor", "Preference");

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
        var form = new ChoiceForm(Path.GetExtension(path), current);
        var spec = new DialogSpec("Open scripts with", form.Content)
        {
            PrimaryButtonText = "Open",
            CloseButtonText = "Cancel",
            DefaultButton = DialogButton.Primary,
            PrimaryAction = () => Task.FromResult(form.Validate()),
        };

        if (await dialogs.ShowAsync(spec).ConfigureAwait(true) != DialogButton.Primary)
        {
            return null;
        }

        var chosen = form.ToPreference(current);
        if (form.Remember)
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
            start = CodeEditorLaunch.Create(preference, path, line, column, project.Path, CodeEditorLaunch.FindVisualStudioCode());
            using var process = Process.Start(start);
            this.LogEditorStarted(path, start);
            return true;
        }
        catch (Exception failure) when (failure is Win32Exception or InvalidOperationException or FileNotFoundException)
        {
            var commandLine = start is null ? preference.Kind.ToString() : CodeEditorLaunch.Describe(start);
            this.PublishLaunchFailure(path, $"Could not open '{Path.GetFileName(path)}' with: {commandLine}. Choose another editor with Open with.", failure);
            return false;
        }
    }

    private void PublishLaunchFailure(string path, string message, Exception failure)
    {
        var operationId = Guid.NewGuid();
        operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = ScriptOperationKinds.OpenInCodeEditor,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = "Code editor did not start",
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = new AffectedScope { AssetSourcePath = path },
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.ScriptAuthoring,
                    Severity = DiagnosticSeverity.Error,
                    Code = EditorLaunchFailed,
                    Message = message,
                    TechnicalMessage = failure.Message,
                    ExceptionType = failure.GetType().FullName,
                    AffectedPath = path,
                },
            ],
        });
    }

    // The choices of the "Open scripts with" dialog, built on the window's thread that shows it.
    private sealed class ChoiceForm
    {
        private readonly RadioButton visualStudioCode;
        private readonly RadioButton custom;
        private readonly TextBox command;
        private readonly CheckBox remember;
        private readonly TextBlock error;

        public ChoiceForm(string extension, CodeEditorPreference? current)
        {
            var found = CodeEditorLaunch.FindVisualStudioCode();
            this.visualStudioCode = new RadioButton
            {
                Content = found is null ? "Visual Studio Code (not found)" : "Visual Studio Code",
                IsEnabled = found is not null,
            };
            var systemDefault = new RadioButton { Content = $"System default for {extension} files" };
            this.custom = new RadioButton { Content = "Custom command" };
            this.command = new TextBox
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
            this.remember = new CheckBox { Content = "Remember my choice", IsChecked = true };
            this.error = new TextBlock
            {
                TextWrapping = TextWrapping.Wrap,
                Visibility = Visibility.Collapsed,
                Foreground = Application.Current.Resources.TryGetValue("SystemFillColorCriticalBrush", out var brush) ? brush as Microsoft.UI.Xaml.Media.Brush : null,
            };

            var initial = current?.Kind ?? (found is null ? CodeEditorKind.SystemDefault : CodeEditorKind.VisualStudioCode);
            var selected = initial == CodeEditorKind.VisualStudioCode && found is null ? CodeEditorKind.SystemDefault : initial;
            this.visualStudioCode.IsChecked = selected == CodeEditorKind.VisualStudioCode;
            systemDefault.IsChecked = selected == CodeEditorKind.SystemDefault;
            this.custom.IsChecked = selected == CodeEditorKind.Custom;
            this.command.IsEnabled = selected == CodeEditorKind.Custom;
            this.custom.Checked += (_, _) => this.command.IsEnabled = true;
            this.custom.Unchecked += (_, _) => this.command.IsEnabled = false;

            var options = new StackPanel { Spacing = 4, Children = { this.visualStudioCode, systemDefault, this.custom, this.command, hint } };
            this.Content = new StackPanel { Spacing = 12, Children = { options, this.remember, this.error } };
        }

        public StackPanel Content { get; }

        public bool Remember => this.remember.IsChecked == true;

        private CodeEditorKind Kind => this.visualStudioCode.IsChecked == true ? CodeEditorKind.VisualStudioCode
            : this.custom.IsChecked == true ? CodeEditorKind.Custom
            : CodeEditorKind.SystemDefault;

        // A custom command needs its text; the error says so and keeps the dialog open.
        public bool Validate()
        {
            var invalid = this.Kind == CodeEditorKind.Custom && this.command.Text.Trim().Length == 0;
            this.error.Text = invalid ? "Enter the command that opens a file, for example " + CodeEditorLaunch.ExampleCustomCommand + "." : string.Empty;
            this.error.Visibility = invalid ? Visibility.Visible : Visibility.Collapsed;
            return !invalid;
        }

        public CodeEditorPreference ToPreference(CodeEditorPreference? current)
        {
            var kind = this.Kind;
            return new CodeEditorPreference(CodeEditorPreference.CurrentVersion, kind, kind == CodeEditorKind.Custom ? this.command.Text.Trim() : current?.CustomCommand);
        }
    }
}
