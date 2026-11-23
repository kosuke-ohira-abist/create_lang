#include "native_function.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace lang
{
    namespace
    {
        struct AnimationSet
        {
            ModelAnimation *clips = nullptr;
            int count = 0;
        };

        struct ModelStore
        {
            std::int64_t next_id = 1;
            std::unordered_map<std::int64_t, Model> models;

            std::unordered_map<std::int64_t, AnimationSet> animations;
        };

        using Args = std::span<const VmValue>;

        float number(Args args, std::size_t index)
        {
            return static_cast<float>(std::get<double>(args[index]));
        }
    }

    void register_host_functions(NativeFunctionRegistry &registry)
    {
        using T = TypeDecl;
        auto model_store = std::make_shared<ModelStore>();

        registry.register_function(
            "init_window", {{T::F64, T::F64, T::Str}, T::F64},
            [](Args args) -> VmValue
            {
                InitWindow(
                    static_cast<int>(number(args, 0)),
                    static_cast<int>(number(args, 1)),
                    std::get<std::string>(args[2]).c_str());

                if (!IsWindowReady())
                {
                    throw std::runtime_error(
                        "raylib window initialization failed");
                }

                return 0.0;
            });

        registry.register_function(
            "set_target_fps", {{T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                SetTargetFPS(
                    static_cast<int>(number(args, 0)));
                return 0.0;
            });

        registry.register_function(
            "window_should_close", {{}, T::F64},
            [](Args) -> VmValue
            {
                return WindowShouldClose() ? 1.0 : 0.0;
            });

        registry.register_function(
            "begin_drawing", {{}, T::F64},
            [](Args) -> VmValue
            {
                BeginDrawing();
                return 0.0;
            });

        registry.register_function(
            "end_drawing", {{}, T::F64},
            [](Args) -> VmValue
            {
                EndDrawing();
                return 0.0;
            });

        registry.register_function(
            "clear_background", {{}, T::F64},
            [](Args) -> VmValue
            {
                ClearBackground(RAYWHITE);
                return 0.0;
            });

        registry.register_function(
            "begin_mode_3d",
            {{T::F64, T::F64, T::F64,
              T::F64, T::F64, T::F64},
             T::F64},
            [](Args args) -> VmValue
            {
                Camera3D camera{};

                camera.position = {
                    number(args, 0),
                    number(args, 1),
                    number(args, 2)};

                camera.target = {
                    number(args, 3),
                    number(args, 4),
                    number(args, 5)};

                camera.up = {0.0f, 1.0f, 0.0f};
                camera.fovy = 45.0f;
                camera.projection = CAMERA_PERSPECTIVE;

                BeginMode3D(camera);
                return 0.0;
            });

        registry.register_function(
            "end_mode_3d", {{}, T::F64},
            [](Args) -> VmValue
            {
                EndMode3D();
                return 0.0;
            });

        registry.register_function(
            "draw_cube", {{T::F64, T::F64, T::F64, T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                const float size = number(args, 3);

                DrawCube(
                    {number(args, 0),
                     number(args, 1),
                     number(args, 2)},
                    size,
                    size,
                    size,
                    RED);

                return 0.0;
            });

        registry.register_function(
            "draw_grid", {{T::F64, T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                DrawGrid(
                    static_cast<int>(number(args, 0)),
                    number(args, 1));

                return 0.0;
            });

        registry.register_function(
            "get_frame_time", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(GetFrameTime());
            });

        registry.register_function(
            "is_key_down", {{T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                const double key = std::get<double>(args[0]);

                if (!std::isfinite(key) ||
                    std::trunc(key) != key ||
                    key < static_cast<double>(
                              std::numeric_limits<int>::min()) ||
                    key > static_cast<double>(
                              std::numeric_limits<int>::max()))
                {
                    throw std::runtime_error("Invalid key code");
                }

                return IsKeyDown(static_cast<int>(key)) ? 1.0 : 0.0;
            });

        registry.register_function(
            "key_left", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(KEY_LEFT);
            });

        registry.register_function(
            "key_right", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(KEY_RIGHT);
            });

        registry.register_function(
            "key_up", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(KEY_UP);
            });

        registry.register_function(
            "key_down", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(KEY_DOWN);
            });

        registry.register_function(
            "disable_cursor", {{}, T::F64},
            [](Args) -> VmValue
            {
                DisableCursor();
                return 0.0;
            });

        registry.register_function(
            "enable_cursor", {{}, T::F64},
            [](Args) -> VmValue
            {
                EnableCursor();
                return 0.0;
            });

        registry.register_function(
            "get_mouse_delta_x", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(GetMouseDelta().x);
            });

        registry.register_function(
            "get_mouse_delta_y", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(GetMouseDelta().y);
            });

        registry.register_function(
            "key_w", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(KEY_W);
            });

        registry.register_function(
            "key_a", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(KEY_A);
            });

        registry.register_function(
            "key_s", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(KEY_S);
            });

        registry.register_function(
            "key_d", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(KEY_D);
            });

        registry.register_function(
            "load_model",
            {{T::Str}, T::I64},
            [model_store](Args args) -> VmValue
            {
                if (!IsWindowReady())
                {
                    throw std::runtime_error(
                        "Window must be initialized before loading models");
                }

                const auto &path = std::get<std::string>(args[0]);

                Model model = LoadModel(path.c_str());

                if (!IsModelValid(model))
                {
                    throw std::runtime_error(
                        "Failed to load model: " + path);
                }

                const auto id = model_store->next_id++;

                model_store->models.emplace(id, model);

                return std::int64_t{id};
            });

        registry.register_function(
            "draw_model_ex",
            {{T::I64, T::F64, T::F64, T::F64, T::F64, T::F64,
              T::F64, T::F64, T::F64},
             T::F64},
            [model_store](Args args) -> VmValue
            {
                const auto id =
                    std::get<std::int64_t>(args[0]);

                const auto it = model_store->models.find(id);

                if (it == model_store->models.end())
                {
                    throw std::runtime_error(
                        "Invalid model handle");
                }

                const Vector3 position{
                    number(args, 1),
                    number(args, 2),
                    number(args, 3)};

                const Vector3 rotation_axis{
                    number(args, 4),
                    number(args, 5),
                    number(args, 6)};

                const float rotation_angle =
                    number(args, 7);

                const float scale = number(args, 8);

                DrawModelEx(
                    it->second,
                    position,
                    rotation_axis,
                    rotation_angle,
                    Vector3{scale, scale, scale},
                    WHITE);

                return 0.0;
            });

        registry.register_function(
            "unload_model", {{T::I64}, T::F64},
            [model_store](Args args) -> VmValue
            {
                const auto id = std::get<std::int64_t>(args[0]);

                const auto it = model_store->models.find(id);

                if (it == model_store->models.end())
                {
                    throw std::runtime_error("Invalid model handle");
                }

                const auto animation_it =
                    model_store->animations.find(id);

                if (animation_it != model_store->animations.end())
                {
                    UnloadModelAnimations(
                        animation_it->second.clips,
                        animation_it->second.count);

                    model_store->animations.erase(animation_it);
                }

                UnloadModel(it->second);
                model_store->models.erase(it);

                return 0.0;
            });

        registry.register_function(
            "clear_depth_buffer", {{}, T::F64},
            [](Args) -> VmValue
            {
                rlDrawRenderBatchActive();

                rlColorMask(false, false, false, false);

                rlEnableDepthMask();

                rlClearScreenBuffers();

                rlColorMask(true, true, true, true);

                return 0.0;
            });

        registry.register_function(
            "mouse_button_left", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(MOUSE_BUTTON_LEFT);
            });

        registry.register_function(
            "is_mouse_button_pressed", {{T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                const double button = std::get<double>(args[0]);

                if (!std::isfinite(button) ||
                    std::trunc(button) != button ||
                    button < static_cast<double>(std::numeric_limits<int>::min()) ||
                    button > static_cast<double>(std::numeric_limits<int>::max()))
                {
                    throw std::runtime_error("Invalid mouse button");
                }

                return IsMouseButtonPressed(static_cast<int>(button))
                           ? 1.0
                           : 0.0;
            });

        registry.register_function(
            "ray_hits_sphere",
            {{T::F64, T::F64, T::F64, T::F64, T::F64, T::F64,
              T::F64, T::F64, T::F64, T::F64},
             T::F64},
            [](Args args) -> VmValue
            {
                const Ray ray{
                    Vector3{
                        number(args, 0),
                        number(args, 1),
                        number(args, 2)},
                    Vector3{
                        number(args, 3),
                        number(args, 4),
                        number(args, 5)}};

                const Vector3 center{
                    number(args, 6),
                    number(args, 7),
                    number(args, 8)};

                const float radius = number(args, 9);

                if (!(radius > 0.0f))
                {
                    throw std::runtime_error("Invalid sphere radius");
                }

                const RayCollision hit =
                    GetRayCollisionSphere(ray, center, radius);

                return hit.hit && hit.distance >= 0.0f
                           ? 1.0
                           : 0.0;
            });

        registry.register_function(
            "draw_circle_2d", {{T::F64, T::F64, T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                DrawCircleV(
                    Vector2{
                        number(args, 0),
                        number(args, 1)},
                    number(args, 2),
                    BLACK);

                return 0.0;
            });

        registry.register_function(
            "load_model_animations", {{T::I64, T::Str}, T::F64},
            [model_store](Args args) -> VmValue
            {
                const auto id = std::get<std::int64_t>(args[0]);

                if (!model_store->models.contains(id))
                {
                    throw std::runtime_error("Invalid model handle");
                }

                if (model_store->animations.contains(id))
                {
                    throw std::runtime_error(
                        "Animations already loaded");
                }

                const auto &path = std::get<std::string>(args[1]);

                int count = 0;
                ModelAnimation *clips =
                    LoadModelAnimations(path.c_str(), &count);

                if (!clips || count <= 0)
                {
                    throw std::runtime_error(
                        "No animations found: " + path);
                }

                try
                {
                    model_store->animations.emplace(
                        id, AnimationSet{clips, count});
                }
                catch (...)
                {
                    UnloadModelAnimations(clips, count);
                    throw;
                }

                return static_cast<double>(count);
            });

        registry.register_function(
            "update_model_animation", {{T::I64, T::F64, T::F64}, T::F64},
            [model_store](Args args) -> VmValue
            {
                const auto id = std::get<std::int64_t>(args[0]);

                const auto model_it =
                    model_store->models.find(id);

                const auto animation_it =
                    model_store->animations.find(id);

                if (model_it == model_store->models.end() ||
                    animation_it == model_store->animations.end())
                {
                    throw std::runtime_error(
                        "Model or animations not loaded");
                }

                const double index = std::get<double>(args[1]);
                const double frame = std::get<double>(args[2]);

                const auto &set = animation_it->second;

                if (!std::isfinite(index) ||
                    std::trunc(index) != index ||
                    index < 0 ||
                    index >= set.count)
                {
                    throw std::runtime_error(
                        "Invalid animation index");
                }

                if (!std::isfinite(frame) || frame < 0)
                {
                    throw std::runtime_error(
                        "Invalid animation frame");
                }

                const auto &clip =
                    set.clips[static_cast<std::size_t>(index)];

                if (clip.keyframeCount <= 0 ||
                    !IsModelAnimationValid(model_it->second, clip))
                {
                    throw std::runtime_error(
                        "Incompatible model animation");
                }

                const float wrapped_frame = static_cast<float>(
                    std::fmod(
                        frame,
                        static_cast<double>(clip.keyframeCount)));

                UpdateModelAnimation(
                    model_it->second,
                    clip,
                    wrapped_frame);

                return 0.0;
            });

        registry.register_function(
            "draw_text", {{T::Str, T::F64, T::F64, T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                const auto &text =
                    std::get<std::string>(args[0]);

                DrawText(
                    text.c_str(),
                    static_cast<int>(number(args, 1)),
                    static_cast<int>(number(args, 2)),
                    static_cast<int>(number(args, 3)),
                    BLACK);

                return 0.0;
            });

        registry.register_function(
            "close_window", {{}, T::F64},
            [model_store](Args) -> VmValue
            {
                for (auto &[id, animation] : model_store->animations)
                {
                    UnloadModelAnimations(animation.clips, animation.count);
                }

                model_store->animations.clear();

                for (auto &[id, model] : model_store->models)
                {
                    UnloadModel(model);
                }

                model_store->models.clear();

                CloseWindow();

                return 0.0;
            });
    }
}