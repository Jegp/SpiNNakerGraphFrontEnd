//! imports
#include "spin1_api.h"
#include "common-typedefs.h"
#include <data_specification.h>
#include <simulation.h>
#include <debug.h>

//!###################### magic numbers
//! the number of DMA buffers to build
#define N_DMA_BUFFERS 2

//! flag size for saying ended
#define END_FLAG_SIZE 4

//! flag for saying stuff has ended
#define END_FLAG 0xFFFFFFFF

//! SDP port for packets.
#define SDP_PORT_FOR_SDP_PACKETS 2

//! items per SDP packet for sending
#define ITEMS_PER_DATA_PACKET 68

//! convert between words to bytes
#define WORD_TO_BYTE_MULTIPLIER 4

#define SEQUENCE_NUMBER_SIZE 1

//!################## SDP flags
//! send data command ID in SDP
#define SDP_COMMAND_FOR_SENDING_DATA 100

//! start missing SDP seq nums in SDP (this includes n SDP packets expected)
#define SDP_COMMAND_FOR_START_OF_MISSING_SDP_PACKETS 1000

//! other missing SDP seq nums in SDP
#define SDP_COMMAND_FOR_MORE_MISSING_SDP_PACKETS 1001

//! timeout for trying to end SDP packet
#define SDP_TIMEOUT 1000

//! extra length adjustment for the SDP header
#define LENGTH_OF_SDP_HEADER 8

//################### DMA tags

//! DMA complete tag for writing the missing SEQ nums to SDRAM
#define DMA_TAG_FOR_WRITING_MISSING_SEQ_NUMS 3

//! DMA complete tag for the reading from SDRAM of data to be retransmitted
#define DMA_TAG_RETRANSMISSION_READING 2

//! DMA complete tag for retransmission of data seq nums
#define DMA_TAG_READ_FOR_RETRANSMISSION 1

//! DMA complete tag for original transmission, this isn't used yet, but needed
//! for full protocol
#define DMA_TAG_READ_FOR_TRANSMISSION 0

//! throttle power on the MC transmissions if needed (assume not needed)
#define TDMA_WAIT_PERIOD 0

//! struct for a SDP message with pure data, no SCP header
typedef struct sdp_msg_pure_data {	// SDP message (=292 bytes)
    struct sdp_msg *next;		// Next in free list
    uint16_t length;		// length
    uint16_t checksum;		// checksum (if used)

    // sdp_hdr_t
    uint8_t flags;	    	// SDP flag byte
    uint8_t tag;		      	// SDP IPtag
    uint8_t dest_port;		// SDP destination port/CPU
    uint8_t srce_port;		// SDP source port/CPU
    uint16_t dest_addr;		// SDP destination address
    uint16_t srce_addr;		// SDP source address

    // User data (272 bytes when no SCP header)
    uint32_t data[ITEMS_PER_DATA_PACKET];

    uint32_t _PAD;		// Private padding
} sdp_msg_pure_data;

//! control value, which says how many timer ticks to run for before exiting
static uint32_t simulation_ticks = 0;
static uint32_t infinite_run = 0;
static uint32_t time = 0;

//! int as a bool to represent if this simulation should run forever
static uint32_t infinite_run;

//! transmission stuff
static uint32_t *data_to_transmit[N_DMA_BUFFERS];
static uint32_t transmit_dma_pointer = 0;
static uint32_t position_in_store = 0;
static bool first_transmission = true;

//! retransmission stuff
static uint32_t missing_sdp_packets = 0;
static uint32_t data_written = 0;
address_t missing_sdp_seq_num_sdram_address = NULL;

//! retransmission DMA stuff
static uint32_t *retransmit_seq_nums[N_DMA_BUFFERS];
static uint32_t current_dma_pointer = 0;
static uint32_t position_for_retransmission = 0;
static uint32_t missing_seq_num_being_processed = 0;
static uint32_t position_in_read_data = 0;

//! SDP message holder for transmissions
sdp_msg_pure_data my_msg;

