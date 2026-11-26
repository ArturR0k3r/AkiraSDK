/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * AkiraConsole Prod has no audio hardware and no joystick: the sound (FX_*,
 * MUSIC_*) and joystick APIs the game links against, doing nothing. The
 * vendored dummy_audiolib.c is commented out upstream and does not match
 * fx_man.h, so it is not used. No voice is ever available, so nothing plays.
 */
#include <stdint.h>

#include "SDL.h"
#include "fx_man.h"
#include "music.h"

char *FX_ErrorString(int ErrorNumber) { return ""; }
int FX_SetupCard(int SoundCard, fx_device *device) { return FX_Error; }
int FX_GetBlasterSettings(fx_blaster_config *blaster) { return FX_Error; }
int FX_SetupSoundBlaster(fx_blaster_config blaster, int *MaxVoices, int *MaxSampleBits,
			 int *MaxChannels) { return FX_Error; }
/* "Succeeds", or SoundStartup() ends the game; no voice is ever free. */
int FX_Init(int SoundCard, int numvoices, int numchannels, int samplebits,
	    unsigned mixrate) { return FX_Ok; }
int FX_Shutdown(void) { return FX_Ok; }
int FX_SetCallBack(void (*function)(int32_t)) { return FX_Ok; }
void FX_SetVolume(int volume) { }
void FX_SetReverseStereo(int setting) { }
int FX_GetReverseStereo(void) { return 0; }
void FX_SetReverb(int reverb) { }
void FX_SetReverbDelay(int delay) { }
int FX_VoiceAvailable(int priority) { return 0; }
int FX_EndLooping(int handle) { return FX_Ok; }
int FX_SetPan(int handle, int vol, int left, int right) { return FX_Ok; }
int FX_SetPitch(int handle, int pitchoffset) { return FX_Ok; }
int FX_PlayVOC(uint8_t *ptr, int pitchoffset, int vol, int left, int right, int priority,
	       uint32_t callbackval) { return FX_Error; }
int FX_PlayWAV(uint8_t *ptr, int pitchoffset, int vol, int left, int right, int priority,
	       uint32_t callbackval) { return FX_Error; }
int FX_PlayLoopedVOC(uint8_t *ptr, int32_t loopstart, int32_t loopend, int32_t pitchoffset,
		     int32_t vol, int32_t left, int32_t right, int32_t priority,
		     uint32_t callbackval) { return FX_Error; }
int FX_PlayLoopedWAV(uint8_t *ptr, int32_t loopstart, int32_t loopend, int32_t pitchoffset,
		     int32_t vol, int32_t left, int32_t right, int32_t priority,
		     uint32_t callbackval) { return FX_Error; }
int FX_PlayVOC3D(uint8_t *ptr, int32_t pitchoffset, int32_t angle, int32_t distance,
		 int32_t priority, uint32_t callbackval) { return FX_Error; }
int FX_PlayWAV3D(uint8_t *ptr, int pitchoffset, int angle, int distance, int priority,
		 uint32_t callbackval) { return FX_Error; }
int32_t FX_Pan3D(int handle, int angle, int distance) { return FX_Ok; }
int32_t FX_SoundActive(int32_t handle) { return 0; }
int32_t FX_StopSound(int handle) { return FX_Ok; }
int32_t FX_StopAllSounds(void) { return FX_Ok; }

char *MUSIC_ErrorString(int ErrorNumber) { return ""; }
int MUSIC_Init(int SoundCard, int Address) { return MUSIC_Ok; }
int MUSIC_Shutdown(void) { return MUSIC_Ok; }
void MUSIC_SetVolume(int volume) { }
void MUSIC_Continue(void) { }
void MUSIC_Pause(void) { }
int MUSIC_StopSong(void) { return MUSIC_Ok; }
int MUSIC_PlaySong(char *songData, int loopflag) { return MUSIC_Ok; }
int MUSIC_SongPlaying(void) { return 0; }
void MUSIC_RegisterTimbreBank(unsigned char *timbres) { }
void PlayMusic(char *filename) { }

int SDL_NumJoysticks(void) { return 0; }
SDL_Joystick *SDL_JoystickOpen(int device_index) { return NULL; }
void SDL_JoystickClose(SDL_Joystick *joystick) { }
const char *SDL_JoystickName(SDL_Joystick *joystick) { return ""; }
int SDL_JoystickNumAxes(SDL_Joystick *joystick) { return 0; }
int SDL_JoystickNumBalls(SDL_Joystick *joystick) { return 0; }
int SDL_JoystickNumHats(SDL_Joystick *joystick) { return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *joystick) { return 0; }
void SDL_JoystickUpdate(void) { }
int SDL_JoystickEventState(int state) { return 0; }
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis) { return 0; }
Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat) { return 0; }
Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button) { return 0; }
