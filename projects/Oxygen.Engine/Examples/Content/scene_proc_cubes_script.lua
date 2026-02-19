local script = {}

local scene = oxygen.scene
local assets = oxygen.assets
local omath = oxygen.math
local log = oxygen.log
local otime = oxygen.time

local node_ref = nil
local host_runtime_id = nil
local created = false
local created_nodes = {}
local cube_geo = nil
local material_pool = {}
local logged_material_mode = false

local function valid(node)
  return node ~= nil and node:is_alive()
end

local function node_tag(node)
  if not valid(node) then
    return "dead"
  end
  local id = node:runtime_id()
  return string.format("%s[sid=%d idx=%d]", node:get_name() or "?", id.scene_id or -1, id.node_index or -1)
end

local function same_runtime_id(a, b)
  if a == nil or b == nil then
    return false
  end
  return a.scene_id == b.scene_id and a.node_index == b.node_index
end

local function resolve_host_node(ctx, allow_lock)
  local current = scene.current_node(ctx)
  if not valid(current) then
    return nil
  end
  if host_runtime_id == nil then
    if not allow_lock then
      return nil
    end
    host_runtime_id = current:runtime_id()
    node_ref = current
    log.info("proc_cubes: host locked -> " .. node_tag(current))
    return current
  end
  local current_id = current:runtime_id()
  if same_runtime_id(current_id, host_runtime_id) then
    node_ref = current
    return current
  end
  return nil
end

local function ensure_cube_geo()
  local geo = assets.create_procedural_geometry("cube")
  if geo == nil then
    log.error("proc_cubes: failed to create cube geometry")
    return nil
  end
  return geo
end

local function parse_material_guids(text)
  local out = {}
  if type(text) ~= "string" then
    return out
  end
  for token in string.gmatch(text, "([^,]+)") do
    local s = token:match("^%s*(.-)%s*$")
    if s ~= nil and #s > 0 then
      table.insert(out, s)
    end
  end
  return out
end

local function load_materials_from_param(ctx)
  material_pool = {}
  local raw = scene.param(ctx, "material_guids")
  if type(raw) == "string" and #raw > 0 then
    local guids = parse_material_guids(raw)
    for _,g in ipairs(guids) do
      -- store GUID strings and warm the loader; resolve to MaterialAsset at assign time
      table.insert(material_pool, g)
      if not assets.has_material(g) then
        assets.load_material_async(g, function(_m)
          -- noop: when loaded, assets.get_material(g) will return the MaterialAsset
        end)
      end
    end
  end
  -- material_pool now contains GUID strings (or is empty)
end

local function random_between(a, b)
  return a + math.random() * (b - a)
end

local function lerp(a, b, t)
  return a + ((b - a) * t)
end

