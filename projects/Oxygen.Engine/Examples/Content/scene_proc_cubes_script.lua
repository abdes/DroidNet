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
local material_pool = {}
local pending_material_guids = {}
local gradient_material_cache = {}
local default_material_cache = nil
local logged_material_mode = false
local spawn_state = nil

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

local function resolve_geometry_token(ctx)
  local token = scene.param(ctx, "geometry_token") or "proc/cube"
  if type(token) ~= "string" or #token == 0 then
    return "proc/cube"
  end
  return token
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
  pending_material_guids = {}
  local raw = scene.param(ctx, "material_guids")
  if type(raw) == "string" and #raw > 0 then
    local guids = parse_material_guids(raw)
    for _, g in ipairs(guids) do
      -- store GUID strings and warm the loader; resolve to MaterialAsset at assign time
      table.insert(material_pool, g)
      if not assets.has_material(g) and not pending_material_guids[g] then
        pending_material_guids[g] = true
        assets.load_material_async(g, function(_m)
          pending_material_guids[g] = nil
          -- noop: when loaded, assets.get_material(g) will return the MaterialAsset
        end)
      end
    end
  end
  -- material_pool now contains GUID strings (or is empty)
end

local function deterministic_unit(seed, x, y, salt)
  local n = (x * 127.1) + (y * 311.7) + (seed * 74.7) + (salt * 19.19)
  local s = math.sin(n) * 43758.5453123
  return s - math.floor(s)
end

local function deterministic_between(seed, x, y, salt, a, b)
  return a + deterministic_unit(seed, x, y, salt) * (b - a)
end

local function lerp(a, b, t)
  return a + ((b - a) * t)
end

