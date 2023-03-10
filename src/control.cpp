#include <iomanip>
#include <gtk/gtk.h>

#include "control.h"
#include "file_parse.h"
#include "tty.h"
#include "array_util.h"
#include "gui.h" /* tenuous inclide at best :pogO: */

const std::string BIN_EXT = "bin";
const std::string CELL_IDS[NUM_CELL_TYPES] = {"MF", "GR", "GO", "BC", "SC", "PC", "IO", "NC"}; 

Control::Control(parsed_commandline &p_cl)
{
	tokenized_file t_file;
	lexed_file l_file;
	if (!p_cl.build_file.empty())
	{
		visual_mode = "TUI";
		run_mode = "build";
		curr_build_file_name = p_cl.build_file;
		out_sim_file_name = p_cl.output_sim_file;
		parsed_build_file pb_file;
		tokenize_file(p_cl.build_file, t_file);
		lex_tokenized_file(t_file, l_file);
		parse_lexed_build_file(l_file, pb_file);
		if (!con_params_populated) populate_con_params(pb_file);
	}
	else if (!p_cl.session_file.empty())
	{
		visual_mode = p_cl.vis_mode;
		run_mode = "run";
		curr_sess_file_name = p_cl.session_file;
		curr_sim_file_name  = p_cl.input_sim_file;
		out_sim_file_name   = p_cl.output_sim_file;
		parsed_sess_file s_file;
		tokenize_file(curr_sess_file_name, t_file);
		lex_tokenized_file(t_file, l_file);
		parse_lexed_sess_file(l_file, s_file);
		translate_parsed_trials(s_file, td);
		trials_data_initialized = true;

		// TODO: move this somewhere else yike
		trialTime   = std::stoi(s_file.parsed_var_sections["trial_spec"].param_map["trialTime"].value);
		msPreCS     = std::stoi(s_file.parsed_var_sections["trial_spec"].param_map["msPreCS"].value);
		msPostCS    = std::stoi(s_file.parsed_var_sections["trial_spec"].param_map["msPostCS"].value);
		PSTHColSize = msPreCS + td.cs_lens[0] + msPostCS;

		set_plasticity_modes(p_cl);
		get_raster_filenames(p_cl.raster_files);
		get_psth_filenames(p_cl.psth_files);
		get_weights_filenames(p_cl.weights_files);
		init_sim(s_file, curr_sim_file_name);
	}
}

Control::~Control()
{
	// delete allocated trials_data memory
	if (trials_data_initialized) delete_trials_data(td);

	// delete all dynamic objects
	if (simState) delete simState;
	if (simCore)  delete simCore;
	if (mfFreq)   delete mfFreq;
	if (mfs)      delete mfs;

	// deallocate output arrays
	if (raster_arrays_initialized) delete_rasters();
	if (psth_arrays_initialized)   delete_psths();
	if (spike_sums_initialized)    delete_spike_sums();
}

void Control::build_sim()
{
	// TODO: create a separate function to create the state,
	// have the constructor allocate memory and initialize values
	if (!simState) simState = new CBMState(numMZones);
}

void Control::set_plasticity_modes(parsed_commandline &p_cl)
{
	if (p_cl.pfpc_plasticity == "off") pf_pc_plast = OFF;
	else if (p_cl.pfpc_plasticity == "graded") pf_pc_plast = GRADED;
	else if (p_cl.pfpc_plasticity == "dual") pf_pc_plast = DUAL;
	else if (p_cl.pfpc_plasticity == "cascade") pf_pc_plast = CASCADE;

	if (p_cl.mfnc_plasticity == "off") mf_nc_plast = OFF;
	else if (p_cl.mfnc_plasticity == "graded") mf_nc_plast = GRADED;
	/* TODO: implement cmdline functionality to enable these */
	else if (p_cl.mfnc_plasticity == "dual") mf_nc_plast = DUAL;
	else if (p_cl.mfnc_plasticity == "cascade") mf_nc_plast = CASCADE;
}

