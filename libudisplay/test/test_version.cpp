/**
 * Version macros in libudisplay/udisplay.h.
 *
 * The header defaults to 0.0.0; a CMake build passes the configured version
 * as compile definitions and the framework install patches it into the
 * header. Either way the macros, the encoded value and the library's
 * udisplay_version() must agree.
 */
#include <gtest/gtest.h>
#include "libudisplay/udisplay.h"
#include <cctype>
#include <string>

TEST(Version, LibraryStringMatchesHeader)
{
    EXPECT_STREQ(udisplay_version(), UDISPLAY_VERSION_STRING);
}

TEST(Version, StringStartsWithNumericTriple)
{
    const std::string triple = std::to_string(UDISPLAY_VERSION_MAJOR) + "." +
                               std::to_string(UDISPLAY_VERSION_MINOR) + "." +
                               std::to_string(UDISPLAY_VERSION_PATCH);
    const std::string full = UDISPLAY_VERSION_STRING;
    ASSERT_EQ(full.compare(0, triple.size(), triple), 0) << full;
    /* Anything after the triple is a suffix such as "-rc1", never more digits. */
    if (full.size() > triple.size()) {
        EXPECT_FALSE(isdigit(static_cast<unsigned char>(full[triple.size()])))
            << full;
    }
}

TEST(Version, EncodedValue)
{
    EXPECT_EQ(UDISPLAY_VERSION_ENCODE(1, 2, 3), 10203);
    EXPECT_EQ(UDISPLAY_VERSION,
              UDISPLAY_VERSION_ENCODE(UDISPLAY_VERSION_MAJOR,
                                      UDISPLAY_VERSION_MINOR,
                                      UDISPLAY_VERSION_PATCH));
#if UDISPLAY_VERSION < UDISPLAY_VERSION_ENCODE(0, 0, 0)
#error "UDISPLAY_VERSION must be usable in #if"
#endif
}
