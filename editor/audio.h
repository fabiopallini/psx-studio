#ifndef STUDIO_AUDIO_H
#define STUDIO_AUDIO_H
#include <gio/gio.h>
/* Desktop-only encoder. Output: mono VAGp, original rate, one-shot SPU ADPCM.
 * Returns the absolute imported file path, owned by the caller. */
char *audio_import_wav(const char *source, const char *directory, GError **error);
GBytes *audio_encode_wav(GBytes *wav, const char *name, GError **error);
#endif