//! state for how many bytes it needs to send, gives approx bandwidth if 
//! round number. 
static uint32_t bytes_to_write;
static address_t *store_address = NULL;
address_t dsg_main_address;
static uint32_t key;

//! human readable definitions of each region in SDRAM
typedef enum regions_e {
    SYSTEM_REGION, CONFIG
} regions_e;

//! values for the priority for each callback
typedef enum callback_priorities{
    MC_PACKET = -1, SDP = 0, DMA = 0
} callback_priorities;

//! human readable definitions of each element in the transmission region
typedef enum config_region_elements {
    MY_KEY, MB
} config_region_elements;


void send_data_block(
        uint32_t current_dma_pointer, uint32_t number_of_elements_to_send,
        uint32_t first_packet_key){

   //log_info("first data is %d", data_to_transmit[current_dma_pointer][0]);

   // send data
   for (uint data_position = 0; data_position < number_of_elements_to_send;
        data_position++)
   {
        uint32_t current_data =
            data_to_transmit[current_dma_pointer][data_position];
        //log_info("transmit key %d and payload %d", first_packet_key, current_data);
        while(!spin1_send_mc_packet(first_packet_key, current_data,
                                    WITH_PAYLOAD)){
        }
        first_packet_key = key;
   }
   //log_info("last data is %d", data_to_transmit[current_dma_pointer][number_of_elements_to_send - 1]);
}

//! \brief sets off a DMA reading a block of SDRAM for dara
//! \param[in] position_in_sdram where in SDRAM to read from
//! \param[in] dma_tag the DMA tag associated with this read.
//!            transmission or retransmission
//! \param[in] offset where in the data array to start writing to
void read(uint32_t dma_tag, uint32_t offset, uint32_t size_in_bytes_to_read){
    // set off DMA
    transmit_dma_pointer = (transmit_dma_pointer + 1) % N_DMA_BUFFERS;

    address_t data_sdram_position =
        (address_t)&store_address[position_in_store];

    // update position as needed
    position_in_store += (size_in_bytes_to_read / WORD_TO_BYTE_MULTIPLIER);

    //log_info("reading %d bytes", size_in_bytes_to_read);
    while (!spin1_dma_transfer(
        dma_tag, data_sdram_position,
        &(data_to_transmit[transmit_dma_pointer][offset]),
        DMA_READ, size_in_bytes_to_read)){
    }
}

//! \brief DMA complete callback for reading for original transmission
void dma_complete_reading_for_original_transmission(uint unused, uint unused2){
    use(unused);
    use(unused2);

    // do DMA
    uint32_t current_dma_pointer = transmit_dma_pointer;
    uint32_t key_to_transmit = key;

    // put size in bytes if first send
    //log_info("in original read complete callback");
    if(first_transmission){
        //log_info("in first");
        data_to_transmit[current_dma_pointer][0] = bytes_to_write;
        key_to_transmit = key + 2;
    }

    // stopping procedure
    // if a full packet, read another and try again
    //log_info("position_in_store = %d, to get to %d", position_in_store, (uint)bytes_to_write / WORD_TO_BYTE_MULTIPLIER);
    if (position_in_store < (uint)bytes_to_write / WORD_TO_BYTE_MULTIPLIER){
        //log_info("setting off another DMA");
        uint32_t num_items_to_transmit =
            ITEMS_PER_DATA_PACKET - SEQUENCE_NUMBER_SIZE;

        //difference in items to send for first time vs rest
        if (first_transmission){
            first_transmission = false;
            num_items_to_transmit = ITEMS_PER_DATA_PACKET;
        }

        // reread and transmit
        read(DMA_TAG_READ_FOR_TRANSMISSION, 0,
             (ITEMS_PER_DATA_PACKET - SEQUENCE_NUMBER_SIZE) *
              WORD_TO_BYTE_MULTIPLIER);

        //log_info("sending data");
        send_data_block(
            current_dma_pointer, num_items_to_transmit, key_to_transmit);
        //log_info("finished sending data");
    }
    else{
        //log_info("sending data");
        uint32_t n_elements_to_trasnmit = (
            (uint)(bytes_to_write / WORD_TO_BYTE_MULTIPLIER)) -
            (position_in_store - (ITEMS_PER_DATA_PACKET - SEQUENCE_NUMBER_SIZE));
        //log_info("trasnmitting %d elements", n_elements_to_trasnmit);
        send_data_block(
            current_dma_pointer, n_elements_to_trasnmit, key_to_transmit);
        //log_info("finished sending data");

        while(!spin1_send_mc_packet(key, END_FLAG, WITH_PAYLOAD)){
        }
        //log_info("finished sending original data with end flag");
    }

    if (TDMA_WAIT_PERIOD != 0){
        sark_delay_us(TDMA_WAIT_PERIOD);
    }
}

