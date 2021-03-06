/*
    Copyright (C) 2010-2012 Florian Jung
     
    This file is part of flo's FM synth.

    flo's FM synth is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    flo's FM synth is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with flo's FM synth.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#include <jack/jack.h>

#include <string>

#include "programs.h"
#include "channel.h"

using namespace std;


extern fixed_t **lfo[N_LFOS];
extern fixed_t *curr_lfo[N_LFOS+1];

extern fixed_t* wave[N_WAVEFORMS];

extern fixed_t sample_and_hold[N_LFO_LEVELS];

extern int sample_and_hold_frames;
extern int lfo_res[N_LFOS];
extern int lfo_phase[N_LFOS];
extern float lfo_freq_hz[N_LFOS];

extern bool verbose;
extern bool fatal_warnings;
extern bool quiet;

extern bool connect_audio, connect_midi;

#ifdef WATCHFILES
extern bool watchfiles;
#endif

extern float cleanup_interval_sec;
extern float snh_freq_hz;
extern float max_port_time_sec;

extern float filter_update_freq_hz;
extern float lfo_update_freq_hz;
extern float envelope_update_freq_hz;

extern int filter_update_frames;
extern int lfo_update_frames;
extern int envelope_update_frames;

extern float xrun_time;
extern int xrun_n;

#ifndef FRAMESKIP
	extern int samp_rate;
#else
	extern int frameskip;
	extern int samp_rate;

	extern jack_default_audio_sample_t outtemp[N_CHANNELS];
	#ifdef STEREO
		extern jack_default_audio_sample_t outtemp2[N_CHANNELS];
	#endif

	extern jack_nframes_t outtemp_nframes_left;
#endif


extern string programfile[128];



extern program_t *program_settings;
extern bool program_lock[128];

extern program_t default_program;

extern Channel *channel[N_CHANNELS];


extern jack_nframes_t cleanup_interval; //in jack frames


#endif
