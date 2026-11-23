#include "native_function.hpp"

#include <raylib.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lang
{
    void register_array_functions(NativeFunctionRegistry &registry);

    namespace
    {
        using Args = std::span<const VmValue>;

        int integer_arg(Args args, std::size_t index)
        {
            const double value = std::get<double>(args[index]);
            if (!std::isfinite(value) || std::trunc(value) != value ||
                value < static_cast<double>(std::numeric_limits<int>::min()) ||
                value > static_cast<double>(std::numeric_limits<int>::max()))
            {
                throw std::runtime_error("Invalid raylib integer argument");
            }
            return static_cast<int>(value);
        }

        Color rgb(Args args, std::size_t start)
        {
            const int r = integer_arg(args, start);
            const int g = integer_arg(args, start + 1);
            const int b = integer_arg(args, start + 2);
            if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
            {
                throw std::runtime_error("Invalid color component");
            }
            return Color{
                static_cast<unsigned char>(r),
                static_cast<unsigned char>(g),
                static_cast<unsigned char>(b),
                255};
        }
    }

    void register_host_functions(NativeFunctionRegistry &registry)
    {
        using T = TypeDecl;

        register_array_functions(registry);

        registry.register_function(
            "init_window", {{T::F64, T::F64, T::Str}, T::F64},
            [](Args args) -> VmValue
            {
                InitWindow(integer_arg(args, 0), integer_arg(args, 1),
                           std::get<std::string>(args[2]).c_str());
                if (!IsWindowReady())
                {
                    throw std::runtime_error("Window initialization failed");
                }
                return 0.0;
            });

        registry.register_function(
            "set_target_fps", {{T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                SetTargetFPS(integer_arg(args, 0));
                return 0.0;
            });

        registry.register_function(
            "window_should_close", {{}, T::F64},
            [](Args) -> VmValue
            { return WindowShouldClose() ? 1.0 : 0.0; });

        registry.register_function(
            "begin_drawing", {{}, T::F64},
            [](Args) -> VmValue
            { BeginDrawing(); return 0.0; });

        registry.register_function(
            "clear_background", {{T::F64, T::F64, T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                ClearBackground(rgb(args, 0));
                return 0.0;
            });

        registry.register_function(
            "end_drawing", {{}, T::F64},
            [](Args) -> VmValue
            { EndDrawing(); return 0.0; });

        registry.register_function(
            "close_window", {{}, T::F64},
            [](Args) -> VmValue
            { CloseWindow(); return 0.0; });

        registry.register_function(
            "draw_rectangle",
            {{T::F64, T::F64, T::F64, T::F64,
              T::F64, T::F64, T::F64},
             T::F64},
            [](Args args) -> VmValue
            {
                DrawRectangle(integer_arg(args, 0), integer_arg(args, 1),
                              integer_arg(args, 2), integer_arg(args, 3),
                              rgb(args, 4));
                return 0.0;
            });

        registry.register_function(
            "draw_rectangle_lines",
            {{T::F64, T::F64, T::F64, T::F64,
              T::F64, T::F64, T::F64},
             T::F64},
            [](Args args) -> VmValue
            {
                DrawRectangleLines(integer_arg(args, 0), integer_arg(args, 1),
                                   integer_arg(args, 2), integer_arg(args, 3),
                                   rgb(args, 4));
                return 0.0;
            });

        registry.register_function(
            "draw_text",
            {{T::Str, T::F64, T::F64, T::F64,
              T::F64, T::F64, T::F64},
             T::F64},
            [](Args args) -> VmValue
            {
                DrawText(std::get<std::string>(args[0]).c_str(),
                         integer_arg(args, 1), integer_arg(args, 2),
                         integer_arg(args, 3), rgb(args, 4));
                return 0.0;
            });

        registry.register_function(
            "get_frame_time", {{}, T::F64},
            [](Args) -> VmValue
            {
                return static_cast<double>(GetFrameTime());
            });

        registry.register_function(
            "is_key_pressed", {{T::F64}, T::F64},
            [](Args args) -> VmValue
            {
                const int key = integer_arg(args, 0);
                return IsKeyPressed(key) ? 1.0 : 0.0;
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
    }
}
