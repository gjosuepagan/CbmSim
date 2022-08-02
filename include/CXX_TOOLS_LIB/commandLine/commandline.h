#ifndef COMMAND_LINE_H_
#define COMMAND_LINE_H_
#include <string>

#include "fileIO/build_file.h"
#include "fileIO/experiment_file.h"

/* ============================ CONSTANTS =============================== */

const int BUILD_NUM_ARGS = 3;
const int RUN_NUM_ARGS   = 4;

const std::string INPUT_DATA_PATH = "../data/inputs/";
const std::string OUTPUT_DATA_PATH = "../data/outputs/";
const std::string DEFAULT_SIM_OUT_FILE = OUTPUT_DATA_PATH + "default_out_sim_file.sim";

const std::string EXPERIMENT_FILE_FIRST_LINE = "#Begin filetype experiment";
const std::string BUILD_FILE_FIRST_LINE      = "#begin filetype build";


/* =============================== ENUMS ================================ */

enum build_args {BUILD_PROGRAM, BUILD_FILE, BUILD_OUT_SIM_FILE};
enum run_args {RUN_PROGRAM, EXPERIMENT_FILE, IN_SIM_FILE, RUN_OUT_SIM_FILE};

enum vis_mode {GUI, TUI, NO_VIS};
enum run_mode {BUILD, RUN, NO_RUN};


/* ======================= FUNCTION DECLARATIONS ======================= */

/*
 * Description:
 *     Checks whether the c-string arg is the name (without full path info)
 *     of a valid file. Does not check the type nor contents of the file.
 *
 */
bool is_file(const char *arg);

/*
 * Description:
 *     Obtains the run mode {RUN, BUILD} as a run_mode enum, declared above,
 *     from the argument arg. Does not check whether arg is a file or not:
 *     that check is assumed to be done before this.  
 *
 */
enum run_mode get_run_mode(const char *arg);

/*
 * Description:
 *     Obtains the visualization mode {TUI, GUI} as a vis_mode enum, declared above,
 *     from the argument arg. Does not check whether the arg is a file or not:
 *     that check is assumed to be done before this. It is assumed that this fnctn
 *     is used in a branch relating only to run mode, as we assume building involves
 *     only a TUI.
 *
 */
enum vis_mode get_vis_mode(const char *arg);

/*
 * Description:
 *     Main function which checks the number of arguments and validates them according
 *     to the following two-mode scheme:
 *
 *             1) Build Mode
 *
 *                 ./big_sim build_file out_sim_file
 *
 *             2) Run Mode
 *
 *                 ./big_sim experiment_file in_sim_file out_sim_file
 *     
 *     For either mode, none of the arguments is optional. If any argument is missing, is
 *     an invalid file, or has the wrong format (e.g. build_file or experiment_file) an
 *     error message is displayed and the program exits with non-zero status.
 *
 */
void validate_args_and_set_modes(int *argc, char ***argv,
	  enum vis_mode *sim_vis_mode, enum run_mode *sim_run_mode);

/*
 * Description:
 *     Parses the given build file into the parsed_build_file structure,
 *     defined in build_file.h. It is assumed that this function will be called
 *     in some branch relating to build mode. For more information on how a
 *     build file is parsed, please look at build_file.h and build_file.cpp.
 *
 */
void parse_build_args(char ***argv, parsed_build_file &p_file);

/*
 * Description:
 *     Parses the given experiment file into the experiment structure, defined in 
 *     experiment_file.h. It is assumed that this function will be called in some 
 *     branch relating to run mode. For more information on how an experiment file
 *     is parsed, please look at experiment_file.h and experiment_file.cpp.
 *
 */
void parse_experiment_args(char ***argv, experiment &exper);

/*
 * Description:
 *     Assigns the full input simulation file name, which is contained within one of argv,
 *     to the string in_file. Assumes that the input simulation file is given in argv and
 *     in the correct placement relative to the other command line args (see scheme above).
 *     This function should be called in a branch relating to run mode, as build mode does
 *     not require an input simulation file.
 *
 */
void get_in_sim_file(char ***argv, std::string &in_file);

/*
 * Description:
 *     Assigns the full output simulation file name, which is contained within one of argv,
 *     to the string out_file. Assumes that the output simulation file is given in argv and
 *     in the correct placement relative to the other command line args (see scheme above).    
 *
 */
void get_out_sim_file(int arg_index, char ***argv, std::string &out_file);

#endif /* COMMAND_LINE_H_ */

