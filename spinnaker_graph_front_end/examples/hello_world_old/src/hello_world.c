
//! imports
#include "spin1_api.h"
#include "common-typedefs.h"
#include <data_specification.h>
#include <recording.h>
#include <simulation.h>
#include <debug.h>
#include <circular_buffer.h>


// KA : The python can only write in SDRAM, the c code has to read the first time from SDRAM when
// initializes  can read from DTCM, The DTCM is the local memory of every core.

// KA: Multicast routing key to communicate with the next neighbour
// The key is used to identify where it is come from this packet.
uint my_key;
// KA : set parameters that will be used in the receive data method
static circular_buffer input_buffer;
// KA : Transmitted flag
uint32_t flag = 0;

//! control value, which says how many timer ticks to run for before exiting
static uint32_t simulation_ticks = 0;
// Kostas: the Infinite run is always false. For reason there is double definition
static uint32_t infinite_run = 0;
static uint32_t time = 0;

// value for turning on and off interrupts
uint cpsr = 0;

//! The recording flags
static uint32_t recording_flags = 0;

//! human readable definitions of each region in SDRAM
// Kostas : You need also transmission Region to tell the keys
typedef enum regions_e {
    SYSTEM_REGION,
    TRANSMISSIONS,
    RECORDED_DATA
} regions_e;

//! values for the priority for each callback
// Kostas : Here the priorities are different compared to conways_cell
typedef enum callback_priorities{
    MC_PACKET = -1, SDP = 0, USER = 3, TIMER = 2, DMA = 1
} callback_priorities;

typedef enum flag_values{
    FLAG_HELLO = 1, FLAG_BYE = 0
} flag_values;

//! human readable definitions of each element in the transmission region
typedef enum transmission_region_elements {
    HAS_KEY, MY_KEY
} transmission_region_elements;

// Kostas: this function will be used when this core will
// receive a multicast  packet. Here the program will create a line
// of connected nodes so every node has 2 neighbours.
void receive_data(uint key, uint payload) {
    log_info("receive_data\n");
    log_info("the key i've received is %d\n", key);
    log_info("the payload i've received is %d\n", payload);
    if (!circular_buffer_add(input_buffer, payload)) {
        log_info("Could not add state");
    }
}

void iobuf_data(){
    address_t address = data_specification_get_data_address();
    address_t hello_world_address =
        data_specification_get_region(RECORDED_DATA, address);

    log_info("Hello world address is %08x", hello_world_address);

    char* my_string = (char *) &hello_world_address[1];
    log_info("Data read is: %s", my_string);
}

void record_data() {
    log_debug("Recording data\n");

    uint chip = spin1_get_chip_id();

    uint core = spin1_get_core_id();

    log_debug("Issuing 'Hello World' from chip %d, core %d", chip, core);

    bool recorded = recording_record(
        0, "Hello world", 11 * sizeof(char));

    if (recorded) {
        log_debug("Hello World recorded successfully!");
    } else {
        log_error("Hello World was not recorded...");
    }
}


//! \brief Initialises the recording parts of the model
//! \return True if recording initialisation is successful, false otherwise
static bool initialise_recording(){
    log_info("initialise_recording\n");
    address_t address = data_specification_get_data_address();
    address_t recording_region = data_specification_get_region(
        RECORDED_DATA, address);

    bool success = recording_initialize(recording_region, &recording_flags);
    log_info("Recording flags = 0x%08x", recording_flags);
    return success;
}

void resume_callback() {
    time = UINT32_MAX;
}

void send_value_one(){
    log_info("send_value_one\n", my_key);
    // send my new state to the simulation neighbours
    log_debug("sending value 1 via multicast with key %d",
              my_key);
    while (!spin1_send_mc_packet(my_key, 1, WITH_PAYLOAD)) {
        spin1_delay_us(1);
    }

    log_debug("sent value 1 via multicast");
}

void read_input_buffer(){

    cpsr = spin1_int_disable();
    circular_buffer_print_buffer(input_buffer);
    // pull payloads from input_buffer. Filter for alive and dead states

    log_debug("read_input_buffer");
    spin1_mode_restore(cpsr);
}

/****f*
 *
 * SUMMARY
 *
 * SYNOPSIS
 *  void update (uint ticks, uint b)
 *
 * SOURCE
 */
