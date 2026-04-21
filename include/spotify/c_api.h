#ifndef LIBSPOTIFYCTL_C_API_H
#define LIBSPOTIFYCTL_C_API_H

/*
 * libspotifyctl — stable C ABI.
 *
 * This header is the single bridge used by Python (ctypes), Node (koffi), and
 * any future Rust/Go binding. It deliberately exposes only C-compatible types.
 *
 * Strings: all `const char*` returned by this API are UTF-8. Pointers stored
 * inside `spotifyctl_playback_state` are owned by the client and remain valid
 * until the next call to `spotifyctl_latest_state` on the same client or until
 * `spotifyctl_free`. Do not free them yourself.
 *
 * Threading: every function below is safe to call concurrently from multiple
 * threads (exceptions are called out per-function). Callbacks are dispatched
 * on Windows-owned background threads and may be invoked concurrently with
 * API calls on the same client. Do not call `spotifyctl_free` from inside a
 * callback. String pointers passed to callbacks are valid only for the
 * duration of the callback.
 *
 * Error model for control functions (spotifyctl_play, _pause, _next, _seek_ms,
 * etc.): return value is `1` on success, `0` on failure. `0` is deliberately
 * coarse and conflates several conditions:
 *   - the client pointer is NULL,
 *   - Spotify is not currently running (no tracked window),
 *   - the requested capability is unavailable (e.g. `seek_ms` requires SMTC,
 *     which is absent on Windows < 10 1809 and briefly absent while the
 *     Spotify session initializes), or
 *   - the underlying Win32 / WinRT call failed.
 * Callers that need to distinguish "Spotify not running" from "call ignored"
 * should check `spotifyctl_is_running()` and, for capability-gated calls,
 * inspect the `can_seek` / `can_skip_next` / `can_skip_prev` flags on the
 * latest state. No errno-style code is exposed; retrying is safe but not
 * generally useful without one of those preconditions changing.
 */

#include <stddef.h>
#include <stdint.h>

#include "spotify/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spotifyctl_client spotifyctl_client;
typedef size_t                   spotifyctl_token;

typedef enum {
    SPOTIFYCTL_STATUS_UNKNOWN        = 0,
    SPOTIFYCTL_STATUS_STOPPED        = 1,
    SPOTIFYCTL_STATUS_PAUSED         = 2,
    SPOTIFYCTL_STATUS_PLAYING        = 3,
    SPOTIFYCTL_STATUS_CHANGING_TRACK = 4
} spotifyctl_status;

typedef enum {
    SPOTIFYCTL_CMD_STOP        = 0,
    SPOTIFYCTL_CMD_PLAY        = 1,
    SPOTIFYCTL_CMD_PAUSE       = 2,
    SPOTIFYCTL_CMD_PLAY_PAUSE  = 3,
    SPOTIFYCTL_CMD_NEXT        = 4,
    SPOTIFYCTL_CMD_PREV        = 5,
    SPOTIFYCTL_CMD_VOL_UP      = 6,
    SPOTIFYCTL_CMD_VOL_DOWN    = 7,
    SPOTIFYCTL_CMD_MUTE        = 8
} spotifyctl_command;

typedef struct {
    spotifyctl_status status;

    /* UTF-8; owned by client, valid until next latest_state / free. */
    const char *artist;
    const char *title;
    const char *album;

    int64_t position_ms;
    int64_t duration_ms;

    /* Album art thumbnail, usually JPEG. NULL when unavailable. */
    const uint8_t *album_art;
    size_t         album_art_len;

    int can_seek;
    int can_skip_next;
    int can_skip_prev;
    int is_ad;
    int audible;
    int app_muted;

    float app_volume;   /* [0,1] or -1 when unresolved. */
} spotifyctl_playback_state;

/* -------------------------------------------------------------------------- */
/* Library-level                                                              */
/* -------------------------------------------------------------------------- */

SPOTIFYCTL_API const char *spotifyctl_version(void);

/* -------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* -------------------------------------------------------------------------- */

/* spotifyctl_new: returns NULL on allocation failure. Otherwise the client is
 * idle until spotifyctl_start() is called. Callers own the returned pointer
 * and must release it with spotifyctl_free(). */
SPOTIFYCTL_API spotifyctl_client *spotifyctl_new(void);

/* spotifyctl_free: stops the client if running, disconnects any remaining
 * callbacks, and releases all resources. MUST NOT be called from inside a
 * callback on the same client. Safe to pass NULL. */
SPOTIFYCTL_API void               spotifyctl_free(spotifyctl_client *c);

/* Start/Stop are idempotent no-ops when already in the requested state. Safe
 * to pass NULL. After spotifyctl_stop() returns, no further callbacks will
 * fire for this client. */
SPOTIFYCTL_API void               spotifyctl_start(spotifyctl_client *c);
SPOTIFYCTL_API void               spotifyctl_stop(spotifyctl_client *c);