//! write SDP seq nums to SDRAM that need retransmitting
void write_missing_sdp_seq_nums_into_sdram(
        uint32_t data[], ushort length, uint32_t start_offset){

    for(ushort offset=start_offset; offset < length; offset ++){
        missing_sdp_seq_num_sdram_address[
            data_written + (offset - start_offset)] = data[offset];
        //log_info("data writing into SDRAM is %d", data[offset]);
    }
    data_written += (length - start_offset);
}

//! entrance method for storing SDP seq nums into SDRAM
void store_missing_seq_nums(uint32_t data[], ushort length, bool first){
    uint32_t start_reading_offset = 1;
    if (first){
        missing_sdp_packets = data[1];
        uint32_t total_missing_seq_nums = (
            (ITEMS_PER_DATA_PACKET - 2) +
            ((missing_sdp_packets  - 1) * (ITEMS_PER_DATA_PACKET - 1)));
        //log_info("final seq num count is %d", total_missing_seq_nums);

        uint32_t size_of_data =
            ((missing_sdp_packets * ITEMS_PER_DATA_PACKET) *
            WORD_TO_BYTE_MULTIPLIER) + END_FLAG_SIZE;

        //log_info("doing first with xalloc of %d bytes", size_of_data);
        if(missing_sdp_seq_num_sdram_address != NULL){
            sark_xfree(sv->sdram_heap, missing_sdp_seq_num_sdram_address,
                       ALLOC_LOCK + ALLOC_ID + (sark_vec->app_id << 8));
            missing_sdp_seq_num_sdram_address = NULL;
        }
        missing_sdp_seq_num_sdram_address = sark_xalloc(
            sv->sdram_heap, size_of_data, 0,
            ALLOC_LOCK + ALLOC_ID + (sark_vec->app_id << 8));
        start_reading_offset = 2;
        //log_info("address to write to is %d",
        //         missing_sdp_seq_num_sdram_address);
    }
    
    // write data to SDRAM and update packet counter
    write_missing_sdp_seq_nums_into_sdram(data, length, start_reading_offset);
    missing_sdp_packets -= 1;
}

//! sets off a DMA for retransmission stuff
void retransmission_dma_read(){
    // update DMA pointer for oscillation
    current_dma_pointer = (current_dma_pointer + 1) % N_DMA_BUFFERS;

    // locate where we are in SDRAM
    address_t data_sdram_position =
        &missing_sdp_seq_num_sdram_address[position_for_retransmission];
    //log_info(" address to DMA from is %d", data_sdram_position);
    //log_info(" DMA pointer = %d", dma_pointer);
    //log_info("size to read is %d",
    //         ITEMS_PER_DATA_PACKET * WORD_TO_BYTE_MULTIPLIER);

    // set off DMA
    //log_info("setting off DMA");
    while (!spin1_dma_transfer(
            DMA_TAG_READ_FOR_RETRANSMISSION, data_sdram_position,
            (void *)retransmit_seq_nums[current_dma_pointer], DMA_READ,
            ITEMS_PER_DATA_PACKET * WORD_TO_BYTE_MULTIPLIER)){
        // do nothing when failing, just keep retrying. it'll work at
        // some point
        //log_info("failing to set off DMA transfer!");
    }
}