void Control::init_sim(parsed_sess_file &s_file, std::string in_sim_filename)
{
	std::cout << "[INFO]: Initializing simulation...\n";
	std::fstream sim_file_buf(in_sim_filename.c_str(), std::ios::in | std::ios::binary);
	read_con_params(sim_file_buf);
	populate_act_params(s_file);
	simState = new CBMState(numMZones, sim_file_buf);
	simCore  = new CBMSimCore(simState, gpuIndex, gpuP2);
	mfFreq   = new ECMFPopulation(num_mf, mfRandSeed, CSTonicMFFrac, CSPhasicMFFrac,
								  contextMFFrac, nucCollFrac, bgFreqMin, csbgFreqMin,
								  contextFreqMin, tonicFreqMin, phasicFreqMin, bgFreqMax,
								  csbgFreqMax, contextFreqMax, tonicFreqMax, phasicFreqMax,
								  collaterals_off, fracImport, secondCS, fracOverlap);
	mfs = new PoissonRegenCells(mfRandSeed, threshDecayTau, numMZones);
	initialize_rast_cell_nums();
	initialize_cell_spikes();
	initialize_rasters();
	initialize_psths();
	initialize_spike_sums();
	sim_file_buf.close();
	sim_initialized = true;
	std::cout << "[INFO]: Simulation initialized.\n";
}

void Control::reset_sim(std::string in_sim_filename)
{
	std::fstream sim_file_buf(in_sim_filename.c_str(), std::ios::in | std::ios::binary);
	read_con_params(sim_file_buf);
	//read_act_params(sim_file_buf);
	simState->readState(sim_file_buf);
	// TODO: simCore, mfFreq, mfs
	
	reset_rasters();
	reset_psths();
	reset_spike_sums();
	sim_file_buf.close();
	curr_sim_file_name = in_sim_filename;
	// TODO: more things to reset?
}

void Control::save_sim_to_file(std::string outSimFile)
{
	std::fstream outSimFileBuffer(outSimFile.c_str(), std::ios::out | std::ios::binary);
	write_con_params(outSimFileBuffer);
	if (!simCore) simState->writeState(outSimFileBuffer);
	else simCore->writeState(outSimFileBuffer);
	outSimFileBuffer.close();
}

void Control::save_pfpc_weights_to_file(std::string out_pfpc_file)
{
	// TODO: make a boolean on weights loaded
	if (!simCore)
	{
		fprintf(stderr, "[ERROR]: Trying to write uninitialized weights to file.\n");
		fprintf(stderr, "[ERROR]: (Hint: Try initializing a sim or loading the weights first.)\n");
		return;
	}
	const float *pfpc_weights = simCore->getMZoneList()[0]->exportPFPCWeights();
	std::fstream outPFPCFileBuffer(out_pfpc_file.c_str(), std::ios::out | std::ios::binary);
	rawBytesRW((char *)pfpc_weights, num_gr * sizeof(float), false, outPFPCFileBuffer);
	outPFPCFileBuffer.close();
}

void Control::load_pfpc_weights_from_file(std::string in_pfpc_file)
{
	if (!simCore)
	{
		fprintf(stderr, "[ERROR]: Trying to read weights to uninitialized simulation.\n");
		fprintf(stderr, "[ERROR]: (Hint: Try initializing a sim first.)\n");
		return;
	}
	std::fstream inPFPCFileBuffer(in_pfpc_file.c_str(), std::ios::in | std::ios::binary);
	simCore->getMZoneList()[0]->load_pfpc_weights_from_file(inPFPCFileBuffer);
	inPFPCFileBuffer.close();
} 

