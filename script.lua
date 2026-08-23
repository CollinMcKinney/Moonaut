-- Load the engine's default scenario (contains default globals/camera/entities)
local scenario_handle = load_scenario("default_scenario")

-- Over-ride the background/clear-color of the scene.
clear_color(0, 32, 64)

-- Two separate material pools – both contain the same list initially
local sphere_material_pool = {
    "default_material_wireframe",
    "default_material_flat",
    "default_material_gouraud",
    "default_material_phong",
    "default_material_water",
    "default_material_grass",
    "default_material_cloth",
    "default_material_wood",
    "default_material_metal",
    "default_material_glass",
    "default_material_skin",
    "default_material_rubber",
    "default_material_ice",
    "default_material_stone",
    "default_material_lava",
    "default_material_toon",
    "default_material_hologram",
    "default_material_iridescent",
    "default_material_plastic",
    "default_material_brick",
    "default_material_leather",
    "default_material_gold",
    "default_material_snow",
    "default_material_dirt",
    "default_material_neon",
    "default_material_velvet",
    "default_material_marble",
    "default_material_wax",
    "default_material_pearl",
    "default_material_ceramic",
    "default_material_chalk",
    "default_material_posterized",
    "default_material_frost",
    "default_material_rust",
    "default_material_carbon",
    "default_material_chrome",
    "default_material_emerald",
    "default_material_oilslick"
}

local station_material_pool = {
    "default_material_wireframe",
    "default_material_flat",
    "default_material_gouraud",
    "default_material_phong",
    "default_material_water",
    "default_material_grass",
    "default_material_cloth",
    "default_material_wood",
    "default_material_metal",
    "default_material_glass",
    "default_material_skin",
    "default_material_rubber",
    "default_material_ice",
    "default_material_stone",
    "default_material_lava",
    "default_material_toon",
    "default_material_hologram",
    "default_material_iridescent",
    "default_material_plastic",
    "default_material_brick",
    "default_material_leather",
    "default_material_gold",
    "default_material_snow",
    "default_material_dirt",
    "default_material_neon",
    "default_material_velvet",
    "default_material_marble",
    "default_material_wax",
    "default_material_pearl",
    "default_material_ceramic",
    "default_material_chalk",
    "default_material_posterized",
    "default_material_frost",
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

-- ------------------------------------------------------------
-- Independent timers and indices
-- ------------------------------------------------------------
local sphere_timer = 0
local sphere_interval = 5   -- seconds
local sphere_index = 1

local station_timer = 0
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
    local radius = 10
    local height = 7
    local cam_x = radius * math.cos(cam_angle)
    local cam_z = radius * math.sin(cam_angle)
    camera_eye(cam_x, height, cam_z)
    camera_lookat(0, 0, 0, 0, 1, 0)

    -- Cycle light direction (orbit faster)
    local light_angle = (total_time / light_dir_cycle_time) * 8 * math.pi
    local light_x = math.sin(light_angle)
    local light_y = math.tan(light_angle) * math.tan(light_angle)
    local light_z = math.cos(light_angle)
    light_direction(light_x, light_y, light_z)

    -- Cycle light color (hue shift)
    local hue = (total_time / light_col_cycle_time) * 360  -- degrees
    local r = (math.sin(hue * math.pi / 180) + 1) 
    local g = (math.sin((hue + 120) * math.pi / 180) + 1)
    local b = (math.sin((hue + 240) * math.pi / 180) + 1)
    light_color(r, g, b)

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
    end
end