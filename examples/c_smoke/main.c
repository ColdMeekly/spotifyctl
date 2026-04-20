/*
 * c_smoke — trivial pure-C consumer of libspotifyctl's C ABI.
 *
 * Its real job is to fail the build if spotify/c_api.h or spotify/export.h
 * accidentally leak C++-only constructs.
 */

#include <stdio.h>
#include <stdlib.h>

#include "spotify/c_api.h"

int main(void) {
    printf("libspotifyctl %s\n", spotifyctl_version());

    spotifyctl_client *c = spotifyctl_new();
    if (!c) {
        fprintf(stderr, "spotifyctl_new failed\n");
        return 1;
    }

    char buf[512];
    size_t n = spotifyctl_latest_state_json(c, buf, sizeof(buf));
    printf("state (%zu bytes): %s\n", n, buf);

    char *uri = spotifyctl_uri_search("radiohead");
    if (uri) {
        printf("uri: %s\n", uri);
        spotifyctl_free_string(uri);
    }

    spotifyctl_free(c);
    return 0;
}