/* spotifyctl_is_running: 1 when a Spotify main window is currently tracked,
 * 0 otherwise (including when c is NULL). Thread-safe. Note that the answer
 * can change at any moment as Spotify is opened or closed by the user. */
SPOTIFYCTL_API int                spotifyctl_is_running(const spotifyctl_client *c);

/* -------------------------------------------------------------------------- */
/* Controls — return 1 on success, 0 on failure (see error model above).      */
/*                                                                            */
/* play / pause / next / previous prefer the SMTC channel and fall back to    */
/* media-key WM_APPCOMMAND when SMTC is unavailable; they may therefore       */
/* succeed even without SMTC. seek_ms is SMTC-only: it returns 0 when SMTC    */
/* is not bound (Windows < 10 1809, or briefly during session setup).         */
/*                                                                            */
/* send_command is the raw media-key channel — the per-app volume / mute      */
/* variants here affect the OS master volume, not Spotify's per-app slider.   */
/* Use spotifyctl_set_app_volume / _set_app_muted for per-app control.        */
/* -------------------------------------------------------------------------- */

SPOTIFYCTL_API int spotifyctl_play        (spotifyctl_client *c);
SPOTIFYCTL_API int spotifyctl_pause       (spotifyctl_client *c);
SPOTIFYCTL_API int spotifyctl_next        (spotifyctl_client *c);
SPOTIFYCTL_API int spotifyctl_previous    (spotifyctl_client *c);
SPOTIFYCTL_API int spotifyctl_seek_ms     (spotifyctl_client *c, int64_t position_ms);
SPOTIFYCTL_API int spotifyctl_send_command(spotifyctl_client *c, spotifyctl_command cmd);
SPOTIFYCTL_API int spotifyctl_open_uri    (spotifyctl_client *c, const char *utf8_uri);
SPOTIFYCTL_API int spotifyctl_send_key    (spotifyctl_client *c, unsigned int virtual_key);

/* -------------------------------------------------------------------------- */
/* Per-app audio                                                              */
/*                                                                            */
/* The per-app Core Audio session only exists after Spotify first outputs     */
/* sound. Until then, get_app_volume returns -1, is_app_muted returns 0,      */
/* and the setters return 0. The library rebinds the session automatically   */
/* when Spotify starts producing audio.                                       */
/* -------------------------------------------------------------------------- */

SPOTIFYCTL_API float spotifyctl_get_app_volume   (const spotifyctl_client *c);
SPOTIFYCTL_API int   spotifyctl_set_app_volume   (spotifyctl_client *c, float volume);
SPOTIFYCTL_API int   spotifyctl_is_app_muted     (const spotifyctl_client *c);
SPOTIFYCTL_API int   spotifyctl_set_app_muted    (spotifyctl_client *c, int muted);
SPOTIFYCTL_API float spotifyctl_get_peak_amplitude(const spotifyctl_client *c);

/* -------------------------------------------------------------------------- */
/* State snapshot                                                             */
/* -------------------------------------------------------------------------- */

/* Fills *out with the latest unified playback state. Returns 1 on success,
 * 0 on failure (c or out is NULL). Always returns 1 when both pointers are
 * non-NULL — a fresh client returns a skeleton state (status=UNKNOWN, empty
 * strings, zero durations) rather than an error.
 *
 * Strings and album-art bytes inside *out are owned by the client and remain
 * valid until the NEXT call to spotifyctl_latest_state on the same client, or
 * until spotifyctl_free. Do not call this function concurrently on the same
 * client if any thread is still reading pointers from a previous out struct.
 * Other calls to this client remain safe to perform concurrently. */
SPOTIFYCTL_API int spotifyctl_latest_state(spotifyctl_client *c,
                                           spotifyctl_playback_state *out);

/* Writes a JSON object describing the latest state into buf. Returns the
 * number of bytes that would have been written (excluding the terminating
 * NUL), in the same spirit as snprintf — so callers may call once with
 * buf=NULL, cap=0 to size the buffer. If the buffer is too small the output
 * is truncated but always NUL-terminated (when cap > 0). Returns 0 when c is
 * NULL. Thread-safe. */
SPOTIFYCTL_API size_t spotifyctl_latest_state_json(spotifyctl_client *c,
                                                   char *buf, size_t cap);

/* -------------------------------------------------------------------------- */
/* Callbacks                                                                  */
/*                                                                            */
/* Each on_* function returns a non-zero token on success, 0 on failure (c or */
/* cb is NULL). Pass the token back to spotifyctl_disconnect to unregister.   */
/* Callbacks fire on Windows background threads. Do NOT call spotifyctl_free  */
/* or spotifyctl_stop from inside a callback on the same client. Any other    */
/* API call on the client is safe. Exceptions thrown by a callback (e.g. a   */
/* Python KeyboardInterrupt crossing the FFI) are undefined behavior — trap   */
/* them in your callback.                                                     */
/* -------------------------------------------------------------------------- */

