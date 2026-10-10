#include <cstdlib>

#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG >= 201703L, "Switch Auto V3 requires C++17");
#else
static_assert(__cplusplus >= 201703L, "Switch Auto V3 requires C++17");
#endif

int main() {
    return EXIT_SUCCESS;
}
