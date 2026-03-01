local script = {}

local scene = oxygen.scene
local omath = oxygen.math

local kDefaultRadiansPerSecond = 0.9
local kSpeedParamKey = "rotation_angle"

local function resolve_node(ctx)
  local node = scene.current_node(ctx)
  if node == nil or not node:is_alive() then
    return nil
  end
  return node
end

local function resolve_speed(ctx)
  local value = scene.param(ctx, kSpeedParamKey)
  if type(value) ~= "number" then
    return kDefaultRadiansPerSecond
  end
  return value
end

function script.on_gameplay(ctx, dt_seconds)
  local node = resolve_node(ctx)
  if node == nil then
    return
  end

  local radians_per_second = resolve_speed(ctx)
  local angle = radians_per_second * dt_seconds
  node:rotate(omath.quat_from_euler_xyz(0.0, 0.0, angle), true)
end

return script