void Control::save_mfdcn_weights_to_file(std::string out_mfdcn_file)
{
	if (!simCore)
	{
		fprintf(stderr, "[ERROR]: Trying to write uninitialized weights to file.\n");
		fprintf(stderr, "[ERROR]: (Hint: Try initializing a sim or loading the weights first.)\n");
		return;
	}
	// TODO: make a export function for mfdcn weights
	const float *mfdcn_weights = simCore->getMZoneList()[0]->exportMFDCNWeights();
	std::fstream outMFDCNFileBuffer(out_mfdcn_file.c_str(), std::ios::out | std::ios::binary);
	rawBytesRW((char *)mfdcn_weights, num_nc * num_p_nc_from_mf_to_nc * sizeof(const float), false, outMFDCNFileBuffer);
	outMFDCNFileBuffer.close();
}

void Control::load_mfdcn_weights_from_file(std::string in_mfdcn_file)
{
	if (!simCore)
	{
		fprintf(stderr, "[ERROR]: Trying to read weights to uninitialized simulation.\n");
		fprintf(stderr, "[ERROR]: (Hint: Try initializing a sim first.)\n");
		return;
	}
	std::fstream inMFDCNFileBuffer(in_mfdcn_file.c_str(), std::ios::in | std::ios::binary);
	simCore->getMZoneList()[0]->load_mfdcn_weights_from_file(inMFDCNFileBuffer);
	inMFDCNFileBuffer.close();
}

// TODO: combine two below funcs into one for generality
void Control::get_raster_filenames(std::map<std::string, std::string> &raster_files)
{
	if (!raster_files.empty())
	{
		for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
		{
			if (raster_files.find(CELL_IDS[i]) != raster_files.end())
			{
				rf_names[i] = raster_files[CELL_IDS[i]];
			}
		}
	}
}

void Control::get_psth_filenames(std::map<std::string, std::string> &psth_files)
{
	if (!psth_files.empty())
	{
		for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
		{
			if (psth_files.find(CELL_IDS[i]) != psth_files.end())
			{
				pf_names[i] = psth_files[CELL_IDS[i]];
			}
		}
	}
}

void Control::get_weights_filenames(std::map<std::string, std::string> &weights_files)
{
	if (!weights_files.empty())
	{
		if (weights_files.find("PFPC") != weights_files.end())
		{
			pf_pc_weights_file = weights_files["PFPC"];
		}
		if (weights_files.find("MFNC") != weights_files.end())
		{
			mf_nc_weights_file = weights_files["MFNC"];
		}
	}
}

void Control::initialize_rast_cell_nums()
{
	rast_cell_nums[MF] = num_mf;
	rast_cell_nums[GR] = num_gr; /* may need to change this to lower value for gui */
	rast_cell_nums[GO] = num_go;
	rast_cell_nums[BC] = num_bc;
	rast_cell_nums[SC] = num_sc;
	rast_cell_nums[PC] = num_pc;
	rast_cell_nums[IO] = num_io;
	rast_cell_nums[NC] = num_nc;
}

void Control::initialize_cell_spikes()
{
	cell_spks[MF] = mfs->getAPs();
	/* NOTE: incurs a call to cudaMemcpy from device to host, but initializing so is not repeatedly called */
	cell_spks[GR] = simCore->getInputNet()->exportAPGR(); 
	cell_spks[GO] = simCore->getInputNet()->exportAPGO(); 
	cell_spks[BC] = simCore->getMZoneList()[0]->exportAPBC(); 
	cell_spks[SC] = simCore->getMZoneList()[0]->exportAPSC();
	cell_spks[PC] = simCore->getMZoneList()[0]->exportAPPC();
	cell_spks[IO] = simCore->getMZoneList()[0]->exportAPIO();
	cell_spks[NC] = simCore->getMZoneList()[0]->exportAPNC();
}

