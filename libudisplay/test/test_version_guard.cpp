/**
 * UDISPLAY_MINIMUM_VERSION in libudisplay/udisplay.h.
 *
 * The guard reads the version macros when it is expanded, so this file
 * replaces the configured version (CMake compile definitions) to check a
 * release and a 0.0.0 git checkout in one build.
 */
#include <gtest/gtest.h>
#include "libudisplay/udisplay.h"

#undef UDISPLAY_VERSION_MAJOR
#undef UDISPLAY_VERSION_MINOR
#undef UDISPLAY_VERSION_PATCH

TEST(VersionGuard, Release)
{
#define UDISPLAY_VERSION_MAJOR 1
#define UDISPLAY_VERSION_MINOR 2
#define UDISPLAY_VERSION_PATCH 3
    EXPECT_TRUE(UDISPLAY_MINIMUM_VERSION(0, 0, 0));
    EXPECT_TRUE(UDISPLAY_MINIMUM_VERSION(1, 2, 2));
    EXPECT_TRUE(UDISPLAY_MINIMUM_VERSION(1, 2, 3));
    EXPECT_FALSE(UDISPLAY_MINIMUM_VERSION(1, 2, 4));
    EXPECT_FALSE(UDISPLAY_MINIMUM_VERSION(1, 3, 0));
    EXPECT_FALSE(UDISPLAY_MINIMUM_VERSION(2, 0, 0));
#if !UDISPLAY_MINIMUM_VERSION(1, 2, 3) || UDISPLAY_MINIMUM_VERSION(1, 2, 4)
#error "UDISPLAY_MINIMUM_VERSION must be usable in #if"
#endif
#undef UDISPLAY_VERSION_MAJOR
#undef UDISPLAY_VERSION_MINOR
#undef UDISPLAY_VERSION_PATCH
}

TEST(VersionGuard, GitCheckoutAcceptsAnyMinimum)
{
#define UDISPLAY_VERSION_MAJOR 0
#define UDISPLAY_VERSION_MINOR 0
#define UDISPLAY_VERSION_PATCH 0
    EXPECT_TRUE(UDISPLAY_MINIMUM_VERSION(0, 0, 0));
    EXPECT_TRUE(UDISPLAY_MINIMUM_VERSION(0, 0, 1));
    EXPECT_TRUE(UDISPLAY_MINIMUM_VERSION(99, 99, 99));
#if !UDISPLAY_MINIMUM_VERSION(99, 99, 99)
#error "UDISPLAY_MINIMUM_VERSION must accept a 0.0.0 checkout in #if"
#endif
#undef UDISPLAY_VERSION_MAJOR
#undef UDISPLAY_VERSION_MINOR
#undef UDISPLAY_VERSION_PATCH
}
