// Stub config.h for MSVC builds of libsidplayfp
// Replaces autotools-generated config.h

#ifndef LIBSIDPLAYFP_CONFIG_H
#define LIBSIDPLAYFP_CONFIG_H

#define PACKAGE "libsidplayfp"
#define PACKAGE_NAME "libsidplayfp"
#define PACKAGE_VERSION "2.16.0"
#define VERSION "2.16.0"
#define PACKAGE_URL "https://github.com/libsidplayfp/libsidplayfp/"

// C++ standard feature flags (MSVC reports __cplusplus as 199711L without /Zc:__cplusplus)
#define HAVE_CXX11
#define HAVE_CXX14
#define HAVE_CXX17
#define HAVE_CXX20

#endif // LIBSIDPLAYFP_CONFIG_H