void Control::initialize_spike_sums()
{
	/* TODO: remove */
	spike_sums[MF].num_cells  = num_mf;
	spike_sums[GR].num_cells  = num_gr;
	spike_sums[GO].num_cells  = num_go;
	spike_sums[BC].num_cells  = num_bc;
	spike_sums[SC].num_cells  = num_sc;
	spike_sums[PC].num_cells  = num_pc;
	spike_sums[IO].num_cells  = num_io;
	spike_sums[NC].num_cells = num_nc;

	FOREACH(spike_sums, ssp)
	{
		ssp->non_cs_spike_sum = 0;
		ssp->cs_spike_sum     = 0;
		ssp->non_cs_spike_counter = new uint32_t[ssp->num_cells];
		ssp->cs_spike_counter = new uint32_t[ssp->num_cells];
		memset((void *)ssp->non_cs_spike_counter, 0, ssp->num_cells * sizeof(uint32_t));
		memset((void *)ssp->cs_spike_counter, 0, ssp->num_cells * sizeof(uint32_t));
	}
	spike_sums_initialized = true;
}

void Control::initialize_rasters()
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!rf_names[i].empty())
		{
			/* granules are saved every trial, so their raster size is num_gr x PSTHColSize */
			uint32_t column_size = (CELL_IDS[i] == "GR") ? PSTHColSize : PSTHColSize * td.num_trials;
			rasters[i] = allocate2DArray<uint8_t>(rast_cell_nums[i], column_size);
		}
	}

	// TODO: find a way to initialize only within gui mode
	pc_vm_raster = allocate2DArray<float>(num_pc, PSTHColSize);
	nc_vm_raster = allocate2DArray<float>(num_nc, PSTHColSize);
	io_vm_raster = allocate2DArray<float>(num_io, PSTHColSize);

	raster_arrays_initialized = true;
}

void Control::initialize_psths()
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!pf_names[i].empty())
			psths[i] = allocate2DArray<uint8_t>(rast_cell_nums[i], PSTHColSize);
	}
	psth_arrays_initialized = true;
}

