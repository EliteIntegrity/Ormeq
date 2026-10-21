Optional Ormeq title music

There is no music track in the source distribution. The game works normally
without one. The separately supplied playable edition can install title.wav
under lib/xtra/music/pack/ beside the executable's other data.

Runtime format: PCM16 little-endian WAV, mono or stereo, 44.1 or 48 kHz,
up to ten minutes and 128 MiB. An original master may use a different format;
convert a delivery copy rather than modifying the master.

Music loops on the title screen, fades out on starting/loading a character,
and has independent volume/mute controls under Settings > Sound > Title music.
It pauses when the window loses focus. No music plays during gameplay.
Missing, unsupported or damaged tracks leave the game silent and playable.

The official track is Ormeq - Home Theme (Dark), supplied by EliteIntegrity.
The delivery WAV preserves the supplied master's duration and dynamics,
converted from 24-bit FLAC to PCM16 with triangular dither. Its natural ending
and quiet tail are retained before the track restarts.

pack-manifest.json records the exact delivery WAV hash and byte size; the
playable inventory includes it. Keep the WAV and production masters out of
Git/source exports. Replacements need a new manifest and packaging review.
See licenses/Ormeq-Audio.txt for the separate audio terms.
