/****a* heat_demo.c/heat_demo_summary
*
* COPYRIGHT
*  Copyright (c) The University of Manchester, 2011. All rights reserved.
*  SpiNNaker Project
*  Advanced Processor Technologies Group
*  School of Computer Science
*
*******/

// SpiNNaker API
#include "spin1_api.h"
#include "common-typedefs.h"
#include <data_specification.h>
#include <simulation.h>
#include <debug.h>


#define DEBUG_KEYS         500

// the visualiser has a bug with negative temperatures!
#define POSITIVE_TEMP      TRUE

// constants to use in heat difusion calculation
#define PARAM_CX           0.03125
#define PARAM_CY           0.03125

/* multicast routing keys to communicate with neighbours */
uint my_key;
uint north_key;
uint south_key;
uint east_key;
uint west_key;
/* multicast routing keys recivable for injecting temps*/
uint fake_temp_north_key;
uint fake_temp_south_key;
uint fake_temp_east_key;
uint fake_temp_west_key;
/* multicast routing keys recivable for commands*/
uint command_pause_key;
uint comamnd_stop_key;
uint command_resume_key;
/* multicast routing keys for sneding temp back to host for live stream*/
uint host_data_key;

/* temperature values */
int my_temp;  // any initial value will do!
int old_temp = 0;  // any initial value will do!

// get temperatures from 4 neighbours
// make sure to have room for two values from each neighbour
// given that the communication is asynchronous
int neighbours_temp[2][4];

/* coeficients to compute new temperature value */
/* adjust for 16.16 fixed-point representation  */
int cx_adj = (int) (PARAM_CX * (1 << 16));
int cy_adj = (int) (PARAM_CY * (1 << 16));

/* keep track of which neighbours have sent data */
/* cores in the boder need special values! */
uint arrived[2];
uint init_arrived;
uint now  = 0;
uint next = 1;

bool updating = true;
uint simulation_ticks;

//! human readable definitions of each region in SDRAM
typedef enum regions_e {
    SYSTEM_REGION, TRANSMISSIONS, NEIGBOUR_KEYS, COMMAND_KEYS, OUTPUT_KEYS,
    TEMP_VALUE,
} regions_e;

//! human readable definitions of each element in the transmission region
typedef enum transmission_region_elements {
    HAS_KEY, MY_KEY
}transmission_region_elements;

//! human readable definitions of each element in the neigbour region
typedef enum neigbour_region_elements {
    EAST_KEY, NORTH_KEY, WEST_KEY, SOUTH_KEY, EAST_FAKE_KEY, NORTH_FAKE_KEY,
    WEST_FAKE_KEY, SOUTH_FAKE_KEY
}neigbour_region_elements;

//! human readable definitions of each element in the command keys region
typedef enum command_region_elements {
    STOP_COMMAND_KEY, PAUSE_COMMAND_KEY, RESUME_COMMAND_KEY
}command_region_elements;

//! human readable definitions of each element in the host output region
typedef enum host_output_region_elements {
    HOST_TRANSMISSION_KEY
}host_output_region_elements;

//! human readable definitions of each element in the initial tempature region
typedef enum initial_temperature_region_elements {
    INITIAL_TEMPERATURE
}initial_temperature_region_elements;

#ifdef DEBUG
    uint   dbg_packs_recv = 0;
    uint * dbg_keys_recv;
    uint   dbg_timeouts = 0;
    uint * dbg_stime;
#endif

/****f* heat_demo.c/send_temps_to_host
*
* SUMMARY
*  This function is called at application exit.
*  It's used to report the final temperatures to the host
*
* SYNOPSIS
*  void send_temps_to_host ()
*
* SOURCE
*/
void send_temps_to_host ()
{
    /* send mc packet which should be ecieved by host via gatherers */
    spin1_send_mc_packet(host_data_key, my_temp, WITH_PAYLOAD);
}


/****f* heat_demo.c/report_temp
*
* SUMMARY
*  This function is used to report current temp
*
* SYNOPSIS
*  void report_temp (uint ticks)
*
* SOURCE
*/
void report_temp (uint ticks)
{
  //error
}

