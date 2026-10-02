// tools/fuzz/fuzz_tuning.cpp
#include "tuning.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto result = parse_tuning(data, size);
    if (result && !valid_tuning(result.value()))
        std::abort();
    return 0;
}
