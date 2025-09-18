//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/Input/api_export.h>
#include <Oxygen/Platform/Platform.h>

namespace oxygen::input {
class Action;
class InputMappingContext;
} // namespace oxygen::input

namespace oxygen::engine {

class InputSystem : public EngineModule, public Composition, public Named {
  OXYGEN_TYPED(InputSystem)
public:
  struct InputMappingContextEntry {
    int32_t priority;
    bool is_active { false };
    std::shared_ptr<input::InputMappingContext> mapping_context;
  };

  OXGN_NPUT_API explicit InputSystem(std::shared_ptr<Platform> platform);
  OXGN_NPUT_API ~InputSystem() override = default;

  OXYGEN_MAKE_NON_COPYABLE(InputSystem)
  OXYGEN_MAKE_NON_MOVABLE(InputSystem)

  // Metadata
  OXGN_NPUT_NDAPI auto GetName() const noexcept -> std::string_view override;

  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    return ModulePriority { kModulePriorityHighest };
  }
  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask override
  {
    return MakeModuleMask<core::PhaseId::kInput, core::PhaseId::kFrameStart,
      core::PhaseId::kFrameEnd>();
  }

  [[nodiscard]] virtual auto IsCritical() const noexcept -> bool
  {
    return true;
  }

  void SetName(std::string_view name) noexcept;

  OXGN_NPUT_NDAPI auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool override;

  OXGN_NPUT_API void AddAction(const std::shared_ptr<input::Action>& action);
  OXGN_NPUT_API void RemoveAction(const std::shared_ptr<input::Action>& action);
  OXGN_NPUT_API void ClearAllActions();
  [[nodiscard]] OXGN_NPUT_API auto GetActionByName(std::string_view name) const
    -> std::shared_ptr<input::Action>;

  OXGN_NPUT_API void AddMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context,
    int32_t priority);
  OXGN_NPUT_API void RemoveMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context);
  OXGN_NPUT_API void ClearAllMappingContexts();
  [[nodiscard]] OXGN_NPUT_API auto GetMappingContextByName(
    std::string_view name) const -> std::shared_ptr<input::InputMappingContext>;
  OXGN_NPUT_API void ActivateMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context);
  OXGN_NPUT_API void DeactivateMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context);

  // OXGN_NPUT_API void Update(const engine::SystemUpdateContext&
  // update_context);

private:
  void ProcessInputEvent(std::shared_ptr<platform::InputEvent> event);
  void HandleInput(
    const platform::InputSlot& slot, const platform::InputEvent& event);

  std::vector<std::shared_ptr<input::Action>> actions_;
  std::list<InputMappingContextEntry> mapping_contexts_;

  std::shared_ptr<Platform> platform_;
};

} // namespace oxygen::engine

// Variables

// Name	Description
// Public variable	TArray< struct ... 	ActionMappings	This player's
// version of the Action Mappings

// Public variable	TArray< struct ... 	AxisConfig	This player's
// version of the Axis Properties

// Public variable	TArray< struct ... 	AxisMappings	This player's
// version of Axis Mappings

// Public variable	TArray< struct ... 	DebugExecBindings	Generic
// bindings of keys to Exec()-compatible strings for development purposes only

// Public variable	TArray< FName > 	InvertedAxis	List of Axis
// Mappings that have been inverted

// Public variable	int32 	MouseSamples	Current average mouse
// movement/sample

// Public variable	float 	MouseSamplingTotal	Number of mouse samples
// since mouse movement has been zero

// Public variable	float[2] 	SmoothedMouse	How long received mouse
// movement has been zero.

// Public variable	FVector[EKeys::... 	Touches	Touch locations, from
// 0..1 (0,0 is top left, 1,1 is bottom right), the Z component is > 0 if the
// touch is currently held down

// Public variable	TMap< uint32, F... 	TouchEventLocations	Used to
// store paired touch locations for event ids during the frame and flushed when
// processed.

// Public variable	float[2] 	ZeroTime	Mouse smoothing sample
// data.

