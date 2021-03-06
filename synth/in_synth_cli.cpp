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


#include <iostream>
#include <string>
#include <signal.h>
#include <stdlib.h>

#include "in_synth_cli.h"
#include "util.h"
#include "communication.h"
#include "globals.h"
#include "load.h"
#include "lfos.h"

#ifdef WATCHFILES
	#include "watch_files.h"
#endif

using namespace std;

#define PROMPT "> "

pthread_mutex_t prog_load_mutex;

void signal_handler(int sig)
{
	cout << endl << PROMPT << flush;
}

void lock_and_load_program(int prg_no, string file)
{
	pthread_mutex_lock(&prog_load_mutex);
	
	#ifdef WATCHFILES
		remove_watch(prg_no);
	#endif
	
	do_request(request_t(request_t::SUSPEND_PROGRAM, prg_no));
	
	if (load_program(file,program_settings[prg_no]))
	{
		cout << "success" << endl;
		programfile[prg_no]=file;

		#ifdef WATCHFILES
			add_watch(prg_no);
		#endif
	}
	else
		cout << "failed" << endl;
	
	for (int i=0;i<N_CHANNELS;++i)
		channel[i]->maybe_reload_program(prg_no);
	
	do_request(request_t(request_t::RESUME_PROGRAM, prg_no));

	pthread_mutex_unlock(&prog_load_mutex);
}

//only use this, if you don't want the file-watches to be updated
//i.e., only when reloading a program!
void lock_and_load_program_no_watch_updates(int prg_no, string file)
{
	pthread_mutex_lock(&prog_load_mutex);

	do_request(request_t(request_t::SUSPEND_PROGRAM, prg_no));
	
	if (load_program(file,program_settings[prg_no]))
	{
		cout << "success" << endl;
		programfile[prg_no]=file;
	}
	else
		cout << "failed" << endl;
	
	for (int i=0;i<N_CHANNELS;++i)
		channel[i]->maybe_reload_program(prg_no);
	
	do_request(request_t(request_t::RESUME_PROGRAM, prg_no));

	pthread_mutex_unlock(&prog_load_mutex);
}

void lock_and_change_lfo(int lfo_no, float freq)
{
	do_request(request_t(request_t::SUSPEND_PROGRAM, -1));
	
	uninit_lfo(lfo_no);
	lfo_freq_hz[lfo_no]=freq;
	init_lfo(lfo_no);
	
	do_request(request_t(request_t::RESUME_PROGRAM, -1));
}