void Control::runSession(struct gui *gui)
{
	float medTrials;
	double start, end;
	int goSpkCounter[num_go];
	if (gui == NULL) run_state = IN_RUN_NO_PAUSE;
	trial = 0;
	raster_counter = 0;
	while (trial < td.num_trials && run_state != NOT_IN_RUN)
	{
		std::string trialName = td.trial_names[trial];

		uint32_t useCS        = td.use_css[trial];
		uint32_t onsetCS      = pre_collection_ts + td.cs_onsets[trial];
		uint32_t csLength     = td.cs_lens[trial];
		uint32_t percentCS    = td.cs_percents[trial];
		uint32_t useUS        = td.use_uss[trial];
		uint32_t onsetUS      = pre_collection_ts + td.us_onsets[trial];
		
		int PSTHCounter = 0;
		float gGRGO_sum = 0;
		float gMFGO_sum = 0;

		memset(goSpkCounter, 0, num_go * sizeof(int));

		std::cout << "[INFO]: Trial number: " << trial + 1 << "\n";
		start = omp_get_wtime();
		for (int ts = 0; ts < trialTime; ts++)
		{
			if (useUS == 1 && ts == onsetUS) /* deliver the US */
			{
				simCore->updateErrDrive(0, 0.3);
			}
			if (ts >= onsetCS && ts < onsetCS + csLength)
			{
				mfAP = (useCS == 1) ? mfs->calcPoissActivity(mfFreq->getMFInCSTonicA(), simCore->getMZoneList())
									: mfs->calcPoissActivity(mfFreq->getMFBG(), simCore->getMZoneList());
			}
			else
			{
				mfAP = mfs->calcPoissActivity(mfFreq->getMFBG(),
					  simCore->getMZoneList());
			}
			
			bool *isTrueMF = mfs->calcTrueMFs(mfFreq->getMFBG()); /* only used for mfdcn plasticity */
			simCore->updateTrueMFs(isTrueMF);
			simCore->updateMFInput(mfAP);
			simCore->calcActivity(spillFrac, pf_pc_plast, mf_nc_plast); 
			//update_spike_sums(ts, onsetCS, onsetCS + csLength);

			if (ts >= onsetCS && ts < onsetCS + csLength)
			{
				mfgoG  = simCore->getInputNet()->exportgSum_MFGO();
				grgoG  = simCore->getInputNet()->exportgSum_GRGO();
				goSpks = simCore->getInputNet()->exportAPGO();
			
				for (int i = 0; i < num_go; i++)
				{
					goSpkCounter[i] += goSpks[i];
					gGRGO_sum       += grgoG[i];
					gMFGO_sum       += mfgoG[i];
				}
			}
			
			/* upon offset of CS, report what we got*/
			if (ts == onsetCS + csLength)
			{
				countGOSpikes(goSpkCounter, medTrials);
				std::cout << "[INFO]: Mean gGRGO   = " << gGRGO_sum / (num_go * csLength) << "\n";
				std::cout << "[INFO]: Mean gMFGO   = " << gMFGO_sum / (num_go * csLength) << "\n";
				std::cout << "[INFO]: GR:MF ratio  = " << gGRGO_sum / gMFGO_sum << "\n";
			}
			
			/* data collection */
			if (ts >= onsetCS - msPreCS && ts < onsetCS + csLength + msPostCS)
			{
				fill_rasters(raster_counter, PSTHCounter, gui);
				fill_psths(PSTHCounter);
				PSTHCounter++;
				raster_counter++;
			}

			if (gui != NULL)
			{
				if (gtk_events_pending()) gtk_main_iteration();
			}
		}
		end = omp_get_wtime();
		std::cout << "[INFO]: '" << trialName << "' took " << (end - start) << "s.\n";
		
		if (gui != NULL)
		{
			// for now, compute the mean and median firing rates for all cells if win is visible
			if (firing_rates_win_visible(gui))
			{
				calculate_firing_rates(onsetCS, onsetCS + csLength);
				gdk_threads_add_idle((GSourceFunc)update_fr_labels, gui);
			}
			if (run_state == IN_RUN_PAUSE)
			{
				std::cout << "[INFO]: Simulation is paused at end of trial " << trial+1 << ".\n";
				while(run_state == IN_RUN_PAUSE)
				{
					if (gtk_events_pending()) gtk_main_iteration();
				}
				std::cout << "[INFO]: Continuing...\n";
			}
			//reset_spike_sums();
		}
		// save gr rasters into new file every trial 
		save_gr_raster();
		save_weights();
		trial++;
	}
	if (run_state == NOT_IN_RUN) std::cout << "[INFO]: Simulation terminated.\n";
	else if (run_state == IN_RUN_NO_PAUSE) std::cout << "[INFO]: Simulation Completed.\n";
	
	if (gui == NULL)
	{
		save_rasters();
		save_psths();
	}
	run_state = NOT_IN_RUN;
}

void Control::reset_spike_sums()
{
		for (int i = 0; i < NUM_CELL_TYPES; i++)
		{
			spike_sums[i].cs_spike_sum = 0;
			spike_sums[i].non_cs_spike_sum = 0;
			memset((void *)(spike_sums[i].non_cs_spike_counter), 0, spike_sums[i].num_cells * sizeof(uint32_t));
			memset((void *)(spike_sums[i].cs_spike_counter), 0, spike_sums[i].num_cells * sizeof(uint32_t));
		}
}

void Control::reset_rasters()
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!rf_names[i].empty())
		{
			uint32_t column_size = (CELL_IDS[i] == "GR") ? PSTHColSize : (PSTHColSize * td.num_trials);
			memset(rasters[i][0], '\000', rast_cell_nums[i] * column_size * sizeof(uint8_t));
		}
	}
}

void Control::reset_psths()
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!pf_names[i].empty())
		{
			memset(psths[i][0], '\000', rast_cell_nums[i] * PSTHColSize * sizeof(uint8_t));
		}
	}
}

void gen_gr_sample(int gr_indices[], int sample_size, int data_size)
{
	CRandomSFMT0 randGen(0); // replace seed later
	bool chosen[data_size] = {false}; 
	int counter = 0;
	while (counter < sample_size)
	{
		int index = randGen.IRandom(0, data_size - 1);
		if (!chosen[index])
		{
			gr_indices[counter] = index;
			chosen[index] = true;
			counter++;
		} 
	}
}