local function initialize_spawn_state(host_node, ctx)
  local state = {}
  state.geometry_token = resolve_geometry_token(ctx)
  load_materials_from_param(ctx)
  gradient_material_cache = {}
  default_material_cache = nil
  state.logged_geometry_error = false

  state.has_gradient_material_binding = type(assets.create_material_with_base_color) == "function"
  state.use_material_guids = scene.param(ctx, "use_material_guids") == true
  state.gradient_steps = math.floor(scene.param(ctx, "gradient_steps") or 12)
  if state.gradient_steps < 1 then
    state.gradient_steps = 1
  end
  if not logged_material_mode then
    if state.use_material_guids and #material_pool > 0 then
      log.info(string.format("proc_cubes: using material_guids pool (%d entries) (use_material_guids=true)",
        #material_pool))
    elseif state.use_material_guids and #material_pool == 0 then
      log.warning("proc_cubes: use_material_guids=true but material_guids is empty; using gradient/default materials")
    elseif state.has_gradient_material_binding then
      log.info("proc_cubes: using per-cube gradient materials via assets.create_material_with_base_color")
    else
      log.warning(
        "proc_cubes: assets.create_material_with_base_color not available; falling back to assets.create_default_material")
    end
    logged_material_mode = true
  end

  local count = scene.param(ctx, "count") or 64
  local cell_size = scene.param(ctx, "cell_size") or 1.0
  local min_scale = scene.param(ctx, "min_scale") or 0.6
  local max_scale = scene.param(ctx, "max_scale") or 1.0
  local origin_height_multiplier = scene.param(ctx, "origin_height_multiplier") or 5.0
  local height_falloff_power = scene.param(ctx, "height_falloff_power") or 2.2
  state.max_spawns_per_frame = math.floor(scene.param(ctx, "max_spawns_per_frame") or 100)
  state.max_prepare_steps_per_frame = math.floor(scene.param(ctx, "max_prepare_steps_per_frame") or 1000)
  if state.max_spawns_per_frame < 1 then
    state.max_spawns_per_frame = 1
  end
  if state.max_prepare_steps_per_frame < 1 then
    state.max_prepare_steps_per_frame = 1
  end
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
  local function add_coord(x, y)
    local k = key(x, y)
    if occupied[k] then
      return false
    end
    occupied[k] = true
    table.insert(coords, { x = x, y = y })
    return true
  end

  local function collect_unoccupied_neighbors(cell)
    local out = {}
    local dirs = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
    for _, d in ipairs(dirs) do
      local nx = cell.x + d[1]
      local ny = cell.y + d[2]
      if not occupied[key(nx, ny)] then
        table.insert(out, { x = nx, y = ny })
      end
    end
    return out
  end

  state.count = count
  state.cell_size = cell_size
  state.min_scale = min_scale
  state.max_scale = max_scale
  state.origin_height_multiplier = origin_height_multiplier
  state.height_falloff_power = height_falloff_power

  state.occupied = occupied
  state.coords = coords
  state.key = key
  state.collect_unoccupied_neighbors = collect_unoccupied_neighbors
  state.add_coord = add_coord

  state.frontier = {}
  add_coord(0, 0)
  table.insert(state.frontier, 1)

  state.phase = "prepare"
  state.spawn_cursor = 1
  state.max_dist = 0.0
  state.spawn_items = {}
  state.seed = seed

  return state
end

local function build_spawn_items(state)
  local items = {}
  local max_height_scale = state.max_scale * state.origin_height_multiplier

  for _, c in ipairs(state.coords) do
    local dist = math.sqrt((c.x * c.x) + (c.y * c.y))
    local closeness = 1.0
    if state.max_dist > 0.0 then
      closeness = 1.0 - (dist / state.max_dist)
    end

    local shaped_closeness = closeness ^ state.height_falloff_power
    local base_height_scale = state.min_scale
        + ((max_height_scale - state.min_scale) * shaped_closeness)

    local spread = deterministic_between(state.seed, c.x, c.y, 17, 0.75, 1.35)
    local jitter = deterministic_between(state.seed, c.x, c.y, 31, -0.15, 0.15)
        * state.max_scale
    local randomized_height_scale = (base_height_scale * spread) + jitter
    if randomized_height_scale < state.min_scale then
      randomized_height_scale = state.min_scale
    end
    if randomized_height_scale > max_height_scale then
      randomized_height_scale = max_height_scale
    end

    local gradient_t = closeness
    if gradient_t < 0.0 then
      gradient_t = 0.0
    end
    if gradient_t > 1.0 then
      gradient_t = 1.0
    end

    local bucket = 0
    if state.gradient_steps > 1 then
      bucket = math.floor((gradient_t * (state.gradient_steps - 1)) + 0.5)
    end

    local chosen_guid = nil
    if state.use_material_guids and #material_pool > 0 then
      local pick = 1
      if #material_pool > 1 then
        local r = deterministic_unit(state.seed, c.x, c.y, 47)
        pick = math.floor(r * #material_pool) + 1
        if pick > #material_pool then
          pick = #material_pool
        end
      end
      chosen_guid = material_pool[pick]
    end

    table.insert(items, {
      x = c.x,
      y = c.y,
      dist = dist,
      randomized_height_scale = randomized_height_scale,
      gradient_bucket = bucket,
      chosen_guid = chosen_guid,
    })
  end

  table.sort(items, function(a, b)
    if a.dist == b.dist then
      if a.y == b.y then
        return a.x < b.x
      end
      return a.y < b.y
    end
    return a.dist < b.dist
  end)

  state.spawn_items = items
end

local function advance_preparation(state)
  local steps = 0
  while #state.coords < state.count
    and #state.frontier > 0
    and steps < state.max_prepare_steps_per_frame do
    steps = steps + 1

    local frontier_pos = math.random(1, #state.frontier)
    local base_index = state.frontier[frontier_pos]
    local base = state.coords[base_index]
    local neighbors = state.collect_unoccupied_neighbors(base)

    if #neighbors == 0 then
      state.frontier[frontier_pos] = state.frontier[#state.frontier]
      table.remove(state.frontier)
    else
      local chosen = neighbors[math.random(1, #neighbors)]
      if state.add_coord(chosen.x, chosen.y) then
        table.insert(state.frontier, #state.coords)
      end

      local remaining = state.collect_unoccupied_neighbors(base)
      if #remaining == 0 then
        state.frontier[frontier_pos] = state.frontier[#state.frontier]
        table.remove(state.frontier)
      end
    end
  end

  if #state.coords >= state.count or #state.frontier == 0 then
    local max_dist = 0.0
    for _, c in ipairs(state.coords) do
      local d = math.sqrt((c.x * c.x) + (c.y * c.y))
      if d > max_dist then
        max_dist = d
      end
    end
    state.max_dist = max_dist
    build_spawn_items(state)
    state.phase = "spawn"
  end
end

local function spawn_batch(state, host_node)
  local spawned_this_frame = 0
  local far_color = { r = 0.12, g = 0.36, b = 0.95 }
  local near_color = { r = 1.00, g = 0.28, b = 0.08 }

  while state.spawn_cursor <= #state.spawn_items
    and spawned_this_frame < state.max_spawns_per_frame do
    local item = state.spawn_items[state.spawn_cursor]
    local randomized_height_scale = item.randomized_height_scale

    local footprint = state.cell_size
    local height = randomized_height_scale * state.cell_size
    local px = item.x * state.cell_size
    local py = item.y * state.cell_size
    local pz = height * 0.5
    local chosen_guid = item.chosen_guid

    local node = scene.create_node("ProcCube", host_node)
    if not valid(node) then
      log.warning("proc_cubes: failed to create cube node at " .. px .. "," .. py)
    else
      node:set_local_position(omath.vec3(px, py, pz))
      node:set_local_scale(omath.vec3(footprint, footprint, height))
      local set_ok, set_err = pcall(function()
        return node:renderable_set_geometry(state.geometry_token)
      end)
      if not set_ok then
        if not state.logged_geometry_error then
          log.error("proc_cubes: renderable_set_geometry token failed")
          state.logged_geometry_error = true
        end
      end

      local mat = nil
      local chosen = chosen_guid
      if type(chosen) == "string" then
        local resolved = assets.get_material(chosen)
        if resolved ~= nil then
          mat = resolved
        else
          if not assets.has_material(chosen)
              and not pending_material_guids[chosen] then
            pending_material_guids[chosen] = true
            assets.load_material_async(chosen, function(m)
              pending_material_guids[chosen] = nil
              if m ~= nil and valid(node) then
                node:renderable_set_material_override(1, 1, m)
              end
            end)
          end
        end
      end

      if mat == nil then
        if state.has_gradient_material_binding then
          local bucket = item.gradient_bucket
          local bucket_t = 0.0
          if state.gradient_steps > 1 then
            bucket_t = bucket / (state.gradient_steps - 1)
          end

          mat = gradient_material_cache[bucket]
          if mat == nil then
            local grad_r = lerp(far_color.r, near_color.r, bucket_t)
            local grad_g = lerp(far_color.g, near_color.g, bucket_t)
            local grad_b = lerp(far_color.b, near_color.b, bucket_t)
            mat = assets.create_material_with_base_color(grad_r, grad_g, grad_b, 1.0)
            if mat ~= nil then
              gradient_material_cache[bucket] = mat
            end
          end
          if mat == nil then
            log.warning("proc_cubes: create_material_with_base_color returned nil; using default material")
          end
        end
        if mat == nil then
          if default_material_cache == nil then
            default_material_cache = assets.create_default_material()
          end
          mat = default_material_cache
        end
      end

      if mat ~= nil then
        if not node:renderable_set_material_override(1, 1, mat) then
          log.error("proc_cubes: renderable_set_material_override returned false for node " .. node_tag(node))
        end
      end

      table.insert(created_nodes, node)
    end

    state.spawn_cursor = state.spawn_cursor + 1
    spawned_this_frame = spawned_this_frame + 1
  end

  if state.spawn_cursor > #state.spawn_items then
    log.info(string.format("proc_cubes: spawned %d cubes", #created_nodes))
    return true
  end

  return false
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

  if spawn_state == nil then
    spawn_state = initialize_spawn_state(host_node, ctx)
  end

  if spawn_state.phase == "prepare" then
    advance_preparation(spawn_state)
    return
  end

  created = spawn_batch(spawn_state, host_node)
end

function script.on_gameplay(ctx, dt_seconds)
  -- noop; can be extended to animate the cluster
  local _ = ctx
  local _dt = dt_seconds
end

return script