void do_in_synth_cli()
{
	string input;
	string command;
	string params;
	int num;

	pthread_mutex_init(&prog_load_mutex, NULL);
	
	if (signal(2,signal_handler)==SIG_ERR)
		output_warning("WARNING: failed to set signal handler in the in-synth-cli. pressing ctrl+c will\n"
		               "         kill the synth, so be careful. this is not fatal");

	fatal_warnings=false;

	while (cin.good())
	{
		cout << PROMPT << flush;
		getline(cin,input);
		input=trim_spaces(input);
		
		command=trim_spaces(str_before(input,' ',input));
		params=trim_spaces(str_after(input,' ',""));
		
		if ((command=="exit") || (command=="quit"))
			break;
		else if (command=="reload")
		{
			if ((!isnum(params)) || (params==""))
				cout << "error: expected program-number, found '"<<params<<"'"<<endl;
			else
			{
				num=atoi(params.c_str());
				if ((num>=0) && (num<128))
					lock_and_load_program(num, programfile[num]);
				else
					cout << "error: program-number must be one of 0..127" << endl;
			}
		}
		else if (command=="load")
		{
			string prgstr, file;
			prgstr=trim_spaces(str_before(params,' ',params));
			file=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(prgstr)) || (prgstr==""))
				cout << "error: expected program-number, found '"<<prgstr<<"'"<<endl;
			else if (file=="")
				cout << "error: expected program-file to load, found nothing"<<endl;
			else
			{
				num=atoi(params.c_str());
				if ((num>=0) && (num<128))
				{
					lock_and_load_program(num, file);
					programfile[num]=file;
				}
				else
					cout << "error: program-number must be one of 0..127" << endl;
			}
		}
		else if (command=="panic")
		{
			if ((params=="") || (params=="all"))
				do_request(request_t(request_t::PANIC, -1));
			else if (isnum(params))
			{
				num=atoi(params.c_str());
				if ((num>=0) && (num<N_CHANNELS))
					do_request(request_t(request_t::PANIC, num));
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if (command=="release")
		{
			if ((params=="") || (params=="all"))
				do_request(request_t(request_t::RELEASE_ALL, -1));
			else if (isnum(params))
			{
				num=atoi(params.c_str());
				if ((num>=0) && (num<N_CHANNELS))
					do_request(request_t(request_t::RELEASE_ALL, num));
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		// TODO: from here, no proper synchronisation with the audio thread
		//       is done. use do_request() everywhere!
		else if (command=="kill_program")
		{
			string prgstr, chanstr;
			prgstr=trim_spaces(str_before(params,' ',params));
			chanstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(prgstr)) || (prgstr==""))
				cout << "error: expected program-number, found '"<<prgstr<<"'"<<endl;
			else if ((!isnum(chanstr)) && chanstr!="")
				cout << "error: expected channel or nothing, found '"<<chanstr<<"'"<<endl;
			else
			{
				num=atoi(prgstr.c_str());
				if ((num>=0) && (num<128))
				{
					if (chanstr=="")
						for (int i=0;i<N_CHANNELS;++i)
							channel[i]->kill_program(num);
					else
					{
						int num2=atoi(chanstr.c_str());
						if ((num2>=0)&&(num2<N_CHANNELS))
							channel[num2]->kill_program(num);
						else
							cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
					}
				}
				else
					cout << "error: program-number must be one of 0..127" << endl;
			}
		}
		else if ((command=="limit") || (command=="voice_limit") || (command=="set_voice_limit"))
		{
			string nstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			nstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if (! ((isnum(nstr)&&nstr!="") || (nstr=="none")))
				cout << "error: expected new limit or 'none', found '"<<nstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
				{
					int num2;
					if (nstr=="none")
						num2=0;
					else
						num2=atoi(nstr.c_str());
					
					if (num2>=0)
						channel[num]->set_n_voices(num2);
					else
						cout << "error: limit must be a positive number, zero or 'all'"<<endl;
				}
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if (command=="reset")
		{
			if ((params=="") || (params=="all"))
				for (int i=0;i<N_CHANNELS;++i)
					channel[i]->reset_controllers();
			else if (isnum(params))
			{
				num=atoi(params.c_str());
				if ((num>=0) && (num<N_CHANNELS))
					channel[num]->reset_controllers();
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="set_program") || (command=="program") || (command=="prog") || (command=="prg"))
		{
			string nstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			nstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if (! (isnum(nstr)&&nstr!="") )
				cout << "error: expected program, found '"<<nstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
				{
					int num2;
					num2=atoi(nstr.c_str());
				
					if ((num2>=0) && (num2<128))
						channel[num]->set_program(num2);
					else
						cout << "error: program must be one of 0..127"<<endl;
				}
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="set_controller") || (command=="controller") || (command=="cont") || (command=="cc"))
		{
			string nstr, chanstr, valstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			params=trim_spaces(str_after(params,' ',""));
			nstr=trim_spaces(str_before(params,' ',params));
			valstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if (! (isnum(nstr)&&nstr!="") )
				cout << "error: expected controller, found '"<<nstr<<"'"<<endl;
			else if (! (isnum(valstr)&&nstr!="") )
				cout << "error: expected value, found '"<<valstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
				{
					int num2;
					num2=atoi(nstr.c_str());
					int val=atoi(valstr.c_str());
				
					if (!((num2>=0) && (num2<128)))
						cout << "error: controller must be one of 0..127"<<endl;
					else if (!((val>=0) && (val<128)))
						cout << "error: value must be one of 0..127"<<endl;
					else
						channel[num]->set_controller(num2,val);
				}
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="set_portamento_time") || (command=="portamento_time") || (command=="port_t"))
		{
			string nstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			nstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if (! (isfloat(nstr)&&nstr!="") )
				cout << "error: expected time, found '"<<nstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
				{
					float num2;
					num2=atof(nstr.c_str());
				
					if (num2>=0)
						channel[num]->set_portamento_time_sec(num2);
					else
						cout << "error: portamento time must be positive"<<endl;
				}
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="portamento") || (command=="port"))
		{
			string onstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			onstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if ((onstr!="on")&&(onstr!="off"))
				cout << "error: expected 'on' or 'off', found '"<<onstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
				{
					if (onstr=="on")
						channel[num]->set_portamento(127);
					else //off
						channel[num]->set_portamento(0);
				}
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="set_volume") || (command=="volume") || (command=="vol"))
		{
			string nstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			nstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if (! (isnum(nstr)&&nstr!="") )
				cout << "error: expected volume, found '"<<nstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
				{
					int num2;
					num2=atoi(nstr.c_str());
				
					if ((num2>=0) && (num2<128))
						channel[num]->set_volume(num2);
					else
						cout << "error: volume must be one of 0..127"<<endl;
				}
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="set_balance") || (command=="balance") || (command=="bal"))
		{
			string nstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			nstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if (! (isnum(nstr)&&nstr!="") )
				cout << "error: expected balance, found '"<<nstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
				{
					int num2;
					num2=atoi(nstr.c_str());
				
					if ((num2>=0) && (num2<128))
						channel[num]->set_balance(num2);
					else
						cout << "error: balance must be one of 0..127"<<endl;
				}
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="hold_pedal") || (command=="hold"))
		{
			string onstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			onstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if ((onstr!="on")&&(onstr!="off"))
				cout << "error: expected 'on' or 'off', found '"<<onstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
					channel[num]->set_hold_pedal(onstr=="on");
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="sostenuto_pedal") || (command=="sost_pedal") || (command=="sostenuto") || (command=="sost"))
		{
			string onstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			onstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if ((onstr!="on")&&(onstr!="off"))
				cout << "error: expected 'on' or 'off', found '"<<onstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
					channel[num]->set_sostenuto_pedal(onstr=="on");
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="soft_pedal") || (command=="soft"))
		{
			string onstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			onstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if ((onstr!="on")&&(onstr!="off"))
				cout << "error: expected 'on' or 'off', found '"<<onstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
					channel[num]->set_soft_pedal(onstr=="on");
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="legato_pedal") || (command=="leg_pedal") || (command=="legato") || (command=="leg"))
		{
			string onstr, chanstr;
			chanstr=trim_spaces(str_before(params,' ',params));
			onstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(chanstr)) || (chanstr==""))
				cout << "error: expected channel, found '"<<chanstr<<"'"<<endl;
			else if ((onstr!="on")&&(onstr!="off"))
				cout << "error: expected 'on' or 'off', found '"<<onstr<<"'"<<endl;
			else
			{
				num=atoi(chanstr.c_str());
				if ((num>=0) && (num<N_CHANNELS))
					channel[num]->set_legato_pedal(onstr=="on");
				else
					cout << "error: channel-number must be one of 0.."<<N_CHANNELS-1<<endl;
			}
		}
		else if ((command=="change_lfo") || (command=="lfo") || (command=="set_lfo"))
		{
			string freqstr, lfostr;
			lfostr=trim_spaces(str_before(params,' ',params));
			freqstr=trim_spaces(str_after(params,' ',""));
			
			if ((!isnum(lfostr)) || (lfostr==""))
				cout << "error: expected lfo-number, found '"<<lfostr<<"'"<<endl;
			else if (! (isfloat(freqstr)&&freqstr!=""))
				cout << "error: expected frequency, found '"<<freqstr<<"'"<<endl;
			else
			{
				num=atoi(lfostr.c_str());
				if ((num>=0) && (num<N_LFOS))
				{
					float freq;
					freq=atof(freqstr.c_str());
					
					if (freq>0)
						lock_and_change_lfo(num,freq);
					else
						cout << "error: frequency must be a positive number"<<endl;
				}
				else
					cout << "error: lfo-number must be one of 0.."<<N_LFOS-1<<endl;
			}
		}
		else if ((command=="snh") || (command=="snh_freq") || (command=="sample_and_hold_freq") || (command=="set_sample_and_hold_freq"))
		{
			if (isfloat(params))
			{
				float freq=atof(params.c_str());
				if (freq>=0)
				{
					snh_freq_hz=freq;
					init_snh(); //no uninit necessary, as this only calculates an integer
					            //no lock necessary, as a race-condition would only cause
					            //  the snh be calculated some times more
				}
				else
					cout << "error: sample-and-hold-frequency must be greater than zero"<<endl;
			}
			else
				cout << "error: expected frequency, found '"<<params<<"'"<<endl;
		}
		else if (command!="")
		{
			cout << "error: unrecognized command '"<<command<<"'"<<endl;
		}
	}

	if (signal(2,SIG_DFL)==SIG_ERR)
		output_warning("WARNING: failed to reset signal handler in the in-synth-cli. you will not be\n"
		               "         able to kill the synth with ctrl+c, try sending SIGTERM instead");
}
