/*
 * File: main.cpp
 * Author: Sean Gallogly
 * Created on: circa 07/21/2022
 * 
 * Description:
 *     this is the main entry point to the program. It calls functions from commandline.h
 *     in order to parse arguments and from control.h in order to run the simulation
 *     in one of several user-specified modes.
 *
 */

#include <time.h>
#include <iostream>
#include <fstream>
#include <omp.h>

#include "control.h"
#include "gui.h"
#include "commandline.h"
#include "file_parse.h"

int main(int argc, char **argv) 
{
	parsed_commandline p_cl = {};
	parse_commandline(&argc, &argv, p_cl); /* includes validation step */

	Control *control = new Control(p_cl);
	int exit_status = 0;

	omp_set_num_threads(1); /* for 4 gpus, 8 is the sweet spot. Unsure for 2. */

	if (!p_cl.build_file.empty())
	{
		control->build_sim();
		control->save_sim_to_file(p_cl.output_sim_file);
	}
	else if (!p_cl.session_file.empty())
	{
		if (p_cl.vis_mode == "TUI")
		{
			control->runSession(NULL);
			if (!p_cl.output_sim_file.empty())
			{
				std::cout << "[INFO]: Saving simulation to file...\n";
				control->save_sim_to_file(p_cl.output_sim_file);
			}
		}
		else if (p_cl.vis_mode == "GUI")
		{
			exit_status = gui_init_and_run(&argc, &argv, control);
		}
	}
	delete control;
	return exit_status;
}

