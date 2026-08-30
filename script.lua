-- Load the engine's default scenario (contains default globals/camera/entities)
local scenario_handle = load_scenario("default_scenario")

-- Over-ride the background/clear-color of the scene.
clear_color(0, 32, 64)

-- --------------------------------------------------------
-- Extra lights (point, spot, directional)
-- --------------------------------------------------------
light_clear()

-- Point light 0: blue, follows sphere (updated in update())
light_set_type(0, LIGHT_POINT)
light_set_position(0, 0, 5, 0)
light_set_color(0, 0, 0, 20)
light_set_range(0, 7)
light_set_enabled(0, true)

-- Point light 1: red, at (0,5,0)
light_set_type(1, LIGHT_POINT)
light_set_position(1, 0, 5, 2)
light_set_color(1, 20, 0, 0)          
light_set_range(1, 7)
light_set_enabled(1, true)

-- Point light 2: green, at (0,5,10)
light_set_type(2, LIGHT_POINT)
light_set_position(2, 0, 5, 10)
light_set_color(2, 0, 20, 0)           -- intense green
light_set_range(2, 7)
light_set_enabled(2, true)

-- Point light 3: blue, at (0,5,-6)
light_set_type(3, LIGHT_POINT)
light_set_position(3, 0, 5, -6)
light_set_color(3, 0, 0, 20)          -- intense blue
light_set_range(3, 10)
light_set_enabled(3, true)

-- Spot light 4: magenta, at (10,5,1) pointing toward origin
light_set_type(4, LIGHT_SPOT)
light_set_position(4, 10, 5, 1)
light_set_color(4, 20, 0, 20)
light_set_direction(4, 0, -1, 0)
light_set_range(4, 30)
light_set_spot_params(4, 0.25, 0.5, 2.0)
light_set_enabled(4, true)

-- Spot light 5: yellow, at (-10,5,1) pointing toward origin
light_set_type(5, LIGHT_SPOT)
light_set_position(5, -10, 5, 1)
light_set_color(5, 20, 20, 0)
light_set_direction(5, 0, -1, 0)
light_set_range(5, 30)
light_set_spot_params(5, 0.25, 0.5, 2.0)
light_set_enabled(5, true)

-- Spot light 6: cyan, at (10,5,-6) pointing toward origin
light_set_type(6, LIGHT_SPOT)
light_set_position(6, 10, 5, -6)
light_set_color(6, 0, 20, 20)
light_set_direction(6, 0, -1, 0)
light_set_range(6, 30)
light_set_spot_params(6, 0.25, 0.5, 2.0)
light_set_enabled(6, true)

-- Spot light 7: yellow, at (10,5,10) pointing toward origin
light_set_type(7, LIGHT_SPOT)
light_set_position(7, 10, 5, 10)
light_set_color(7, 20, 20, 0)
light_set_direction(7, 0, -1, 0)
light_set_range(7, 30)
light_set_spot_params(7, 0.25, 0.5, 2.0)
light_set_enabled(7, true)

-- Spot light 8: magenta, at (-10,5,-6) pointing toward origin
light_set_type(8, LIGHT_SPOT)
light_set_position(8, -10, 5, -6)
light_set_color(8, 20, 0, 20)
light_set_direction(8, 0, -1, 0)
light_set_range(8, 30)
light_set_spot_params(8, 0.25, 0.5, 2.0)
light_set_enabled(8, true)

-- Spot light 9: cyan, at (-10,5,10) pointing toward origin
light_set_type(9, LIGHT_SPOT)
light_set_position(9, -10, 5, 10)
light_set_color(9, 0, 20, 20)
light_set_direction(9, 0, -1, 0)
light_set_range(9, 30)
light_set_spot_params(9, 0.25, 0.5, 2.0)
light_set_enabled(9, true)

-- Directional light 10: upward fill (from below)
light_set_type(10, LIGHT_DIRECTIONAL)
light_set_direction(10, 0, -1, 0)          -- pointing up
light_set_color(10, 0.25,0.5,0.25)    
light_set_enabled(10, true)

