/* Embedded 250ms A5 (880Hz) MIDI tone for nab.tone() - GENERATED, do not edit:
 *   task lua:firmware:gen:tone   (tools/tonegen.py: a Format-0 SMF, no encoder)
 * A Standard MIDI File, NOT MP3 and NOT raw PCM WAV: the VS1003B decodes
 * MIDI natively (#331 - 45 B here where the MP3 predecessor was 2,160), and it
 * does not decode PCM WAV at all. Byte-identical to what ../lib/audio/midi.lua
 * builds for audio.midi.note("A5", 250), so the resident tone and a Lua-side
 * jingle cannot drift; test/host/tone_test.c pins every byte to the SMF spec. */
static const unsigned char nab_tone_midi[45] = {
  77,84,104,100,0,0,0,6,0,0,0,1,1,224,77,84,
  114,107,0,0,0,23,0,255,81,3,7,161,32,0,192,9,
  0,144,81,100,129,112,128,81,64,0,255,47,0,
};
