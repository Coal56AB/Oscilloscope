/*
 * Keep the bundled SDL2 compatible with older TinkerOS/Debian releases.
 * ARM glibc changed the exported logf/powf symbol version in 2.27, while the
 * double precision variants retain their older ABI. SDL only uses these
 * wrappers for its public math helpers.
 */
#include <math.h>

__attribute__((visibility("hidden"))) float logf(float value)
{
    return (float)log((double)value);
}

__attribute__((visibility("hidden"))) float powf(float base, float exponent)
{
    return (float)pow((double)base, (double)exponent);
}
