
-- Load the engine's default scenario (contains default globals/camera/entities)
load_scenario("default_scenario")

-- Over-ride the background/clear-color of the scene.
clear_color(0, 32, 64)

local frame_count = 0

--called every game-tick.
function update(dt) 
    frame_count = frame_count + 1
    if frame_count == 60 then
        frame_count = 0
    end
end