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
 * Threading: callbacks may be invoked concurrently from multiple background
 * threads. User code must synchronize its own state. String pointers passed to
 * callbacks are valid for the duration of the callback only.
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

SPOTIFYCTL_API spotifyctl_client *spotifyctl_new(void);
SPOTIFYCTL_API void               spotifyctl_free(spotifyctl_client *c);
SPOTIFYCTL_API void               spotifyctl_start(spotifyctl_client *c);
SPOTIFYCTL_API void               spotifyctl_stop(spotifyctl_client *c);
SPOTIFYCTL_API int                spotifyctl_is_running(const spotifyctl_client *c);

/* -------------------------------------------------------------------------- */
/* Controls — return 1 on success, 0 on failure.                              */
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
 * 0 on failure. Strings in *out remain valid until the next call to
 * spotifyctl_latest_state on the same client, or until spotifyctl_free. */
SPOTIFYCTL_API int spotifyctl_latest_state(spotifyctl_client *c,
                                           spotifyctl_playback_state *out);

/* Writes a JSON object describing the latest state into buf. Returns the
 * number of bytes that would have been written (excluding the terminating
 * NUL), in the same spirit as snprintf. If the buffer is too small the output
 * is truncated but always NUL-terminated (when cap > 0). */
SPOTIFYCTL_API size_t spotifyctl_latest_state_json(spotifyctl_client *c,
                                                   char *buf, size_t cap);

/* -------------------------------------------------------------------------- */
/* Callbacks                                                                  */
/* -------------------------------------------------------------------------- */

typedef void (*spotifyctl_state_cb)  (const spotifyctl_playback_state *state, void *user);
typedef void (*spotifyctl_bool_cb)   (int value, void *user);
typedef void (*spotifyctl_string_cb) (const char *utf8, void *user);
typedef void (*spotifyctl_void_cb)   (void *user);

SPOTIFYCTL_API spotifyctl_token spotifyctl_on_state_changed  (spotifyctl_client *c,
                                                              spotifyctl_state_cb cb,
                                                              void *user);
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
SPOTIFYCTL_API void             spotifyctl_disconnect        (spotifyctl_client *c,
                                                              spotifyctl_token token);

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