void the_dma_complete_read_missing_seqeuence_nums(uint unused, uint unused2){
    use(unused);
    use(unused2);

    //! check if at end of read missing seq nums
    if (position_in_read_data > ITEMS_PER_DATA_PACKET){
        position_for_retransmission += ITEMS_PER_DATA_PACKET;
        if (data_written > position_for_retransmission){
            position_in_read_data = 0;
            retransmission_dma_read();
        }
    }
    else{

        // get next seq num to regenerate
        missing_seq_num_being_processed = (uint32_t)
            retransmit_seq_nums[current_dma_pointer][position_in_read_data];
        if(missing_seq_num_being_processed != END_FLAG){
            // regenerate data
            position_in_store =
               missing_seq_num_being_processed * (
                   ITEMS_PER_DATA_PACKET - SEQUENCE_NUMBER_SIZE);
            read(DMA_TAG_RETRANSMISSION_READING, 1,
                 (ITEMS_PER_DATA_PACKET - SEQUENCE_NUMBER_SIZE - 1) *
                  WORD_TO_BYTE_MULTIPLIER);
        }
        else{ // finished data send, tell host its done
           while(!spin1_send_mc_packet(key, END_FLAG, WITH_PAYLOAD)){
           }
        }
    }
}

//! \brief DMA complete callback for have read missing seq num data
void dma_complete_writing_missing_seq_to_sdram(uint unused, uint unused2){
    use(unused);
    use(unused2);
    //log_info("Need to figure what to do here");
}


//! \brief DMA complete callback for have read missing seq num data
void dma_complete_reading_retransmission_data(uint unused, uint unused2){
    use(unused);
    use(unused2);

    //log_info("just read data for a given missing sequence number");

    // set seq number as first element
    data_to_transmit[transmit_dma_pointer][0] =
        missing_seq_num_being_processed;

    // send new data back to host
    send_data_block(transmit_dma_pointer,
                    ITEMS_PER_DATA_PACKET * WORD_TO_BYTE_MULTIPLIER,
                    key + 1);

    position_in_read_data += 1;
    the_dma_complete_read_missing_seqeuence_nums(0, 0);
}



//! SDP reception
void sdp_reception(uint mailbox, uint port){
    use(port);
    //log_info("packet received");
    sdp_msg_pure_data *msg = (sdp_msg_pure_data *) mailbox;

    //log_info("received packet with code %d", msg->data[0]);
    //log_info("bytes to read = %d", bytes_to_write);

    // start the process of sending data
    if(msg->data[0] == SDP_COMMAND_FOR_SENDING_DATA){
        log_info("starting the send of orginial data");
        spin1_msg_free((sdp_msg_t *) msg);

        // reset states
        first_transmission = true;
        transmit_dma_pointer = 0;
        position_in_store = 0;
        read(DMA_TAG_READ_FOR_TRANSMISSION, 1,
             (ITEMS_PER_DATA_PACKET - SEQUENCE_NUMBER_SIZE) *
             WORD_TO_BYTE_MULTIPLIER);
    }

    // start or continue to gather missing packet list
    else if(msg->data[0] == SDP_COMMAND_FOR_START_OF_MISSING_SDP_PACKETS ||
            msg->data[0] == SDP_COMMAND_FOR_MORE_MISSING_SDP_PACKETS){
        //log_info("starting resend mode");

        // reset state, as could be here from multiple attempts
        if(msg->data[0] == SDP_COMMAND_FOR_START_OF_MISSING_SDP_PACKETS){
            data_written= 0;
            missing_sdp_packets = 0;
            position_for_retransmission = 0;
            position_in_read_data = 0;
        }


        store_missing_seq_nums(
            msg->data,
            ((msg->length - LENGTH_OF_SDP_HEADER) / WORD_TO_BYTE_MULTIPLIER),
            msg->data[0] == SDP_COMMAND_FOR_START_OF_MISSING_SDP_PACKETS);
        //log_info("free message");
        spin1_msg_free((sdp_msg_t *) msg);

        // if got all missing packets, start retransmitting them to host
        if(missing_sdp_packets == 0){
        
            // packets all received, add finish flag for DMA stoppage
            missing_sdp_seq_num_sdram_address[data_written + 1] = END_FLAG;
            data_written += 1;

            //log_info("create DMA buffers");
            // create the DMA buffers when needed
            for (uint32_t i = 0; i < N_DMA_BUFFERS; i++) {
                retransmit_seq_nums[i] = (uint32_t*) spin1_malloc(
                    ITEMS_PER_DATA_PACKET * sizeof(uint32_t));
            }

            //log_info("start retransmission");
            // start DMA off
            retransmission_dma_read();
        }
    }

    else{
        log_error("received unknown SDP packet");
    }
}