/****f* heat_demo.c/receive_data
*
* SUMMARY
*  This function is used as a callback for packet received events.
* receives data from 4 (NSEW) neighbours and updates the checklist
*
* SYNOPSIS
*  void receive_data (uint key, uint payload)
*
* INPUTS
*   uint key: packet routing key - provided by the RTS
*   uint payload: packet payload - provided by the RTS
*
* SOURCE
*/
void receive_data (uint key, uint payload)
{
    sark.vcpu->user1++;

    #ifdef DEBUG
        dbg_keys_recv[dbg_packs_recv++] = key;
        if (dbg_packs_recv == DEBUG_KEYS)
        {
             dbg_packs_recv = 0;
        }
    #endif

    if (key == north_key)
    {
        if (arrived[now] & NORTH_ARRIVED)
        {
            neighbours_temp[next][NORTH] = payload;
            arrived[next] |= NORTH_ARRIVED;
        }
        else
        {
            neighbours_temp[now][NORTH] = payload;
            arrived[now] |= NORTH_ARRIVED;
        }
      }
    else if (key == south_key)
    {
        if (arrived[now] & SOUTH_ARRIVED)
        {
            neighbours_temp[next][SOUTH] = payload;
            arrived[next] |= SOUTH_ARRIVED;
        }
        else
        {
            neighbours_temp[now][SOUTH] = payload;
            arrived[now] |= SOUTH_ARRIVED;
        }
    }
    else if (key == east_key)
    {
        if (arrived[now] & EAST_ARRIVED)
        {
            neighbours_temp[next][EAST] = payload;
            arrived[next] |= EAST_ARRIVED;
        }
        else
        {
            neighbours_temp[now][EAST] = payload;
            arrived[now] |= EAST_ARRIVED;
        }
    }
    else if (key == west_key)
    {
        if (arrived[now] & WEST_ARRIVED)
        {
            neighbours_temp[next][WEST] = payload;
            arrived[next] |= WEST_ARRIVED;
        }
        else
        {
            neighbours_temp[now][WEST] = payload;
            arrived[now] |= WEST_ARRIVED;
        }
    }
    else if (key == fake_temp_north_key)
    {
        neighbours_temp[now][NORTH]  = payload;
        neighbours_temp[next][NORTH] = payload;
    }
    else if (key == fake_temp_east_key)
    {
        neighbours_temp[now][EAST]  = payload;
        neighbours_temp[next][EAST] = payload;
    }
    else if (key == fake_temp_south_key)
    {
        neighbours_temp[now][SOUTH]  = payload;
        neighbours_temp[next][SOUTH] = payload;
    }
    else if (key == fake_temp_west_key)
    {
        neighbours_temp[now][WEST]  = payload;
        neighbours_temp[next][WEST] = payload;
    }
    else if (key == comamnd_stop_key)
    {
        spin1_exit (0);
    }
    else if (key == command_pause_key)
    {
        updating = false;
    }
    else if (key == command_resume_key)
    {
        updating = true;
    }
    else
    {
        // unexpected packet!
        #ifdef DEBUG
            io_printf (IO_STD, "!\n");
        #endif
    }
}


/****f* heat_demo.c/send_first_value
*
* SUMMARY
*
* SYNOPSIS
*  void send_first_value (uint a, uint b)
*
* SOURCE
*/
void send_first_value (uint a, uint b)
{
    /* send data to neighbours */
    spin1_send_mc_packet(my_key, my_temp, WITH_PAYLOAD);
}


/****f* heat_demo.c/update
*
* SUMMARY
*
* SYNOPSIS
*  void update (uint ticks, uint b)
*
* SOURCE
*/
void update (uint ticks, uint b)
{
    sark.vcpu->user0++;

    if (updating)
    {
        /* report if not all neighbours' data arrived */
        #ifdef DEBUG
            if (arrived[now] != ALL_ARRIVED)
            {
                io_printf (IO_STD, "@\n");
                dbg_timeouts++;
            }
        #endif

        // if a core does not receive temperature from a neighbour
        // it uses it's own as an estimate for the nieghbour's.
        if (arrived[now] != ALL_ARRIVED)
        {
            if (!(arrived[now] & NORTH_ARRIVED))
            {
                neighbours_temp[now][NORTH] = my_temp;
            }

            if (!(arrived[now] & SOUTH_ARRIVED))
            {
                neighbours_temp[now][SOUTH] = my_temp;
            }

            if (!(arrived[now] & EAST_ARRIVED))
            {
                neighbours_temp[now][EAST] = my_temp;
            }

            if (!(arrived[now] & WEST_ARRIVED))
            {
                neighbours_temp[now][WEST] = my_temp;
            }
        }

        /* compute new temperature */
        /* adjust for 16.16 fixed-point representation  */
        int tmp1 = neighbours_temp[now][EAST] + neighbours_temp[now][WEST]
                    - 2 * my_temp;
        int tmp2 = neighbours_temp[now][NORTH] + neighbours_temp[now][SOUTH]
                   - 2 * my_temp;
        /* adjust for 16.16 fixed-point representation  */
        int tmp3 = (int) (((long long) cx_adj * (long long) tmp1) >> 16);
        int tmp4 = (int) (((long long) cy_adj * (long long) tmp2) >> 16);
        my_temp = my_temp + tmp3 + tmp4;

        #ifdef POSITIVE_TEMP
                // avoids a problem with negative temps in the visualiser!
                my_temp = (my_temp > 0) ? my_temp : 0;
        #endif

        /* send new data to neighbours */
        spin1_send_mc_packet(my_key, my_temp, WITH_PAYLOAD);

        /* prepare for next iteration */
        arrived[now] = init_arrived;
        now  = 1 - now;
        next = 1 - next;

        /* report current temp */
        report_temp(ticks);
    }
}