// Public function	void 	AddActionMapping
// (
//     const FInputActionKeyMapping& KeyM...
// )
// Add a player specific action mapping.
// Public function	void 	AddAxisMapping
// (
//     const FInputAxisKeyMapping& KeyMap...
// )
// Add a player specific axis mapping.
// Public function Static	void 	AddEngineDefinedActionMapping
// (
//     const FInputActionKeyMapping& Acti...
// )
// Add an core defined action mapping that cannot be remapped.
// Public function Static	void 	AddEngineDefinedAxisMapping
// (
//     const FInputAxisKeyMapping& AxisMa...
// )
// Add an core defined axis mapping that cannot be remapped.
// Public function	void 	ClearSmoothing()
// Exec function to reset mouse smoothing values
// Protected function	void 	ConditionalInitAxisProperties()
// Initialized axis properties (i.e deadzone values) if needed
// Public function	void 	DiscardPlayerInput()
// Rather than processing input, consume it and discard without doing anything
// useful with it. Public function Virtual	void 	DisplayDebug
// (
//     UCanvas* Canvas,
//     const FDebugDisplayInfo& DebugDisp...,
//     float& YL,
//     float& YPos
// )
// Draw important PlayerInput variables on canvas.
// Public function	bool 	Exec
// (
//     UWorld* UInWorld,
//     const TCHAR* Cmd,
//     FOutputDevice& Ar
// )
// Exec handler
// Public function	bool 	ExecInputCommands
// (
//     UWorld* InWorld,
//     const TCHAR* Cmd,
//     FOutputDevice& Ar
// )
// Execute input commands within the legacy key binding system.
// Public function	void 	FlushPressedActionBindingKeys
// (
//     FName ActionName
// )
// Flushes the current key state of the keys associated with the action name
// passed in Public function	void 	FlushPressedKeys() Flushes the current
// key state. Public function	void 	ForceRebuildingKeyMaps
// (
//     const bool bRestoreDefaults
// )
// Clear the current cached key maps and rebuild from the source arrays.
// Public function	bool 	GetAxisProperties
// (
//     const FKey AxisKey,
//     FInputAxisProperties& AxisProperti...
//     FInputAxisProperties& AxisProperti...
// )
// Gets the axis properties for a given AxisKey.
// Public function	FString 	GetBind
// (
//     FKey Key
// )
// Returns the command for a given key in the legacy binding system
// Public function Static	const TArray...
// GetEngineDefinedActionMappings() Public function Static	const TArray...
// GetEngineDefinedAxisMappings() Public function	FKeyBind
// GetExecBind
// (
//     FString const& ExecCommand
// )
// Get the legacy Exec key binding for the given command.
// Public function	bool 	GetInvertAxis
// (
//     const FName AxisName
// )
// Returns whether an Axis Mapping is inverted
// Public function	bool 	GetInvertAxisKey
// (
//     const FKey AxisKey
// )
// Returns whether an Axis Key is inverted
// Public function Const	uint32 	GetKeyMapBuildIndex()
// Public function Const	const TArray... 	GetKeysForAction
// (
//     const FName ActionName
// )
// Returns the list of keys mapped to the specified Action Name
// Public function Const	const TArray... 	GetKeysForAxis
// (
//     const FName AxisName
// )
// Returns the list of keys mapped to the specified Axis Name
// Public function Const	const FKeySt... 	GetKeyState
// (
//     FKey InKey
// )
// Public function	FKeyState &#... 	GetKeyState
// (
//     FKey InKey
// )
// Protected function	TMap< FKey, ... 	GetKeyStateMap()
// Public function Const	float 	GetKeyValue
// (
//     FKey InKey
// )
// Public function	float 	GetMouseSensitivityX()
// Returns the mouse sensitivity along the X-axis, or the Y-axis, or 1.0 if none
// are known. Public function	float 	GetMouseSensitivityY() Returns the mouse
// sensitivity along the Y-axis, or 1.0 if none are known. Public function Const
// FVector 	GetProcessedVectorKeyValue
// (
//     FKey InKey
// )
// Public function Const	float 	GetRawKeyValue
// (
//     FKey InKey
// )
// Public function Const	FVector 	GetRawVectorKeyValue
// (
//     FKey InKey
// )
// Public function Const	float 	GetTimeDown
// (
//     FKey InKey
// )
// Public function Virtual Const	UWorld * 	GetWorld()
// Public function	bool 	InputGesture
// (
//     const FKey Gesture,
//     const EInputEvent Event,
//     const float Value
// )
// Handles a gesture input event. Returns true.
// Public function Virtual	bool 	InputKey
// (
//     const FInputKeyParams& Params
// )
// Handles a key input event. Returns true if there is an action that handles
// the specified key. Public function	bool 	InputMotion
// (
//     const FVector& Tilt,
//     const FVector& RotationRate,
//     const FVector& Gravity,
//     const FVector& Acceleration
// )
// Handles a motion input event. Returns true.
// Public function	bool 	InputTouch
// (
//     uint32 Handle,
//     ETouchType::Type Type,
//     const FVector2D& TouchLocation,
//     float Force,
//     FDateTime DeviceTimestamp,
//     uint32 TouchpadIndex
// )
// Handles a touch input event. Returns true.
// Public function	void 	InvertAxis
// (
//     const FName AxisName
// )
// Exec function to invert an axis mapping
// Public function	void 	InvertAxisKey
// (
//     const FKey AxisKey
// )
// Exec function to invert an axis key
// Public function Const	bool 	IsAltPressed()
// Public function Const	bool 	IsCmdPressed()
// Public function Const	bool 	IsCtrlPressed()
// Protected function Virtual Const	bool 	IsKeyHandledByAction
// (
//     FKey Key
// )
// Public function Const	bool 	IsPressed
// (
//     FKey InKey
// )
// Public function Const	bool 	IsShiftPressed()
// Protected function Virtual	float 	MassageAxisInput
// (
//     FKey Key,
//     float RawValue
// )
// Given raw keystate value, returns the "massaged" value.
// Protected function Virtual	FVector 	MassageVectorAxisInput
// (
//     FKey Key,
//     FVector RawValue
// )
// Given raw keystate value of a vector axis, returns the "massaged" value.
// Public function Virtual	void 	ProcessInputStack
// (
//     const TArray< UInputComponent*...,
//     const float DeltaTime,
//     const bool bGamePaused
// )
// Process the frame's input events given the current input component stack.
// Public function	void 	RemoveActionMapping
// (
//     const FInputActionKeyMapping& KeyM...
// )
// Remove a player specific action mapping.
// Public function	void 	RemoveAxisMapping
// (
//     const FInputAxisKeyMapping& KeyMap...
// )
// Remove a player specific axis mapping.
// Public function	void 	SetAxisProperties
// (
//     const FKey AxisKey,
//     const FInputAxisProperties& AxisPr...
// )
// Gets the axis properties for a given AxisKey.
// Public function	void 	SetBind
// (
//     FName BindName,
//     const FString& Command
// )
// Exec function to add a debug exec command
// Public function	void 	SetMouseSensitivity
// (
//     const float Sensitivity
// )
// Sets both X and Y axis sensitivity to the supplied value.
// Public function	void 	SetMouseSensitivity
// (
//     const float SensitivityX,
//     const float SensitivityY
// )
// Exec function to change the mouse sensitivity
// Public function Virtual	float 	SmoothMouse
// (
//     float aMouse,
//     uint8& SampleCount,
//     int32 Index
// )
// Smooth mouse movement, because mouse sampling doesn't match up with tick
// time. Public function	void 	Tick
// (
//     float DeltaTime
// )
// Per frame tick function. Primarily for gesture recognition
// Public function	void 	UpdatePinchStartDistance()
// Manually update the GestureRecognizer AnchorDistance using the current
// locations of the touches Public function Const	bool 	WasJustPressed
// (
//     FKey InKey
// )
// Public function Const	bool 	WasJustReleased
// (
//     FKey InKey
// )
// Return true if InKey went from down to up since player input was last
// processed.
