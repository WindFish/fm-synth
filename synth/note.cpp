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


#include <cmath>

#include "note.h"
#include "globals.h"
#include "defines.h"

using namespace std;

//this function returns the smallest phase_init possible for a
//given custom_wave which is greater or equal than PHASE_INIT
inline fixed_t init_custom_osc_phase(int len, fixed_t sr)
{
	return ( (fixed_t(ceil( float(PHASE_INIT) * sr / len / ONE )) *len << (2*SCALE)) / sr);
}


Note::Note(int n, float v, program_t &prg, jack_nframes_t pf, fixed_t pb, int prg_no, float vol_fac)
{	
	curr_prg=&prg;
	
		
	n_oscillators=prg.n_osc;
	
	
	pfactor.out=new fixed_t [n_oscillators];
	pfactor.freq_env_amount=new fixed_t [n_oscillators];
	pfactor.fm=new fixed_t* [n_oscillators];
	for (int i=0;i<n_oscillators;++i)
		pfactor.fm[i]=new fixed_t [n_oscillators];
	
	freqfactor_factor=new double[n_oscillators];
	envval=new fixed_t[n_oscillators];
	oscval=new fixed_t[n_oscillators];
	old_oscval=new fixed_t[n_oscillators];
	
	for (int i=0;i<n_oscillators;++i)
		envval[i]=oscval[i]=old_oscval[i]=0;
	
	envelope=new Envelope*[n_oscillators];
	factor_env=new Envelope*[n_oscillators];
	
	for (int i=0;i<n_oscillators;++i)
	{
		envelope[i]=new Envelope(prg.env_settings[i], envelope_update_frames);
		factor_env[i]=new Envelope(prg.osc_settings[i].freq_env, envelope_update_frames);
	}
	
	oscillator=new oscillator_t[n_oscillators];
	orig.oscillator=new oscillator_t[n_oscillators];
	copy(&prg.osc_settings[0],&prg.osc_settings[n_oscillators],oscillator);
	copy(&prg.osc_settings[0],&prg.osc_settings[n_oscillators],orig.oscillator);
	
	oscillator_phase_increment=new fixed_t[n_oscillators];
	
	fm_oscs=new list<int>[n_oscillators];
	for (int i=0;i<n_oscillators;++i)
		for (int j=0;j<n_oscillators;++j)
			if (oscillator[i].fm_strength[j]!=0)
				fm_oscs[i].push_back(j);
	
	//initalize oscillator.phase to multiples of their wave resolution
	//this has the following effect: the actual phase, i.e. the index
	//in the wave-array (wave[phase]) doesn't change, because
	// (n * wave_res) % wave_res is always zero.
	//however, if doing phase modulation, it's very unlikely now that
	//phase ever becomes negative (which would cause the program to
	//segfault, or at least to produce noise). this saves an additional
	//(slow) sanity check for the phase.
	for (int i=0;i<n_oscillators;++i)
	{
		if (oscillator[i].custom_wave)
			oscillator[i].phase=init_custom_osc_phase(oscillator[i].custom_wave->wave_len, oscillator[i].custom_wave->samp_rate);
		else
			oscillator[i].phase=ONE * PHASE_INIT;
	}
	
		
	do_ksl();
	
	
	filter_params=prg.filter_settings;
	orig.filter_params=prg.filter_settings;

	if (filter_params.enabled)
	{
		filter_envelope=new Envelope(filter_params.env_settings,1);
		filter_update_counter=filter_update_frames;
	}

	env_frame_counter=envelope_update_frames; //force update in first frame
	

	sync_factor=prg.sync_factor;
	sync_phase=0;




	portamento_frames=0;
	set_portamento_frames(pf);
		
	set_note(n);
	freq=dest_freq;
	set_vel(v);
	set_vol_factor(vol_fac);
	
	pitchbend=pb;
	
	program=prg_no;

	recalc_actual_freq();
}

Note::~Note()
{
	int i;
	
	for (i=0;i<n_oscillators;++i)
	{
		delete [] oscillator[i].fm_strength;
		delete envelope[i];
		delete factor_env[i];
		
		delete [] pfactor.fm[i];
	}
	
	delete [] oscillator;
	delete [] oscillator_phase_increment;
	delete [] envelope;
	delete [] factor_env;
	
	delete [] freqfactor_factor;
	delete [] envval;
	delete [] oscval;
	delete [] old_oscval;

	delete [] pfactor.out;
	delete [] pfactor.freq_env_amount;
	delete [] pfactor.fm;

	delete [] fm_oscs;
}