local function generate_pattern(host_node, ctx)
  ensure_cube_geo()
  load_materials_from_param(ctx)

  local has_gradient_material_binding = type(assets.create_material_with_base_color) == "function"
  local use_material_guids = scene.param(ctx, "use_material_guids") == true
  if not logged_material_mode then
    if use_material_guids and #material_pool > 0 then
      log.info(string.format("proc_cubes: using material_guids pool (%d entries) (use_material_guids=true)", #material_pool))
    elseif use_material_guids and #material_pool == 0 then
      log.warning("proc_cubes: use_material_guids=true but material_guids is empty; using gradient/default materials")
    elseif has_gradient_material_binding then
      log.info("proc_cubes: using per-cube gradient materials via assets.create_material_with_base_color")
    else
      log.warning("proc_cubes: assets.create_material_with_base_color not available; falling back to assets.create_default_material")
    end
    logged_material_mode = true
  end

  local count = scene.param(ctx, "count") or 64
  local cell_size = scene.param(ctx, "cell_size") or 1.0
  local min_scale = scene.param(ctx, "min_scale") or 0.6
  local max_scale = scene.param(ctx, "max_scale") or 1.0
  local origin_height_multiplier = scene.param(ctx, "origin_height_multiplier") or 5.0
  local height_falloff_power = scene.param(ctx, "height_falloff_power") or 2.2
  -- seed randomness for reproducible-ish results per host
  local seed = 1
  if otime and otime.delta_seconds then
    seed = math.floor((otime.delta_seconds() or 0.0) * 1000.0)
  end
  if valid(host_node) then
    local id = host_node:runtime_id()
    seed = (id.scene_id or 0) * 1000003 + (id.node_index or 0)
  end
  math.randomseed(seed)

  local occupied = {}
  local coords = {}
  local function key(x, y) return tostring(x) .. "," .. tostring(y) end

  -- start at origin
  coords[1] = { x = 0, y = 0 }
  occupied[key(0,0)] = true

  -- random walk / growth to form an organic cluster
  for i = 2, count do
    local placed = false
    local attempts = 0
    while not placed and attempts < 128 do
      attempts = attempts + 1
      local base = coords[math.random(1, #coords)]
      local dirs = { {1,0}, {-1,0}, {0,1}, {0,-1} }
      local d = dirs[math.random(1, #dirs)]
      local nx = base.x + d[1]
      local ny = base.y + d[2]
      if not occupied[key(nx, ny)] then
        table.insert(coords, { x = nx, y = ny })
        occupied[key(nx, ny)] = true
        placed = true
      end
    end
    if not placed then
      break
    end
  end

  -- normalize distance-to-origin for proportional (randomized) height on Z axis
  local max_dist = 0.0
  for _,c in ipairs(coords) do
    local d = math.sqrt((c.x * c.x) + (c.y * c.y))
    if d > max_dist then
      max_dist = d
    end
  end

  -- create nodes
  local idx = 1
  for _,c in ipairs(coords) do
    local dist = math.sqrt((c.x * c.x) + (c.y * c.y))
    local closeness = 1.0
    if max_dist > 0.0 then
      closeness = 1.0 - (dist / max_dist)
    end

    local shaped_closeness = closeness ^ height_falloff_power
    local max_height_scale = max_scale * origin_height_multiplier
    local base_height_scale = min_scale + ((max_height_scale - min_scale) * shaped_closeness)

    -- keep randomness, but preserve strong origin bias
    local spread = random_between(0.75, 1.35)
    local jitter = random_between(-0.15, 0.15) * max_scale
    local randomized_height_scale = (base_height_scale * spread) + jitter
    if randomized_height_scale < min_scale then
      randomized_height_scale = min_scale
    end
    if randomized_height_scale > max_height_scale then
      randomized_height_scale = max_height_scale
    end

    local footprint = cell_size
    local height = randomized_height_scale * cell_size
    local px = c.x * cell_size
    local py = c.y * cell_size
    local pz = height * 0.5

    local height_norm = 0.0
    if max_height_scale > min_scale then
      height_norm = (randomized_height_scale - min_scale) / (max_height_scale - min_scale)
    end

    local gradient_t = (closeness * 0.65) + (height_norm * 0.35)
    if gradient_t < 0.0 then
      gradient_t = 0.0
    end
    if gradient_t > 1.0 then
      gradient_t = 1.0
    end

    local far_color = { r = 0.12, g = 0.36, b = 0.95 }
    local near_color = { r = 1.00, g = 0.28, b = 0.08 }
    local grad_r = lerp(far_color.r, near_color.r, gradient_t)
    local grad_g = lerp(far_color.g, near_color.g, gradient_t)
    local grad_b = lerp(far_color.b, near_color.b, gradient_t)

    local node = scene.create_node("ProcCube", nil)
    if not valid(node) then
      log.warning("proc_cubes: failed to create cube node at " .. px .. "," .. py)
    else
      node:set_local_position(omath.vec3(px, py, pz))
      node:set_local_scale(omath.vec3(footprint, footprint, height))
      -- create and assign cube geometry following example pattern
      local geom = ensure_cube_geo()
      if geom ~= nil then
        if not node:renderable_set_geometry(geom) then
          log.error("proc_cubes: renderable_set_geometry failed for procedural geometry, trying token")
          node:renderable_set_geometry("proc/cube")
        end
      else
        -- fallback to token-based geometry
        node:renderable_set_geometry("proc/cube")
      end

      -- pick and resolve a material GUID at assign time
      local mat = nil
        if use_material_guids and #material_pool > 0 then
        local chosen = material_pool[math.random(1, #material_pool)]
        if type(chosen) == "string" then
          local resolved = assets.get_material(chosen)
          if resolved ~= nil then
            mat = resolved
          else
            -- start async load if not present; when loaded apply to this node if still valid
            if not assets.has_material(chosen) then
              assets.load_material_async(chosen, function(m)
                if m ~= nil and valid(node) then
                  node:renderable_set_material_override(1, 1, m)
                end
              end)
            end
          end
        end
      end

      if mat == nil then
        if has_gradient_material_binding then
          mat = assets.create_material_with_base_color(grad_r, grad_g, grad_b, 1.0)
          if mat == nil then
            log.warning("proc_cubes: create_material_with_base_color returned nil; using default material")
          end
        end
        if mat == nil then
          mat = assets.create_default_material()
        end
      end

      if mat ~= nil then
        if not node:renderable_set_material_override(1, 1, mat) then
          log.error("proc_cubes: renderable_set_material_override returned false for node " .. node_tag(node))
        end
      end

      table.insert(created_nodes, node)
    end
    idx = idx + 1
  end

  log.info(string.format("proc_cubes: spawned %d cubes", #created_nodes))
end

function script.on_scene_mutation(ctx, dt_seconds)
  local _ = dt_seconds
  -- keep host lock semantics similar to other scripts
  if not valid(node_ref) then
    host_runtime_id = nil
  end
  local host_node = resolve_host_node(ctx, true)
  if not valid(host_node) then
    return
  end

  if created then
    return
  end

  generate_pattern(host_node, ctx)
  created = true
end

function script.on_gameplay(ctx, dt_seconds)
  -- noop; can be extended to animate the cluster
  local _ = ctx
  local _dt = dt_seconds
end

return script
