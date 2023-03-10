#ifndef _CONTROL_H
#define _CONTROL_H

#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <iterator>
#include <ctime>

#include "commandline.h"
#include <cstdint>
#include "connectivityparams.h"
#include "activityparams.h"
#include "cbmstate.h"
#include "innetconnectivitystate.h"
#include "innetactivitystate.h"
#include "cbmsimcore.h"
#include "ecmfpopulation.h"
#include "poissonregencells.h"
#include "bits.h"

// TODO: place in a common place, as gui uses a constant like this too
#define NUM_CELL_TYPES 8

// convenience enum for indexing cell arrays
enum cell_id {MF, GR, GO, BC, SC, PC, IO, NC};

struct cell_spike_sums
{
	uint32_t num_cells;
	uint32_t non_cs_spike_sum;
	uint32_t cs_spike_sum;
	uint32_t *non_cs_spike_counter;
	uint32_t *cs_spike_counter;
};

struct cell_firing_rates
{
	float non_cs_mean_fr;
	float non_cs_median_fr;
	float cs_mean_fr;
	float cs_median_fr;
};

enum sim_run_state {NOT_IN_RUN, IN_RUN_NO_PAUSE, IN_RUN_PAUSE};

class Control 
{
	public:
		Control(parsed_commandline &p_cl);
		~Control();

		// Objects
		trials_data td;
		CBMState *simState     = NULL;
		CBMSimCore *simCore    = NULL;
		ECMFPopulation *mfFreq = NULL;
		PoissonRegenCells *mfs = NULL;

		/* temporary state check vars, going to refactor soon */
		bool trials_data_initialized = false;
		bool sim_initialized         = false;

		bool raster_arrays_initialized = false;
		bool psth_arrays_initialized   = false;
		bool spike_sums_initialized    = false;
		enum sim_run_state run_state   = NOT_IN_RUN; 

		std::string visual_mode          = "";
		std::string run_mode             = "";
		std::string curr_build_file_name = "";
		std::string curr_sess_file_name  = "";
		std::string curr_sim_file_name   = "";
		std::string out_sim_file_name    = "";

		// params that I do not know how to categorize
		float goMin = 0.26; 
		float spillFrac = 0.15; // go->gr synapse, part of build
		float inputStrength = 0.0;

		// sim params -> TODO: place in simcore
		int gpuIndex = 0;
		int gpuP2    = 2;

		int trial;
		uint32_t raster_counter;

		int csPhasicSize = 50;

		// mzone stuff -> TODO: place in build file down the road
		int numMZones = 1; 

		// MFFreq params (formally in Simulation::getMFs, Simulation::getMFFreq)
		int mfRandSeed = 3;
		float threshDecayTau = 4.0;

		float nucCollFrac = 0.02;
		//float csMinRate = 100.0; // uh look at tonic lol
		//float csMaxRate = 110.0;

		float CSTonicMFFrac = 0.05;
		float tonicFreqMin  = 100.0;
		float tonicFreqMax  = 110.0;

		float CSPhasicMFFrac = 0.0;
		float phasicFreqMin  = 200.0;
		float phasicFreqMax  = 250.0;
	
		// separate set of contextual MF due to position of rabbit
		float contextMFFrac  = 0.0;
		float contextFreqMin = 20.0; 
		float contextFreqMax = 50.0;

		float bgFreqMin   = 10.0;
		float csbgFreqMin = 10.0;
		float bgFreqMax   = 30.0;
		float csbgFreqMax = 30.0;

		bool collaterals_off = false;
		bool secondCS        = true;

		float fracImport  = 0.0;
		float fracOverlap = 0.2;

		int trialTime = 0; 
		const uint32_t pre_collection_ts = 2000;
		// raster measurement params. 
		int msPreCS = 0;
		int msPostCS = 0;
		int PSTHColSize = 0; // derived param, from msPreCS, msPostCS and csLength 

		enum plasticity pf_pc_plast;
		enum plasticity mf_nc_plast;

		std::string rf_names[NUM_CELL_TYPES];
		std::string pf_names[NUM_CELL_TYPES]; 

		std::string pf_pc_weights_file = "";
		std::string mf_nc_weights_file = "";

		struct cell_spike_sums spike_sums[NUM_CELL_TYPES];
		struct cell_firing_rates firing_rates[NUM_CELL_TYPES];
		const uint8_t *cell_spikes[NUM_CELL_TYPES];

		const float *grgoG, *mfgoG, *gogrG, *mfgrG;
		float *sample_pfpc_syn_weights; //TODO: remove, write function to save at end of session
		const uint8_t *mfAP, *goSpks;
		
		const uint8_t *cell_spks[NUM_CELL_TYPES];
		int rast_cell_nums[NUM_CELL_TYPES];
		uint8_t **rasters[NUM_CELL_TYPES];
		uint8_t **psths[NUM_CELL_TYPES];

		uint32_t rast_sizes[NUM_CELL_TYPES]; 

		float **pc_vm_raster;
		float **nc_vm_raster;
		float **io_vm_raster;

		void build_sim();

		void set_plasticity_modes(parsed_commandline &p_cl);
		void init_sim(parsed_sess_file &s_file, std::string in_sim_filename);
		void reset_sim(std::string in_sim_filename);

		void save_sim_to_file(std::string outSimFile);
		void save_pfpc_weights_to_file(std::string out_pfpc_file);
		void load_pfpc_weights_from_file(std::string in_pfpc_file);
		void save_mfdcn_weights_to_file(std::string out_mfdcn_file);
		void load_mfdcn_weights_from_file(std::string in_mfdcn_file);

		void get_raster_filenames(std::map<std::string, std::string> &raster_files);
		void get_psth_filenames(std::map<std::string, std::string> &psth_files);
		void get_weights_filenames(std::map<std::string, std::string> &weights_files);
		void initialize_rast_cell_nums();
		void initialize_cell_spikes();
		void initialize_spike_sums();
		void initialize_rasters();
		void initialize_psths();

		void runSession(struct gui *gui);

		void reset_spike_sums();
		void reset_rasters(); // TODO: seems like should be deprecated
		void reset_psths(); 

		void countGOSpikes(int *goSpkCounter, float &medTrials);
		void update_spike_sums(int tts, float onset_cs, float offset_cs);
		void calculate_firing_rates(float onset_cs, float offset_cs);
		void fill_rasters(uint32_t raster_counter, uint32_t psth_counter, struct gui *gui);
		void fill_psths(uint32_t psth_counter);
		void save_weights();
		void save_gr_raster();
		void save_rasters();
		void save_psths();

		void delete_rasters();
		void delete_psths(); 
		void delete_spike_sums();
};

#endif /*_CONTROL_H*/