void Note::recalc_factors()
{
	pfactor.filter_env=calc_pfactor(curr_prg->pfactor.filter_env, vel);
	pfactor.filter_res=calc_pfactor(curr_prg->pfactor.filter_res, vel);
	pfactor.filter_offset=calc_pfactor(curr_prg->pfactor.filter_offset, vel);
	
	for (int i=0;i<n_oscillators;++i)
	{
		pfactor.out[i]=calc_pfactor(curr_prg->pfactor.out[i], vel) * volume_factor;
		pfactor.freq_env_amount[i]=calc_pfactor(curr_prg->pfactor.freq_env_amount[i], vel);

		for (int j=0;j<n_oscillators;++j)
			pfactor.fm[i][j]=calc_pfactor(curr_prg->pfactor.fm[i][j], vel);
	}
}

void Note::apply_pfactor()
{
	//apply pfactor to all necessary parameters
	for (int i=0;i<n_oscillators;++i)
	{
		oscillator[i].output=orig.oscillator[i].output*pfactor.out[i] >>SCALE;
		oscillator[i].freq_env_amount=orig.oscillator[i].freq_env_amount*pfactor.freq_env_amount[i] /ONE; //because it's a float
		
		fm_oscs[i].clear();
		for (int j=0;j<n_oscillators;++j)
		{
			oscillator[i].fm_strength[j]=orig.oscillator[i].fm_strength[j]*pfactor.fm[i][j] >>SCALE;
			if (oscillator[i].fm_strength[j]!=0)
				fm_oscs[i].push_back(j);
		}
	}
	filter_params.env_amount=orig.filter_params.env_amount*pfactor.filter_env /ONE;
	filter_params.freqfactor_offset=orig.filter_params.freqfactor_offset*pfactor.filter_offset /ONE;
	filter_params.resonance=orig.filter_params.resonance*pfactor.filter_res /ONE;	
}

void Note::set_param(const parameter_t &p, fixed_t v) //ACHTUNG:
{ 
	//wenn das verändert wird, muss auch program_t::set_param verändert werden!
	switch(p.par)
	{
		case ATTACK: envelope[p.osc]->set_attack(v*samp_rate >>SCALE); break;
		case DECAY: envelope[p.osc]->set_decay(v*samp_rate >>SCALE); break;
		case SUSTAIN: envelope[p.osc]->set_sustain(v); break;
		case RELEASE: envelope[p.osc]->set_release(v*samp_rate >>SCALE); break;
		case HOLD: envelope[p.osc]->set_hold(v!=0); break;

		case KSR: oscillator[p.osc].ksr=float(v)/ONE; break;
		case KSL: oscillator[p.osc].ksl=float(v)/ONE; break;

		case FACTOR: orig.oscillator[p.osc].factor=pow(2.0, (double)v/12.0/ONE)*ONE;
		             oscillator[p.osc].factor=v*freqfactor_factor[p.osc];
		             recalc_oscillator_phase_increment(p.osc);
		             break;
		case MODULATION: orig.oscillator[p.osc].fm_strength[p.index]=v; apply_pfactor(); break;
		case OUTPUT: orig.oscillator[p.osc].output=v; apply_pfactor(); break;
		case TREMOLO: oscillator[p.osc].tremolo_depth=v; break;
		case TREM_LFO: oscillator[p.osc].tremolo_lfo=v; break;
		case VIBRATO: oscillator[p.osc].vibrato_depth=v; break;
		case VIB_LFO: oscillator[p.osc].vibrato_lfo=v; break;
		case WAVEFORM: oscillator[p.osc].waveform=v; break;
		case SYNC: oscillator[p.osc].sync=(v!=0); break;

		case FILTER_ENABLED: output_note("NOTE: cannot enable filter in playing notes"); break;
		case FILTER_ENV_AMOUNT: orig.filter_params.env_amount=float(v)/ONE; apply_pfactor(); break;

		case FILTER_ATTACK:
			if (filter_params.enabled)
				filter_envelope->set_attack(v*samp_rate/filter_update_frames >>SCALE);
			else
				output_note("NOTE: cannot set filter-attack when filter is disabled");
			break;

		case FILTER_DECAY:
			if (filter_params.enabled)
				filter_envelope->set_decay(v*samp_rate/filter_update_frames >>SCALE);
			else
				output_note("NOTE: cannot set filter-decay when filter is disabled");
			break;

		case FILTER_SUSTAIN:
			if (filter_params.enabled)
				filter_envelope->set_sustain(v);
			else
				output_note("NOTE: cannot set filter-sustain when filter is disabled");
			break;

		case FILTER_RELEASE:
			if (filter_params.enabled)
				filter_envelope->set_release(v*samp_rate/filter_update_frames >>SCALE);
			else
				output_note("NOTE: cannot set filter-release when filter is disabled");
			break;

		case FILTER_HOLD:
			if (filter_params.enabled)
				filter_envelope->set_hold(v!=0);
			else
				output_note("NOTE: cannot set filter-hold when filter is disabled");
			break;

		case FILTER_OFFSET: orig.filter_params.freqfactor_offset=float(v)/ONE; apply_pfactor(); break;
		case FILTER_RESONANCE: orig.filter_params.resonance=float(v)/ONE; apply_pfactor(); break;
		case FILTER_TREMOLO: filter_params.trem_strength=v; break;
		case FILTER_TREM_LFO: filter_params.trem_lfo=v; break;
		
		case SYNC_FACTOR: sync_factor=pow(2.0, (double)v/12.0/ONE)*ONE; sync_phase_increment=(actual_freq*sync_factor/samp_rate) >> SCALE; break;

		case FREQ_ATTACK: factor_env[p.osc]->set_attack(v*samp_rate >>SCALE); break;
		case FREQ_DECAY: factor_env[p.osc]->set_decay(v*samp_rate >>SCALE); break;
		case FREQ_SUSTAIN: factor_env[p.osc]->set_sustain(v); break;
		case FREQ_RELEASE: factor_env[p.osc]->set_release(v*samp_rate >>SCALE); break;
		case FREQ_HOLD: factor_env[p.osc]->set_hold((v!=0)); break;
		case FREQ_ENV_AMOUNT: orig.oscillator[p.osc].freq_env_amount=double(v)/ONE; apply_pfactor(); break;

		default: throw string("trying to set an unknown parameter");

	}
}

