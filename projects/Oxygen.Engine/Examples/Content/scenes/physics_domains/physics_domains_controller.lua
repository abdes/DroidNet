local script = oxygen.script {
  params = {
    forward = oxygen.param.asset { kind = "input_action",
      description = "Action that drives forward" },
    reverse = oxygen.param.asset { kind = "input_action",
      description = "Action that drives in reverse" },
    steer_left = oxygen.param.asset { kind = "input_action",
      description = "Action that steers left" },
    steer_right = oxygen.param.asset { kind = "input_action",
      description = "Action that steers right" },
    brake = oxygen.param.asset { kind = "input_action",
      description = "Action that brakes" },
    hand_brake = oxygen.param.asset { kind = "input_action",
      description = "Action that pulls the hand brake" },
  },
}

local input = oxygen.input
local log = oxygen.log
local scene = oxygen.scene
local vehicle_api = oxygen.physics.vehicle

local CONTROL_LOG_INTERVAL_SECONDS = 1.0
local HOLD_LATCH_FRAMES = 2

local vehicle_node = nil
local vehicle_handle = nil
local vehicle_wait_logged = false
local resolve_probe_logged = false
local control_log_timer = 0.0
local last_non_zero_input = false

local control_state = {
  forward = false,
  reverse = false,
  steer_left = false,
  steer_right = false,
  brake = false,
  hand_brake = false,
}

local control_latch_frames = {
  forward = 0,
  reverse = 0,
  steer_left = 0,
  steer_right = 0,
  brake = 0,
  hand_brake = 0,
}

local function is_node_alive(node)
  return node ~= nil and node:is_alive()
end

local function node_name_or_fallback(node)
  if not is_node_alive(node) then
    return "<invalid>"
  end
  local name = node:get_name()
  if type(name) == "string" and name ~= "" then
    return name
  end
  return "<unnamed>"
end