void update(uint ticks, uint b) {
    log_info("update\n");
    use(b);
    use(ticks);

    time++;

    log_info("on tick %d of %d", time, simulation_ticks);

    // check that the run time hasn't already elapsed and thus needs to be
    // killed
    if ((infinite_run != TRUE) && (time >= simulation_ticks)) {
        log_info("Simulation complete.\n");

        // fall into the pause resume mode of operating
        simulation_handle_pause_resume(resume_callback);

        if (recording_flags > 0) {
            log_info("updating recording regions");
            recording_finalise();
        }

        // switch to state where host is ready to read
        simulation_ready_to_read();

        return;

    }
    read_input_buffer();

    if (time == 1) {
        //record_data();
        send_value_one();
    } else if (time ==  100) {
        iobuf_data();
    }

    // trigger buffering_out_mechanism
    log_info("recording flags is %d", recording_flags);
    if (recording_flags > 0) {
        log_info("doing timer tick update\n");
        //! \brief Call once per timestep to ensure buffering is done - should only
        //!be called if recording flags is not 0
        recording_do_timestep_update(time);
        log_info("done timer tick update\n");
    }
}


static bool initialize(uint32_t *timer_period) {
    log_info("Initialise: started\n");

    // return the SDRAM start address for this core.
    address_t address = data_specification_get_data_address();
    log_info("SDRAM start address is %u\n", address);

    //! \return boolean where True is when the header is correct and False if there
    //!         is a conflict with the DSE magic number
    if (!data_specification_read_header(address)) {
        log_error("failed to read the data spec header");
        return false;
    }

    // Get the timing details and set up the simulation interface
    if (!simulation_initialise(
            data_specification_get_region(SYSTEM_REGION, address),
            APPLICATION_NAME_HASH, timer_period, &simulation_ticks,
            &infinite_run, SDP, DMA)) {
            log_info("Get timing details\n");
        return false;
    }

    //    #####  initialise transmission keys   #####
    //! \brief Returns the absolute SDRAM memory address for a given region value.
    address_t transmission_region_address = data_specification_get_region(
            TRANSMISSIONS, address);
    log_info("transmission_region_address  is %u\n", transmission_region_address);
    if (transmission_region_address[HAS_KEY] == 1) { // a pointer to uint32 and if the first element of this array exists so has key do the code bellow
        my_key = transmission_region_address[MY_KEY];
        log_info("my key is %d\n", my_key);
    } else {
        log_error(
            "cannot find the keys in the regions\n");
        return false;
    }

    // initialise my input_buffer for receiving packets
    input_buffer = circular_buffer_initialize(256);
    if (input_buffer == 0){
        return false;
    }
    log_info("input_buffer initialised");

    return true;
}

/****f*
 *
 * SUMMARY
 *  This function is called at application start-up.
 *  It is used to register event callbacks and begin the simulation.
 *
 * SYNOPSIS
 *  int c_main()
 *
 * SOURCE
 */
void c_main() {
    log_info("starting HelloWorld_demo\n");

    // Load DTCM data
    uint32_t timer_period;

    // initialise the model
    if (!initialize(&timer_period)) {
        rt_error(RTE_SWERR);
    }

    // initialise the recording section
    // set up recording data structures
    if(!initialise_recording()){
         rt_error(RTE_SWERR);
    }

    // set timer tick value to configured value
    log_info("setting timer to execute every %d microseconds", timer_period);
    // Kostas: here sets spin1_set_timer_tick the timer tick value.
    // Every one millisecond the timer ticks and the callback function
    // is being invoked.
    spin1_set_timer_tick(timer_period);


    // register callbacks
    // Kostas: This function will never be used
    // since the helloworld does not have communication between the vertices, it could be used if the cores
    // received multicast packets and subsequently call receive_data function.
    spin1_callback_on(MCPL_PACKET_RECEIVED, receive_data, MC_PACKET);
    // Kostas: This callback function is important, it captures
    // every tick of the timer and calls each time the update function. It works like a for loop
    // so if we set the program to run(10) , it will run 10 milliseconds and the timer will tick every
    // millisecond.
    spin1_callback_on(TIMER_TICK, update, TIMER);

    // start execution
    log_info("Starting\n");

    // Start the time at "-1" so that the first tick will be 0
    time = UINT32_MAX;

    simulation_run();
}


// Overall Here the c code will setup some packet interrupts and the timer, and the period of ticks
// when t=1 will record the text helloworld, and will not run io_bufrecord.
// so overall all this c program records "Hello world" text.