bool Note::still_active()
{
	for (int i=0; i<n_oscillators; ++i)
		if ((oscillator[i].output>0) && (envelope[i]->still_active()))
			return true;
	
	return false;
}


//this function must still work properly if called multiple times
//when called a second time, there shall be no effect
void Note::release_quickly(jack_nframes_t maxt)
{
	for (int i=0;i<n_oscillators;++i)
	{
		if (envelope[i]->get_release() > maxt)
			envelope[i]->set_release(maxt);
			
		envelope[i]->release_key();
		
		// i don't release the filter-env because lacking to do so
		// does not generate a hearable difference (or would you hear
		// when in the last half second a tone is filtered or not?)
	}
}

void Note::release()
{
	for (int i=0;i<n_oscillators;++i)
	{
		envelope[i]->release_key();
		factor_env[i]->release_key();
	}
	
	if (filter_params.enabled)
		filter_envelope->release_key();
}

void Note::reattack()
{
	for (int i=0;i<n_oscillators;++i)
	{
		envelope[i]->reattack();	
		factor_env[i]->reset();
	}

	if (filter_params.enabled)
		filter_envelope->reattack();
}


void Note::do_ksl()
{ //osc.ksl is in Bel/octave (i.e. dB/10)
  //if ksl=1, this means that for each octave the loudness
  //decreases by half
	for (int i=0;i<n_oscillators;++i)
	{
		if (oscillator[i].ksl==0)
			envelope[i]->set_max(ONE);
		else
			envelope[i]->set_max( fixed_t(double(ONE) / pow(freq>>SCALE, oscillator[i].ksl)) );
	}
}

void Note::do_ksr()
{
	for (int i=0;i<n_oscillators;++i)
		envelope[i]->set_ratefactor(1.0 / pow(dest_freq>>SCALE, oscillator[i].ksr));
}

void Note::recalc_actual_freq()
{
	actual_freq=freq*pitchbend >>SCALE;
	
	sync_phase_increment=(actual_freq*sync_factor/samp_rate) >> SCALE;
	
	for (int i=0;i<n_oscillators;++i)
		recalc_oscillator_phase_increment(i);
}

void Note::recalc_oscillator_phase_increment(int osc)
{
	oscillator_phase_increment[osc]=(actual_freq*oscillator[osc].factor/samp_rate)>>SCALE;
}