-- --------------------------------------------------------
-- White spot light that follows the camera
-- --------------------------------------------------------
light_set_type(11, LIGHT_SPOT)
light_set_color(11, 2,2,2)               -- white
light_set_range(11, 100)
light_set_spot_params(11, 0.25, 0.5, 2.0)   -- inner cone 0.25 rad, outer 0.5 rad, falloff 2
light_set_enabled(11, true)

-- Load a particle emitter tag
local emitter = particle_load_emitter("default_particle_emitter")

-- Change its properties on the fly
particle_set_position(0, 5, 0)
particle_set_color(1, 0.5, 1)
particle_set_alpha(1)
particle_set_rate(4096)

-- Two separate material pools – both contain the same list initially
local sphere_material_pool = {
    "default_material_grass",
    "default_material_cloth",
    "default_material_wood",
    "default_material_metal",
    "default_material_skin",
    "default_material_rubber",
    "default_material_stone",
    "default_material_plastic",
    "default_material_brick",
    "default_material_leather",
    "default_material_gold",
    "default_material_snow",
    "default_material_dirt",
    "default_material_velvet",
    "default_material_marble",
    "default_material_pearl",
    "default_material_ceramic",
    "default_material_chalk",
    "default_material_rust",
    "default_material_carbon",
    "default_material_chrome",
    "default_material_emerald",
    "default_material_oilslick"
}

local station_material_pool = {
    "default_material_grass",
    "default_material_cloth",
    "default_material_wood",
    "default_material_metal",
    "default_material_skin",
    "default_material_rubber",
    "default_material_stone",
    "default_material_plastic",
    "default_material_brick",
    "default_material_leather", 
    "default_material_gold",
    "default_material_snow",
    "default_material_dirt",
    "default_material_velvet",
    "default_material_marble",
    "default_material_pearl",
    "default_material_ceramic",
    "default_material_chalk",
    "default_material_rust",
    "default_material_carbon",
    "default_material_chrome",
    "default_material_emerald",
    "default_material_oilslick"
}

-- Load material handles for each pool
local sphere_handles = {}
for i, name in ipairs(sphere_material_pool) do
    sphere_handles[i] = tag_load(name, TAG_material)
end

local station_handles = {}
for i, name in ipairs(station_material_pool) do
    station_handles[i] = tag_load(name, TAG_material)
end

local sphere_pool_size = #sphere_handles
local station_pool_size = #station_handles

-- Import the station model
local station_model_handle = import_model("station.glb")
local station_cbsp_handle = build_cbsp(station_model_handle)

if tag_get_block_count(scenario_handle, "entities") > 0 then
    local entity_handle = tag_get_block_field(scenario_handle, "entities", 1, "entity")
    if entity_handle and entity_handle >= 0 then
        tag_set_field(entity_handle, "model", station_model_handle)
        tag_set_field(entity_handle, "collision_bsp", station_cbsp_handle)
    end
end

-- Remember the first entity as the 'sphere' to track with particles
local sphere_entity_handle = nil
if tag_get_block_count(scenario_handle, "entities") > 0 then
    -- use index 0 for the first entity element
    local ent = tag_get_block_field(scenario_handle, "entities", 0, "entity")
    if ent and ent >= 0 then sphere_entity_handle = ent end
end

-- ------------------------------------------------------------
-- Independent timers and indices
-- ------------------------------------------------------------
local sphere_timer = 0
local sphere_interval = 5   -- seconds
local sphere_index = 1

-- trigger immediately for debugging
local station_timer = 5
local station_interval = 5  -- seconds (can be different)
local station_index = 2     -- start offset so they're on different materials

-- seconds for all animations to complete their cycles
local cam_cycle_time = 15
local light_dir_cycle_time = 10
local light_col_cycle_time = 5

