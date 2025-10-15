// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1204 // Static elements should appear before instance elements

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Windows.System;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents an individual menu item control, used within a <see cref="MenuBar"/> or cascaded menu flyouts.
/// </summary>
public partial class MenuItem
{
    [LoggerMessage(
        EventId = 3501,
        Level = LogLevel.Error,
        Message = "[MenuItem: `{ItemId}`] Command execution failed")]
    private static partial void LogCommandExecutionFailed(ILogger logger, string itemId, Exception exception);

    private void LogCommandExecutionFailed(Exception ex)
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            LogCommandExecutionFailed(logger, this.ItemData.Id, ex);
        }
    }

    [LoggerMessage(
        EventId = 3502,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] Transitioning to visual state `{State}` (IsExpanded={IsExpanded})")]
    private static partial void LogVisualState(ILogger logger, string itemId, string state, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogVisualState(string state)
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.VisualLogger is ILogger logger)
        {
            var data = this.ItemData;
            LogVisualState(logger, data.Id, state, data.IsExpanded);
        }
    }

    [LoggerMessage(
        EventId = 3503,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] {State} (IsExpanded={IsExpanded})")]
    private static partial void LogFocusState(ILogger logger, string state, string itemId, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogFocusState()
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            var state = this.FocusState switch
            {
                Microsoft.UI.Xaml.FocusState.Unfocused => "Unfocused",
                Microsoft.UI.Xaml.FocusState.Pointer => "Pointer Focused",
                Microsoft.UI.Xaml.FocusState.Keyboard => "Keyboard Focused",
                Microsoft.UI.Xaml.FocusState.Programmatic => "Programmatic Focused",
                _ => "Unknown Focus State",
            };
            var data = this.ItemData;
            LogFocusState(logger, state, data.Id, data.IsExpanded);
        }
    }

    [LoggerMessage(
        EventId = 3504,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] `{Key}` Key {EventType} (IsExpanded={IsExpanded})")]
    private static partial void LogKeyEvent(ILogger logger, VirtualKey key, string eventType, string itemId, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogKeyEvent(VirtualKey key, string eventType)
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            var data = this.ItemData;
            LogKeyEvent(logger, key, eventType, data.Id, data.IsExpanded);
        }
    }

    [LoggerMessage(
        EventId = 3505,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] Tapped (IsExpanded={IsExpanded})")]
    private static partial void LogTapped(ILogger logger, string itemId, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogTapped()
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            var data = this.ItemData;
            LogTapped(logger, data.Id, data.IsExpanded);
        }
    }

    [LoggerMessage(
        EventId = 3505,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] Checked={IsChecked}")]
    private static partial void LogChecked(ILogger logger, string itemId, bool isChecked);

    [Conditional("DEBUG")]
    private void LogChecked()
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            var data = this.ItemData;
            LogChecked(logger, data.Id, data.IsChecked);
        }
    }

    [LoggerMessage(
        EventId = 3506,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] Pointer {EventType} (IsPointerOver={IsPointerOver}, IsPressed={IsPressed}, IsExpanded={IsExpanded}, IsEnabled={IsEnabled})")]
    private static partial void LogPointerEvent(ILogger logger, string itemId, string eventType, bool isPointerOver, bool isPressed, bool isExpanded, bool isEnabled);

    [Conditional("DEBUG")]
    private void LogPointerEvent(string eventType)
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            var data = this.ItemData;
            LogPointerEvent(
                logger,
                data.Id,
                eventType,
                this.isPointerOver,
                this.isPressed,
                data.IsExpanded,
                data.IsEnabled);
        }
    }

    [LoggerMessage(
        EventId = 3507,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] Mnemonic visibility toggled (WasVisible={WasVisible}, NowVisible={IsVisible}, IsExpanded={IsExpanded})")]
    private static partial void LogMnemonicVisibility(ILogger logger, string itemId, bool wasVisible, bool isVisible, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogMnemonicVisibility(bool wasVisible)
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.VisualLogger is ILogger logger)
        {
            var data = this.ItemData;
            var isVisible = !wasVisible;
            LogMnemonicVisibility(logger, data.Id, wasVisible, isVisible, data.IsExpanded);
        }
    }

    [LoggerMessage(
        EventId = 3508,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] Submenu requested via {InputSource} (HasChildren={HasChildren}, IsExpanded={IsExpanded})")]
    private static partial void LogSubmenuRequestedEvent(ILogger logger, string itemId, MenuInteractionInputSource inputSource, bool hasChildren, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogSubmenuRequestedEvent(MenuInteractionInputSource inputSource)
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            var data = this.ItemData;
            LogSubmenuRequestedEvent(logger, data.Id, inputSource, data.HasChildren, data.IsExpanded);
        }
    }

    [LoggerMessage(
        EventId = 3509,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] Invoked via {InputSource} (HasCommand={HasCommand}, CanExecute={CanExecute}, IsCheckable={IsCheckable}, IsChecked={IsChecked})")]
    private static partial void LogInvokedEvent(ILogger logger, string itemId, MenuInteractionInputSource inputSource, bool hasCommand, bool canExecute, bool isCheckable, bool isChecked);

    [Conditional("DEBUG")]
    private void LogInvokedEvent(MenuInteractionInputSource inputSource)
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            var data = this.ItemData;
            var command = data.Command;
            var hasCommand = command is not null;
            var canExecute = command?.CanExecute(data) == true;
            LogInvokedEvent(logger, data.Id, inputSource, hasCommand, canExecute, data.IsCheckable, data.IsChecked);
        }
    }

    [LoggerMessage(
        EventId = 3510,
        Level = LogLevel.Trace,
        Message = "[MenuItem: `{ItemId}`] Radio group selection requested for `{GroupId}` (IsChecked={IsChecked}, IsExpanded={IsExpanded})")]
    private static partial void LogRadioGroupSelectionEvent(ILogger logger, string itemId, string groupId, bool isChecked, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogRadioGroupSelection()
    {
        Debug.Assert(this.ItemData is not null, "Expecting ItemData to be non-null when logging");
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            var data = this.ItemData;
            LogRadioGroupSelectionEvent(logger, data.Id, data.RadioGroupId ?? "__NULL__", data.IsChecked, data.IsExpanded);
        }
    }
}
