// Host-build shim for the device-only <unistd.h>.
//
// The application sources are compiled as-is (no copies, no edits). MSVC has no
// <unistd.h>, while the device toolchain (POSIX/uclibc) does. This header is
// therefore placed on the include path of the PC test build only; it maps the
// single POSIX call the config stores use (fsync) onto the MSVC equivalent.
//
// This shim is NOT part of the firmware and must never be added to the device
// build (the FlyThings toolchain provides the real <unistd.h>).
#ifndef TESTS_SHIM_UNISTD_H_
#define TESTS_SHIM_UNISTD_H_

#if defined(_MSC_VER)

#include <io.h>
#include <stdio.h>

// fflush() + fsync(fileno(fp)) is the device-side durability pattern; _commit()
// is the MSVC equivalent. fileno() is declared by <stdio.h>.
#ifndef fsync
#define fsync _commit
#endif

#else

#error "tests/shim is MSVC-only; POSIX toolchains must use the system <unistd.h>."

#endif

#endif  // TESTS_SHIM_UNISTD_H_
