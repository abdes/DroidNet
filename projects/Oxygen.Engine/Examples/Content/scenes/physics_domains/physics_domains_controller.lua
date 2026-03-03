local script = {}

local input = oxygen.input
local log = oxygen.log
local input_test_wired = false

local function wire_input_once()
  if input_test_wired then return end
  local function bind_hold(action_name)
    input.on_action(action_name, input.edges.triggered, function()
      log.info(">>>>>> [INPUT TEST] PRESSED: " .. action_name .. " <<<<<<")
    end)
    local function stop()
      log.info("<<<<<< [INPUT TEST] RELEASED: " .. action_name .. " >>>>>>")
    end
    input.on_action(action_name, input.edges.completed, stop)
    input.on_action(action_name, input.edges.canceled, stop)
  end

  bind_hold("VehicleForwardAction")
  bind_hold("VehicleReverseAction")
  bind_hold("VehicleSteerLeftAction")
  bind_hold("VehicleSteerRightAction")
  bind_hold("VehicleBrakeAction")
  bind_hold("VehicleHandbrakeAction")

  input_test_wired = true
  log.info("[INPUT TEST] Inputs successfully wired. Waiting for key presses...")
end

function script.on_gameplay(_ctx, _dt_seconds)
  wire_input_once()
end

return script