void Control::save_weights()
{
	if (!pf_pc_weights_file.empty())
	{
		std::string trial_pfpc_weights_name = OUTPUT_DATA_PATH + get_file_basename(pf_pc_weights_file)
											+ "_trial_" + std::to_string(trial) + "." + BIN_EXT;
		std::cout << "[INFO]: Saving granule to purkinje weights to file...\n";
		save_pfpc_weights_to_file(trial_pfpc_weights_name);
	}
	if (!mf_nc_weights_file.empty())
	{
		std::string trial_mfnc_weights_name = OUTPUT_DATA_PATH + get_file_basename(mf_nc_weights_file)
											+ "_trial_" + std::to_string(trial) + "." + BIN_EXT;
		std::cout << "[INFO]: Saving mossy fiber to deep nucleus weigths to file...\n";
		save_mfdcn_weights_to_file(trial_mfnc_weights_name);
	}
}

void Control::save_gr_raster()
{
	if (!rf_names[GR].empty())
	{
		std::string trial_raster_name = OUTPUT_DATA_PATH + get_file_basename(rf_names[GR])
									  + "_trial_" + std::to_string(trial) + "." + BIN_EXT;
		std::cout << "[INFO]: GR Raster file name: " << trial_raster_name << "\n";
		write2DArray<uint8_t>(trial_raster_name, rasters[GR], num_gr, PSTHColSize);
	}
}

void Control::save_rasters()
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!rf_names[i].empty() && CELL_IDS[i] != "GR")
		{
			std::cout << "[INFO]: Filling " << CELL_IDS[i] << " raster file...\n";
			write2DArray<uint8_t>(rf_names[i], rasters[i], rast_cell_nums[i], PSTHColSize * td.num_trials);
		}
	}
}

void Control::save_psths()
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!pf_names[i].empty())
		{
			std::cout << "[INFO]: Filling " << CELL_IDS[i] << " psth file...\n";
			write2DArray<uint8_t>(pf_names[i], psths[i], rast_cell_nums[i], PSTHColSize);
		}
	}
}

void Control::update_spike_sums(int tts, float onset_cs, float offset_cs)
{
	// update cs spikes
	if (tts >= onset_cs && tts < offset_cs)
	{
		for (int i = 0; i < NUM_CELL_TYPES; i++)
		{
			for (int j = 0; j < spike_sums[i].num_cells; j++)
			{
				spike_sums[i].cs_spike_sum += cell_spks[i][j];
				spike_sums[i].cs_spike_counter[j] += cell_spks[i][j];
			}
		}
	}
	// update non-cs spikes
	else if (tts < onset_cs)
	{
		for (int i = 0; i < NUM_CELL_TYPES; i++)
		{
			for (int j = 0; j < spike_sums[i].num_cells; j++)
			{
				spike_sums[i].non_cs_spike_sum += cell_spks[i][j];
				spike_sums[i].non_cs_spike_counter[j] += cell_spks[i][j];
			}
		}
	}
}


void Control::calculate_firing_rates(float onset_cs, float offset_cs)
{
	float non_cs_time_secs = (onset_cs - 1) / 1000.0; // why only pre-cs? (Ask Joe)
	float cs_time_secs = (offset_cs - onset_cs) / 1000.0;

	for (int i = 0; i < NUM_CELL_TYPES; i++)
	{
		// sort sums for medians 
		std::sort(spike_sums[i].non_cs_spike_counter,
			spike_sums[i].non_cs_spike_counter + spike_sums[i].num_cells);
		std::sort(spike_sums[i].cs_spike_counter,
			spike_sums[i].cs_spike_counter + spike_sums[i].num_cells);
		
		// calculate medians
		firing_rates[i].non_cs_median_fr =
			(spike_sums[i].non_cs_spike_counter[spike_sums[i].num_cells / 2 - 1]
		   + spike_sums[i].non_cs_spike_counter[spike_sums[i].num_cells / 2]) / (2.0 * non_cs_time_secs);
		firing_rates[i].cs_median_fr     =
			(spike_sums[i].cs_spike_counter[spike_sums[i].num_cells / 2 - 1]
		   + spike_sums[i].cs_spike_counter[spike_sums[i].num_cells / 2]) / (2.0 * cs_time_secs);
		
		// calculate means
		firing_rates[i].non_cs_mean_fr = spike_sums[i].non_cs_spike_sum / (non_cs_time_secs * spike_sums[i].num_cells);
		firing_rates[i].cs_mean_fr     = spike_sums[i].cs_spike_sum / (cs_time_secs * spike_sums[i].num_cells);
	}
}