local function find_node_by_name_from_scene_roots(target_name)
  local roots = scene.root_nodes()
  if type(roots) ~= "table" then
    return nil, 0
  end

  local stack = {}
  for i = #roots, 1, -1 do
    local root = roots[i]
    if is_node_alive(root) then
      stack[#stack + 1] = root
    end
  end

  local visited_count = 0
  while #stack > 0 do
    local node = stack[#stack]
    stack[#stack] = nil
    if is_node_alive(node) then
      visited_count = visited_count + 1
      if node:get_name() == target_name then
        return node, visited_count
      end
      local children = node:get_children()
      if type(children) == "table" then
        for i = #children, 1, -1 do
          local child = children[i]
          if is_node_alive(child) then
            stack[#stack + 1] = child
          end
        end
      end
    end
  end

  return nil, visited_count
end

local function resolve_vehicle_node()
  if is_node_alive(vehicle_node) then
    return vehicle_node
  end

  vehicle_node = nil
  vehicle_handle = nil

  local found_node, visited_count = find_node_by_name_from_scene_roots("Vehicle")
  if is_node_alive(found_node) then
    vehicle_node = found_node
    log.info(
      "[INPUT TEST] Resolved vehicle node='" .. node_name_or_fallback(vehicle_node)
      .. "' via scene.root_nodes traversal (visited=" .. tostring(visited_count) .. ").")
    return vehicle_node
  end

  if not resolve_probe_logged then
    resolve_probe_logged = true
    log.info(
      "[INPUT TEST] Vehicle node pending (scene traversal visited="
      .. tostring(visited_count) .. ").")
  end
  return nil
end

local function resolve_vehicle_handle()
  if vehicle_handle ~= nil and is_node_alive(vehicle_node) then
    return vehicle_handle
  end
  if not is_node_alive(vehicle_node) then
    vehicle_handle = nil
  end

  local node = resolve_vehicle_node()
  if node == nil then
    if not vehicle_wait_logged then
      vehicle_wait_logged = true
      log.info("[INPUT TEST] Vehicle node/physics binding pending hydration.")
    end
    return nil
  end

  local handle = vehicle_api.get_exact(node)
  if handle == nil then
    handle = vehicle_api.find_in_ancestors(node)
  end
  if handle == nil then
    if not vehicle_wait_logged then
      vehicle_wait_logged = true
      log.info("[INPUT TEST] Vehicle physics handle pending hydration.")
    end
    return nil
  end

  vehicle_wait_logged = false
  vehicle_handle = handle
  local authority = vehicle_api.get_authority(vehicle_handle)
  log.info(
    "[INPUT TEST] Vehicle handle resolved; authority=" .. tostring(authority) .. ".")
  return vehicle_handle
end

local function compute_control_input()
  local forward = (control_state.forward and 1.0 or 0.0)
    - (control_state.reverse and 1.0 or 0.0)
  local steering = (control_state.steer_right and 1.0 or 0.0)
    - (control_state.steer_left and 1.0 or 0.0)
  local brake = control_state.brake and 1.0 or 0.0
  local hand_brake = control_state.hand_brake and 1.0 or 0.0
  return {
    forward = forward,
    steering = steering,
    brake = brake,
    hand_brake = hand_brake,
  }
end

local function set_control_flag(state_key, active, edge_name)
  local current = control_state[state_key]
  if current == active then
    return
  end
  control_state[state_key] = active
  log.info("[INPUT TEST] " .. state_key .. " -> " .. edge_name .. " (active=" .. tostring(active) .. ")")
end

local function tick_control_latches()
  for state_key, frames in pairs(control_latch_frames) do
    if frames > 0 then
      control_latch_frames[state_key] = frames - 1
      control_state[state_key] = true
    else
      control_state[state_key] = false
    end
  end
end

local function apply_vehicle_control()
  local handle = resolve_vehicle_handle()
  if handle == nil then
    return
  end

  local control_input = compute_control_input()
  local ok = vehicle_api.set_control_input(handle, control_input)
  if ok ~= true then
    vehicle_handle = nil
    vehicle_wait_logged = false
    log.warn("[INPUT TEST] vehicle.set_control_input rejected; will re-resolve handle.")
    return
  end

  local state = vehicle_api.get_state(handle)
  if type(state) == "table" then
    local non_zero_input = (math.abs(control_input.forward) > 0.0)
      or (math.abs(control_input.steering) > 0.0)
      or (control_input.brake > 0.0)
      or (control_input.hand_brake > 0.0)
    local input_transition = (non_zero_input ~= last_non_zero_input)
    local periodic = control_log_timer >= CONTROL_LOG_INTERVAL_SECONDS
      and (non_zero_input or last_non_zero_input)
    if input_transition or periodic then
      control_log_timer = 0.0
      log.info(
        "[INPUT TEST] control ok input={f="
        .. string.format("%.2f", control_input.forward)
        .. ", s=" .. string.format("%.2f", control_input.steering)
        .. ", b=" .. string.format("%.2f", control_input.brake)
        .. ", hb=" .. string.format("%.2f", control_input.hand_brake)
        .. "} state={speed="
        .. string.format("%.3f", state.forward_speed_mps or 0.0)
        .. ", grounded=" .. tostring(state.grounded) .. "}")
    end
    last_non_zero_input = non_zero_input
  else
    log.warn("[INPUT TEST] vehicle.get_state returned nil after set_control_input success.")
  end
end

local function wire_input(params)
  local function bind_hold(state_key)
    local action = params[state_key]
    if action == nil then
      return
    end
    local function engage(edge_name)
      control_latch_frames[state_key] = HOLD_LATCH_FRAMES
      set_control_flag(state_key, true, edge_name)
    end
    local function release(edge_name)
      control_latch_frames[state_key] = 0
      set_control_flag(state_key, false, edge_name)
    end

    input.on_action(action, input.edges.triggered, function()
      engage("triggered")
    end)
    input.on_action(action, input.edges.released, function()
      release("released")
    end)
    input.on_action(action, input.edges.completed, function()
      release("completed")
    end)
    input.on_action(action, input.edges.canceled, function()
      release("canceled")
    end)
  end

  for state_key in pairs(control_state) do
    bind_hold(state_key)
  end
  log.info("[INPUT TEST] Inputs successfully wired. Waiting for key presses...")
end

function script.on_start(self)
  wire_input(self.params)
end

function script.on_gameplay(_self, dt_seconds)
  control_log_timer = control_log_timer + dt_seconds
  tick_control_latches()
  apply_vehicle_control()
end

return script