//! boiler plate: not really needed
void resume_callback() {
    time = UINT32_MAX;
}

//! method to make test data in SDRAM
void write_data(){
    // write data into SDRAM for reading later
    store_address = (address_t) sark_xalloc(
        sv->sdram_heap, bytes_to_write, 0,
        ALLOC_LOCK + ALLOC_ID + (sark_vec->app_id << 8));

    uint iterations = (uint)(bytes_to_write / 4);
    //log_info("iterations = %d", iterations - 1);

    for(uint count = 0; count < iterations; count++){
        store_address[count] = count;
    }
}

//! setup
static bool initialize(uint32_t *timer_period) {
    log_info("Initialise: started\n");

    // Get the address this core's DTCM data starts at from SRAM
    address_t address = data_specification_get_data_address();

    // Read the header
    if (!data_specification_read_header(address)) {
        log_error("failed to read the data spec header");
        return false;
    }

    // Get the timing details and set up the simulation interface
    if (!simulation_initialise(
            data_specification_get_region(SYSTEM_REGION, address),
            APPLICATION_NAME_HASH, timer_period, &simulation_ticks,
            &infinite_run, SDP, DMA)) {
        return false;
    }

    // add callback for DMA when dealing with retransmissions
    simulation_dma_transfer_done_callback_on(
        DMA_TAG_READ_FOR_RETRANSMISSION,
        the_dma_complete_read_missing_seqeuence_nums);
    simulation_dma_transfer_done_callback_on(
        DMA_TAG_RETRANSMISSION_READING,
        dma_complete_reading_retransmission_data);
    simulation_dma_transfer_done_callback_on(
        DMA_TAG_FOR_WRITING_MISSING_SEQ_NUMS,
        dma_complete_writing_missing_seq_to_sdram);

    // read config params.
    address_t config_address = data_specification_get_region(CONFIG, address);
    bytes_to_write = config_address[MB];
    key = config_address[MY_KEY];

    log_info("bytes to write is %d", bytes_to_write);

    for (uint32_t i = 0; i < 2; i++) {
        data_to_transmit[i] = (uint32_t*) spin1_malloc(
            ITEMS_PER_DATA_PACKET * sizeof(uint32_t));
    }

    // flags needed for SDP message to go via ethernet
    my_msg.tag = 1;                    // IPTag 1
    my_msg.dest_port = PORT_ETH;       // Ethernet
    my_msg.dest_addr = sv->eth_addr;   // Nearest Ethernet chip

    // fill in SDP source & flag fields
    my_msg.flags = 0x07;
    my_msg.srce_port = 3;
    my_msg.srce_addr = sv->p2p_addr;

    return true;
}

//! start
void c_main() {

    uint32_t timer_period;
    log_info("starting SDRAM reader and writer\n");

    // initialise the model
    if (!initialize(&timer_period)) {
        rt_error(RTE_SWERR);
    }

    // write data
    write_data();

    // set up SDP callback
    simulation_sdp_callback_on(SDP_PORT_FOR_SDP_PACKETS, sdp_reception);

    // set up the DMA complete callbacks per tag
    simulation_dma_transfer_done_callback_on(
        DMA_TAG_READ_FOR_TRANSMISSION,
        dma_complete_reading_for_original_transmission);

    // start execution
    log_info("Starting\n");

    // Start the time at "-1" so that the first tick will be 0
    time = UINT32_MAX;

    simulation_run();
}