typedef void (*spotifyctl_state_cb)  (const spotifyctl_playback_state *state, void *user);
typedef void (*spotifyctl_bool_cb)   (int value, void *user);
typedef void (*spotifyctl_string_cb) (const char *utf8, void *user);
typedef void (*spotifyctl_void_cb)   (void *user);
typedef void (*spotifyctl_position_cb)(int64_t position_ms, void *user);
typedef void (*spotifyctl_track_changed_cb)(const spotifyctl_playback_state *previous,
                                            const spotifyctl_playback_state *current,
                                            void *user);

SPOTIFYCTL_API spotifyctl_token spotifyctl_on_state_changed  (spotifyctl_client *c,
                                                              spotifyctl_state_cb cb,
                                                              void *user);

/* Like spotifyctl_on_state_changed, but also invokes `cb` synchronously with
 * the current state exactly once before returning — atomically, under the
 * signal's lock, so a concurrent emit on another thread cannot interleave and
 * produce a duplicated or dropped initial update. Use this when the caller
 * needs "current + all future" semantics (UI frameworks, Node EventEmitters)
 * without racing connect() against spotifyctl_latest_state. */
SPOTIFYCTL_API spotifyctl_token spotifyctl_on_state_changed_with_replay(
    spotifyctl_client *c, spotifyctl_state_cb cb, void *user);

SPOTIFYCTL_API spotifyctl_token spotifyctl_on_audible_changed(spotifyctl_client *c,
                                                              spotifyctl_bool_cb cb,
                                                              void *user);
SPOTIFYCTL_API spotifyctl_token spotifyctl_on_raw_title      (spotifyctl_client *c,
                                                              spotifyctl_string_cb cb,
                                                              void *user);
SPOTIFYCTL_API spotifyctl_token spotifyctl_on_opened         (spotifyctl_client *c,
                                                              spotifyctl_void_cb cb,
                                                              void *user);
SPOTIFYCTL_API spotifyctl_token spotifyctl_on_closed         (spotifyctl_client *c,
                                                              spotifyctl_void_cb cb,
                                                              void *user);

/* Fires on every (artist, title, album) tuple change, including startup
 * (empty→populated) and shutdown (populated→empty). Callers get both the
 * previous and current state pointers; both are valid for the callback
 * duration only. */
SPOTIFYCTL_API spotifyctl_token spotifyctl_on_track_changed  (spotifyctl_client *c,
                                                              spotifyctl_track_changed_cb cb,
                                                              void *user);

/* Fires on is_ad false→true / true→false edges respectively. */
SPOTIFYCTL_API spotifyctl_token spotifyctl_on_ad_started     (spotifyctl_client *c,
                                                              spotifyctl_void_cb cb,
                                                              void *user);
SPOTIFYCTL_API spotifyctl_token spotifyctl_on_ad_ended       (spotifyctl_client *c,
                                                              spotifyctl_void_cb cb,
                                                              void *user);

/* Fires at ~1 Hz while playback is Playing AND at least one listener is
 * connected. Zero overhead when unsubscribed. Delivers the same value as
 * spotifyctl_latest_position_smooth_ms. */
SPOTIFYCTL_API spotifyctl_token spotifyctl_on_position_changed(spotifyctl_client *c,
                                                               spotifyctl_position_cb cb,
                                                               void *user);

SPOTIFYCTL_API void             spotifyctl_disconnect        (spotifyctl_client *c,
                                                              spotifyctl_token token);

/* Current position with monotonic-clock extrapolation applied while playback
 * is Playing. When not Playing (Paused / Stopped / Unknown / ChangingTrack),
 * returns the raw anchor position (same value as
 * spotifyctl_latest_state().position_ms).
 *
 * Extrapolation only advances across SMTC updates that look like real
 * discontinuities (play/pause/seek/track edges). Tens-of-ms skew between
 * SMTC's clock and the monotonic clock is absorbed so the progress does not
 * visibly step on every SMTC republish. Returns 0 when c is NULL or when
 * SMTC has not yet published anything on this client. Thread-safe. */
SPOTIFYCTL_API int64_t spotifyctl_latest_position_smooth_ms(const spotifyctl_client *c);

/* -------------------------------------------------------------------------- */
/* URI builders                                                               */
/* -------------------------------------------------------------------------- */

/* All return heap-allocated UTF-8 C strings. Free with spotifyctl_free_string.
 * Returns NULL on allocation failure. */
SPOTIFYCTL_API char *spotifyctl_uri_track   (const char *id);
SPOTIFYCTL_API char *spotifyctl_uri_album   (const char *id);
SPOTIFYCTL_API char *spotifyctl_uri_playlist(const char *id);
SPOTIFYCTL_API char *spotifyctl_uri_artist  (const char *id);
SPOTIFYCTL_API char *spotifyctl_uri_user    (const char *username);
SPOTIFYCTL_API char *spotifyctl_uri_search  (const char *query);
SPOTIFYCTL_API void  spotifyctl_free_string (char *s);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* LIBSPOTIFYCTL_C_API_H */