local total_time = 0
function update(dt)
    total_time = total_time + dt

    -- Orbit camera around (0,0,0)
    local cam_angle = (total_time / cam_cycle_time) * 2 * math.pi
    local radius = 12
    local height = 2
    local cam_x = radius * math.cos(cam_angle)
    local cam_z = radius * math.sin(cam_angle)
    camera_eye(cam_x, height, cam_z)
    camera_lookat(0, 0, 0, 0, 1, 0)

    -- ---- Update global (main) directional light ----
    light_direction(0, 1, 0)
    light_color(0.5,0.5,1)          -- blue (you can change)
    --light_ambient(0.0, 0.0, 0.0)

    -- ---- Update camera‑following spotlight ----
    local eye_pos = vec3(cam_x, height, cam_z)
    light_set_position(11, eye_pos.x, eye_pos.y, eye_pos.z)
    -- Point slightly downwards: target (0, -2, 0) instead of (0,0,0)
    local target_x = 0
    local target_y = 0   -- slightly below origin
    local target_z = 0
    local dx = target_x - eye_pos.x
    local dy = target_y - eye_pos.y
    local dz = target_z - eye_pos.z
    local len = math.sqrt(dx*dx + dy*dy + dz*dz)
    if len > 0 then
        light_set_direction(11, dx/len, dy/len, dz/len)
    end

    -- ---- Update sphere‑following point light (index 0) ----
    if sphere_entity_handle then
        local pos = tag_get_field(sphere_entity_handle, "position")
        if pos then
            light_set_position(0, pos.x, pos.y, pos.z)
        end
    end

    -- --------------------------------------------------------
    -- 1. Update sphere material (uses sphere_handles)
    -- --------------------------------------------------------
    sphere_timer = sphere_timer + dt
    if sphere_timer >= sphere_interval then
        sphere_timer = 0
        sphere_index = (sphere_index % sphere_pool_size) + 1
        local sphere_mat = sphere_handles[sphere_index]
        local sphere_name = sphere_material_pool[sphere_index]

        print("Sphere material changed to: " .. sphere_name)

        if tag_get_block_count(scenario_handle, "entities") > 0 then
            local ent_handle = tag_get_block_field(scenario_handle, "entities", 0, "entity")
            if ent_handle and ent_handle >= 0 then
                local model_handle = tag_get_field(ent_handle, "model")
                if model_handle and model_handle >= 0 then
                    if tag_get_block_count(model_handle, "materials") > 0 then
                        tag_set_block_field(model_handle, "materials", 0, "material", sphere_mat)
                    end
                end
                -- Physics updates (optional)
                local rigid_body_handle = tag_get_field(ent_handle, "rigid_body")
                tag_set_field(rigid_body_handle, "velocity", vec3(-5, 7, 0))
                tag_set_field(rigid_body_handle, "angular_velocity", vec3(1, 0, 0))
                tag_set_field(ent_handle, "position", vec3(5, 3, 3))
            end
        end
    end

    -- --------------------------------------------------------
    -- 2. Update station material (uses station_handles)
    -- --------------------------------------------------------
    station_timer = station_timer + dt
    if station_timer >= station_interval then
        station_timer = 0
        station_index = (station_index % station_pool_size) + 1
        local station_mat = station_handles[station_index]
        local station_name = station_material_pool[station_index]

        print("Station material changed to: " .. station_name)

        if station_model_handle and station_model_handle >= 0 then
            local mat_count = tag_get_block_count(station_model_handle, "materials")
            if mat_count > 0 then
                for i = 0, mat_count - 1 do
                    tag_set_block_field(station_model_handle, "materials", i, "material", station_mat)
                end
            end
        end
        -- Debug: print assigned material properties to detect culling/winding issues
        do
            local mat_count = tag_get_block_count(station_model_handle, "materials")
            print("Station materials after assignment: count=" .. tostring(mat_count))
            for i = 0, mat_count - 1 do
                local mat_handle = tag_get_block_field(station_model_handle, "materials", i, "material")
                if mat_handle and mat_handle >= 0 then
                    local ds = tag_get_field(mat_handle, "double_sided")
                    local alpha = tag_get_field(mat_handle, "alpha")
                    local rm = tag_get_field(mat_handle, "render_method")
                    print(string.format("  material[%d]=%d double_sided=%s alpha=%.2f render_method=0x%X", i, mat_handle, tostring(ds), tonumber(alpha) or 0.0, tonumber(rm) or 0))
                else
                    print(string.format("  material[%d]=nil", i))
                end
            end
        end
    end

    -- --------------------------------------------------------
    -- 3. Track sphere position with particle emitter (leave trail)
    -- --------------------------------------------------------
    if sphere_entity_handle then
        local pos = tag_get_field(sphere_entity_handle, "position")
        if pos then
            particle_set_position(pos.x, pos.y, pos.z)
            light_set_position(0, pos.x, pos.y, pos.z)
        end
    end
end