static bool initialize(uint32_t *timer_period) {
    log_info("Initialise: started");

    // Get the address this core's DTCM data starts at from SRAM
    address_t address = data_specification_get_data_address();

    // Read the header
    if (!data_specification_read_header(address)) {
        return false;
    }

    // Get the timing details
    address_t system_region = data_specification_get_region(
        SYSTEM_REGION, address);
    if (!simulation_read_timing_details(
            system_region, APPLICATION_MAGIC_NUMBER, timer_period,
            &simulation_ticks)) {
        return false;
    }

    // initlise transmission keys
    address_t transmission_region_address =  data_specification_get_region(
        TRANSMISSIONS, address);
    if (transmission_region_address[HAS_KEY] == 1){
        my_key = transmission_region_address[MY_KEY];
    }
    else {
        log_error("this heat element cant effect anything, deduced as an error,"
                  "please fix the application fabric and try again");
        return false;
    }

    // initlise neighbour keys
    address_t neigbour_region_address =  data_specification_get_region(
       NEIGBOUR_KEYS, address);
    east_key = neigbour_region_address[EAST_KEY];
    north_key = neigbour_region_address[NORTH_KEY];
    west_key = neigbour_region_address[WEST_KEY];
    south_key = neigbour_region_address[SOUTH_KEY];
    fake_temp_east_key = neigbour_region_address[EAST_FAKE_KEY];
    fake_temp_north_key = neigbour_region_address[NORTH_FAKE_KEY];
    fake_temp_west_key = neigbour_region_address[WEST_FAKE_KEY];
    fake_temp_south_key = neigbour_region_address[SOUTH_FAKE_KEY];
    
    // initlise command keys
    address_t command_region_address =  data_specification_get_region(
       COMMAND_KEYS, address);
    comamnd_stop_key = command_region_address[STOP_COMMAND_KEY];
    command_pause_key = command_region_address[PAUSE_COMMAND_KEY];
    command_resume_key = command_region_address[RESUME_COMMAND_KEY];

    // initlise output key
    address_t host_output_region_address =  data_specification_get_region(
       OUTPUT_KEYS, address);
    host_data_key = host_output_region_address[HOST_TRANSMISSION_KEY];

    // read my temperture
    address_t my_temp_region_address =  data_specification_get_region(
       TEMP_VALUE, address);
    my_temp = my_temp_region_address[INITIAL_TEMPERATURE];
    return true;
}


/****f* heat_demo.c/c_main
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
void c_main()
{
    log_info("starting heat_demo\n");
    // Load DTCM data
    uint32_t timer_period;

    // initialise the model
    if (!initialize(&timer_period)){
    	rt_error(RTE_API);
    }

    // Start the time at "-1" so that the first tick will be 0
    time = UINT32_MAX;

    // set timer tick value to 1ms (in microseconds)
    // slow down simulation to alow users to appreciate changes
    spin1_set_timer_tick (timer_period);

    // register callbacks
    spin1_callback_on (MCPL_PACKET_RECEIVED, receive_data, 0);
    spin1_callback_on (TIMER_TICK, update, 0);

    #ifdef DEBUG
        // initialise variables
        dbg_keys_recv = spin1_malloc(DEBUG_KEYS * 4 * sizeof(uint));
        // record start time somewhere in SDRAM
        dbg_stime = (uint *) (SPINN_SDRAM_BASE + 4 * coreID);
        *dbg_stime = sv->clock_ms;
    #endif

    // kick-start the update process
    spin1_schedule_callback(send_first_value, 0, 0, 3);

    // start execution
    log_info("Starting");
    simulation.run()
    log_info("stopping heat_demo\n");
}