fixed_t Note::get_sample()
{
	// maybe BOTTLENECK: possible optimisation: only execute every N frames
	if (freq!=dest_freq)
	{
		// the div.by.zero if p_frames=0 is avoided because then the 
		// if-condition below would always be true
		if (portamento_t>=portamento_frames)
			freq=dest_freq;
		else //will only happen if p_t < p_frames -> p_frames is always > 0 -> div. ok
			freq = old_freq + (dest_freq-old_freq)*portamento_t/portamento_frames;
		
		recalc_actual_freq();
		
		do_ksl();
		
		++portamento_t;
	}

	fixed_t *temp;
	temp=old_oscval;   //swap the current and old oscval-pointers
	old_oscval=oscval;
	oscval=temp;
	
	fixed_t fm=0;
	fixed_t out=0;
	
	int i;
	
	if (sync_factor)
	{
		sync_phase+=sync_phase_increment;
		// phase-increment depends on:
		// - actual_freq (which depends on freq and pitchbend)
		//       steadily updated while portamento-ing and whenever a pitchbend comes in
		// - sync_factor: only updated manually
		// - samp_rate: never changes
		
		if (sync_phase >= ONE)
		{
			sync_phase-=ONE;
			
			for (i=0;i<n_oscillators;++i)
				if (oscillator[i].sync)
				{
					if (oscillator[i].custom_wave)
						oscillator[i].phase=init_custom_osc_phase(oscillator[i].custom_wave->wave_len, oscillator[i].custom_wave->samp_rate);
					else
						oscillator[i].phase=ONE * PHASE_INIT;
				}
		}
	}
	
	
	++env_frame_counter;
	if (env_frame_counter>=envelope_update_frames)
	{
		env_frame_counter=0;
		for (i=0;i<n_oscillators;++i)
		{
			envval[i]=envelope[i]->get_level();
			
			freqfactor_factor[i]=pow(2.0, oscillator[i].freq_env_amount*(factor_env[i]->get_level() - factor_env[i]->get_sustain())/ONE);
			oscillator[i].factor=orig.oscillator[i].factor*freqfactor_factor[i];
			recalc_oscillator_phase_increment(i);
		}
	}
	
	
	for (i=0;i<n_oscillators;++i)
	{
		fm=0;
		
		//iterate through all modulating oscs
		for (list<int>::iterator j=fm_oscs[i].begin(), end=fm_oscs[i].end(); j!=end; j++)		
			fm+=old_oscval[*j]*oscillator[i].fm_strength[*j];

		fm=fm>>SCALE;
		
		//phase increases in one second, i.e. in samp_rate frames, by the osc's freq
		if (oscillator[i].vibrato_depth!=0) // BOTTLENECK: update only on lfo recalculation
			oscillator[i].phase+=oscillator_phase_increment[i]*curr_lfo[oscillator[i].vibrato_lfo][oscillator[i].vibrato_depth] >>SCALE;
		else
			oscillator[i].phase+=oscillator_phase_increment[i];
			// phase-increment depends on:
			// - actual_freq (which depends on freq and pitchbend)
			//       steadily updated while portamento-ing and whenever a pitchbend comes in
			// - the vibrato-lfo: needs update whenever this lfo is updated
			// - factor (which depends on the freq envelope)
			//       steadily updated every env_frames frames
			// - samp_rate: never changes
			
		if (oscillator[i].custom_wave)
		{
			//sampler
			custom_wave_t *cw=oscillator[i].custom_wave;
			oscval[i]=cw->wave[ ((unsigned int)((oscillator[i].phase  +  fm) * cw->samp_rate >>(2*SCALE))) % cw->wave_len ] * envval[i]  >> (SCALE);
		}
		else
		{
			//normal oscillator
			//optimisation: the unsigned int cast avoids a slow 64bit modulo calculation. ca. 8% speed gain. same above.
			oscval[i]=wave[oscillator[i].waveform][ ((unsigned int)((oscillator[i].phase  +  fm) * WAVE_RES >>SCALE)) % WAVE_RES ] * envval[i] >> (SCALE);
		}

		if (oscillator[i].tremolo_depth!=0)
			oscval[i]=oscval[i]* curr_lfo[oscillator[i].tremolo_lfo][oscillator[i].tremolo_depth] >> SCALE;
		
		if (oscillator[i].output!=0)
			out+=oscillator[i].output*oscval[i];
	}
	
	out=out>>SCALE;
	
	if (filter_params.enabled)
	{
		++filter_update_counter;
		if (filter_update_counter>=filter_update_frames)
		{
			filter_update_counter=0;
			
			float cutoff= float(actual_freq)/ONE * 
			  float(curr_lfo[filter_params.trem_lfo][filter_params.trem_strength])/ONE *
			  ( filter_params.freqfactor_offset  +  filter_envelope->get_level() * filter_params.env_amount / float(ONE) );
			filter.set_params( cutoff, filter_params.resonance  );
		}
		
		fixed_t tmp=out;
		filter.process_sample(&tmp);
		return tmp;
	}
	else
	{
		return out;
	}
}

void Note::destroy()
{
	delete this;
}
