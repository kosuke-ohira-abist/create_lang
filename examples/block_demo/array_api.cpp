#include "native_function.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace lang
{
    namespace
    {
        using Args = std::span<const VmValue>;

        struct ArrayStore
        {
            std::int64_t next_id = 1;
            std::unordered_map<std::int64_t, std::vector<std::int64_t>> arrays;
        };

        std::vector<std::int64_t> &find_array(ArrayStore &store, std::int64_t id)
        {
            const auto it = store.arrays.find(id);
            if (it == store.arrays.end())
            {
                throw std::runtime_error("Invalid array handle");
            }
            return it->second;
        }

        std::size_t checked_index(const std::vector<std::int64_t> &array,
                                  std::int64_t index)
        {
            if (index < 0 || static_cast<std::uint64_t>(index) >= array.size())
            {
                throw std::runtime_error("Array index out of range");
            }
            return static_cast<std::size_t>(index);
        }
    }

    void register_array_functions(NativeFunctionRegistry &registry)
    {
        using T = TypeDecl;
        auto store = std::make_shared<ArrayStore>();

        registry.register_function(
            "array_create", {{T::I64}, T::I64},
            [store](Args args) -> VmValue
            {
                const auto length = std::get<std::int64_t>(args[0]);
                if (length < 0 ||
                    static_cast<std::uint64_t>(length) >
                        std::vector<std::int64_t>{}.max_size())
                {
                    throw std::runtime_error("Invalid array length");
                }

                if (store->next_id == std::numeric_limits<std::int64_t>::max())
                {
                    throw std::runtime_error("Array handle exhausted");
                }

                std::vector<std::int64_t> values(
                    static_cast<std::size_t>(length), 0);
                const auto id = store->next_id;
                store->arrays.emplace(id, std::move(values));
                ++store->next_id;
                return id;
            });

        registry.register_function(
            "array_get", {{T::I64, T::I64}, T::I64},
            [store](Args args) -> VmValue
            {
                auto &array = find_array(*store,
                                         std::get<std::int64_t>(args[0]));
                const auto index = checked_index(array,
                                                 std::get<std::int64_t>(args[1]));
                return array[index];
            });

        registry.register_function(
            "array_set", {{T::I64, T::I64, T::I64}, T::F64},
            [store](Args args) -> VmValue
            {
                auto &array = find_array(*store,
                                         std::get<std::int64_t>(args[0]));
                const auto index = checked_index(array,
                                                 std::get<std::int64_t>(args[1]));
                array[index] = std::get<std::int64_t>(args[2]);
                return 0.0;
            });

        registry.register_function(
            "array_destroy", {{T::I64}, T::F64},
            [store](Args args) -> VmValue
            {
                const auto id = std::get<std::int64_t>(args[0]);
                if (store->arrays.erase(id) != 1)
                {
                    throw std::runtime_error("Invalid array handle");
                }
                return 0.0;
            });
    }
}
