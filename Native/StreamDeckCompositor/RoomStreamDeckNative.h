/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#ifdef _WIN32
# ifdef ROOM_SD_NATIVE_BUILD
#  define ROOM_SD_API __declspec(dllexport)
# else
#  define ROOM_SD_API
# endif
#else
# define ROOM_SD_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* C ABI: no allocator, STL object or exception crosses the /MT <-> /MD boundary.
 * All operations on a handle must run on its creating thread. Buffers belong to
 * the caller. The DLL is pinned for process lifetime because Frida owns threads. */
enum { ROOM_SD_ABI = 1, ROOM_SD_STATUS = 1, ROOM_SD_LAYOUT = 2,
       ROOM_SD_FRAME = 3, ROOM_SD_STOP = 4 };
enum { ROOM_SD_OK = 0, ROOM_SD_INVALID = 1, ROOM_SD_GUARD = 2,
       ROOM_SD_BUSY = 3, ROOM_SD_ATTACH = 4, ROOM_SD_RPC = 5 };
ROOM_SD_API uint32_t room_sd_abi(void);
/* Read-only process identity lookup. No Frida calls, hash checks, or attachment. */
ROOM_SD_API uint32_t room_sd_find_process(void);
/* Strict executable path, all three hashes and shared observer lock are checked
 * before attachment. pid=0 selects the sole matching Elgato process. */
ROOM_SD_API int room_sd_open(uint32_t pid, const char* absolute_lock_directory,
                            uint32_t fps, void** handle, char* error, uint32_t error_capacity);
ROOM_SD_API int room_sd_request(void* handle, uint32_t operation,
                               const uint8_t* data, uint32_t size,
                               char* result, uint32_t result_capacity,
                               char* error, uint32_t error_capacity);
/* Stops the override, waits up to 3 seconds for native restoration, then unloads
 * and detaches with separately bounded cancellation. Reports incomplete restore. */
ROOM_SD_API int room_sd_close(void* handle, char* error, uint32_t error_capacity);
#ifdef __cplusplus
}
#endif