void Control::countGOSpikes(int *goSpkCounter, float &medTrials)
{
	float isi = (td.us_onsets[0] - td.cs_onsets[0]) / 1000.0;
	std::sort(goSpkCounter, goSpkCounter + num_go);
	
	float m = (goSpkCounter[num_go / 2 - 1] + goSpkCounter[num_go / 2]) / 2.0;
	float goSpkSum = 0;

	for (int i = 0; i < num_go; i++) goSpkSum += goSpkCounter[i];

	// NOTE: 1.0s below should really be the isi
	std::cout << "[INFO]: Mean GO Rate: " << goSpkSum / ((float)num_go * isi) << std::endl;

	medTrials += m / isi;
	std::cout << "[INFO]: Median GO Rate: " << m / isi << std::endl;
}

void Control::fill_rasters(uint32_t raster_counter, uint32_t psth_counter, struct gui *gui)
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		uint32_t temp_counter = raster_counter;
		if (!rf_names[i].empty())
		{
			/* GR spikes are only spikes not saved on host every time step:
			 * InNet::exportAPGR makes cudaMemcpy call before returning pointer to mem address */
			if (CELL_IDS[i] == "GR")
			{
				cell_spks[i] = simCore->getInputNet()->exportAPGR();
				temp_counter = psth_counter;
			}
			for (uint32_t j = 0; j < rast_cell_nums[i]; j++)
			{
				rasters[i][j][temp_counter] = cell_spks[i][j];
			}
		}
	}

	// Note: backwards compatability, for now. might place in separate function later
	if (gui != NULL)
	{
		const float* vm_pc = simCore->getMZoneList()[0]->exportVmPC();
		for (int i = 0; i < num_pc; i++)
		{
			pc_vm_raster[i][psth_counter] = vm_pc[i];
		}
		const float* vm_io = simCore->getMZoneList()[0]->exportVmIO();
		for (int i = 0; i < num_io; i++)
		{
			io_vm_raster[i][psth_counter] = vm_io[i];
		}
		const float* vm_nc = simCore->getMZoneList()[0]->exportVmNC();
		for (int i = 0; i < num_nc; i++)
		{
			nc_vm_raster[i][psth_counter] = vm_nc[i];
		}
	}
}

void Control::fill_psths(uint32_t psth_counter)
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!pf_names[i].empty())
		{
			for (uint32_t j = 0; j < rast_cell_nums[i]; j++)
			{
				psths[i][j][psth_counter] += cell_spks[i][j];
			}
		}
	}
}

void Control::delete_spike_sums()
{
	FOREACH(spike_sums, ssp)
	{
		delete[] ssp->non_cs_spike_counter;
		delete[] ssp->cs_spike_counter;
	}
}

void Control::delete_rasters()
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!rf_names[i].empty()) delete2DArray<uint8_t>(rasters[i]);
	}
	delete2DArray<float>(pc_vm_raster);
	delete2DArray<float>(nc_vm_raster);
	delete2DArray<float>(io_vm_raster);
}

void Control::delete_psths()
{
	for (uint32_t i = 0; i < NUM_CELL_TYPES; i++)
	{
		if (!pf_names[i].empty()) delete2DArray<uint8_t>(psths[i]);
	}
}

