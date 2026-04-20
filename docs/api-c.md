# C ABI

The C ABI in `include/spotify/c_api.h` is the stable bridge used by every
non-C++ consumer (Python, Node, future Rust/Go). It only exposes C-compatible
types.

Headers need just:

```c
#include <spotify/c_api.h>
```

Link against `libspotifyctl.dll` (or the static archive with
`-DSPOTIFYCTL_STATIC_DEFINE`).

## Types

```c
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
    SPOTIFYCTL_CMD_STOP       = 0,
    SPOTIFYCTL_CMD_PLAY       = 1,
    SPOTIFYCTL_CMD_PAUSE      = 2,
    SPOTIFYCTL_CMD_PLAY_PAUSE = 3,
    SPOTIFYCTL_CMD_NEXT       = 4,
    SPOTIFYCTL_CMD_PREV       = 5,
    SPOTIFYCTL_CMD_VOL_UP     = 6,
    SPOTIFYCTL_CMD_VOL_DOWN   = 7,
    SPOTIFYCTL_CMD_MUTE       = 8
} spotifyctl_command;

typedef struct {
    spotifyctl_status status;
    const char       *artist, *title, *album;   /* UTF-8 */
    int64_t           position_ms, duration_ms;
    const uint8_t    *album_art;
    size_t            album_art_len;
    int               can_seek, can_skip_next, can_skip_prev,
                      is_ad, audible, app_muted;
    float             app_volume;   /* [0,1] or -1 */
} spotifyctl_playback_state;
```

## Lifetimes

- **Strings in `spotifyctl_playback_state`** are owned by the client and
  remain valid until the next call to `spotifyctl_latest_state` on the same
  client, or until `spotifyctl_free`. Do not free them yourself.
- **URI-builder return values** are heap-allocated. Free with
  `spotifyctl_free_string(ptr)`.
- **Strings in callbacks** are valid for the duration of the callback only.

## Threading

Callbacks may be invoked concurrently from multiple background threads. User
code must synchronize its own state.

## Functions

### Lifecycle

```c
const char* spotifyctl_version(void);
spotifyctl_client* spotifyctl_new(void);
void               spotifyctl_free(spotifyctl_client *c);
void               spotifyctl_start(spotifyctl_client *c);
void               spotifyctl_stop(spotifyctl_client *c);
int                spotifyctl_is_running(const spotifyctl_client *c);
```

### Transport — return `1` on success, `0` on failure

```c
int spotifyctl_play        (spotifyctl_client *c);
int spotifyctl_pause       (spotifyctl_client *c);
int spotifyctl_next        (spotifyctl_client *c);
int spotifyctl_previous    (spotifyctl_client *c);
int spotifyctl_seek_ms     (spotifyctl_client *c, int64_t position_ms);
int spotifyctl_send_command(spotifyctl_client *c, spotifyctl_command cmd);
int spotifyctl_open_uri    (spotifyctl_client *c, const char *utf8_uri);
int spotifyctl_send_key    (spotifyctl_client *c, unsigned int virtual_key);
```

### Per-app audio

```c
float spotifyctl_get_app_volume   (const spotifyctl_client *c);
int   spotifyctl_set_app_volume   (spotifyctl_client *c, float v);
int   spotifyctl_is_app_muted     (const spotifyctl_client *c);
int   spotifyctl_set_app_muted    (spotifyctl_client *c, int muted);
float spotifyctl_get_peak_amplitude(const spotifyctl_client *c);
```

### State

```c
int    spotifyctl_latest_state     (spotifyctl_client *c,
                                    spotifyctl_playback_state *out);
size_t spotifyctl_latest_state_json(spotifyctl_client *c,
                                    char *buf, size_t cap);
```

`spotifyctl_latest_state_json` behaves like `snprintf`: pass `(NULL, 0)` to
get the required size, then allocate `size + 1`.

### Callbacks

```c
typedef void (*spotifyctl_state_cb)  (const spotifyctl_playback_state*, void *user);
typedef void (*spotifyctl_bool_cb)   (int value, void *user);
typedef void (*spotifyctl_string_cb) (const char *utf8, void *user);
typedef void (*spotifyctl_void_cb)   (void *user);

spotifyctl_token spotifyctl_on_state_changed  (spotifyctl_client*, spotifyctl_state_cb,  void*);
spotifyctl_token spotifyctl_on_audible_changed(spotifyctl_client*, spotifyctl_bool_cb,   void*);
spotifyctl_token spotifyctl_on_raw_title      (spotifyctl_client*, spotifyctl_string_cb, void*);
spotifyctl_token spotifyctl_on_opened         (spotifyctl_client*, spotifyctl_void_cb,   void*);
spotifyctl_token spotifyctl_on_closed         (spotifyctl_client*, spotifyctl_void_cb,   void*);
void             spotifyctl_disconnect        (spotifyctl_client*, spotifyctl_token);
```

### URI builders

```c
char* spotifyctl_uri_track   (const char *id);
char* spotifyctl_uri_album   (const char *id);
char* spotifyctl_uri_playlist(const char *id);
char* spotifyctl_uri_artist  (const char *id);
char* spotifyctl_uri_user    (const char *username);
char* spotifyctl_uri_search  (const char *query);   /* percent-encodes spaces */
void  spotifyctl_free_string (char *s);
```

## Example

```c
#include <spotify/c_api.h>
#include <stdio.h>

int main(void) {
    spotifyctl_client *c = spotifyctl_new();
    spotifyctl_start(c);

    spotifyctl_playback_state s;
    if (spotifyctl_latest_state(c, &s)) {
        printf("[%d] %s - %s\n", s.status, s.artist, s.title);
    }

    spotifyctl_stop(c);
    spotifyctl_free(c);
    return 0;
}
```

See `examples/c_smoke/main.c` for the full runnable source.
