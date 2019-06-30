
//! imports
#include "spin1_api.h"
#include "common-typedefs.h"
#include <data_specification.h>
#include <recording.h>
#include <simulation.h>
#include <debug.h>


int value_a;
int value_b;
int counter = 0;
int result = 0;

int expected_packets=0;

// Transmission info
uint my_key;
uint32_t pre_vertex1_key;
uint32_t pre_vertex2_key;
int is_matrix1=0;
int is_matrix2=0;

// Tensor properties
int size1 = 0;
int size2 = 0;
int rank1 =0;
int rank2 =0;
uint32_t* shape1;
uint32_t* shape2;
uint32_t* tensor1;
uint32_t* tensor2;

uint32_t oper_type = 0;
uint key_exist = 0; 
address_t address = NULL;

typedef enum regions_e {
    PREVERTEX_KEYS,
    OPER_TYPE,
    TRANSMISSIONS,
    RECORDED_DATA
} regions_e;


typedef enum callback_priorities{
    MC_PACKET = -1, USER = 3
} callback_priorities;


typedef enum oper_type_region_element {
    OPER_TYPE_POSITION
} initial_state_region_elements;


typedef enum transmission_region_elements {
    HAS_KEY, MY_KEY
} transmission_region_elements;


void send_value(uint data){
    log_info("addition send_value\n", my_key);

    log_info("sending value via multicast with key %d",
              my_key);
    while (!spin1_send_mc_packet(my_key, data, WITH_PAYLOAD)) {
        spin1_delay_us(1);
    }

}

void record_data(int result) {
    log_info("Recording data\n");

    address_t record_region =
        data_specification_get_region(RECORDED_DATA, address);
    uint8_t* record_space_address = (uint8_t*) record_region;
    spin1_memcpy(record_space_address, &result, 4);
    log_info("recorded result %d address: %u\n", result ,record_space_address);

}

int addition(int a, int b){
    log_info("addition\n");
    int sum;
    log_info("Addition of A %d and B %d \n", a , b);
    sum = a + b;
    log_info("Addition Result : %d \n", sum);
    return sum;
}

// Todo : specify which value is Substracted after the reception.
int sub(int a, int b){
    log_info("subtraction\n");
    int res;
    log_info("Subtraction from A %d the value B %d \n", a , b);
    res = a - b;
    log_info("Subtraction Result : %d \n", res);
    return res;
}


int a[2][3] = {
    {1, 2, 3},
    {4, 5, 6}
};

int b[3][2] = {
    {7, 8},
    {9, 10},
    {11,12}
};

//int multiply[][2];

// int* mat_mul(int a[][N], int* shape_a, int b[][N], int* shape_b){
//void mat_mul(){

  //  log_info("mat_mul\n");

//    for(int row=0; row<2; row++){
//        for(int col=0; col<2; col++){
//            for(int i=0; i<3; i++){
//                multiply[row][col] += a[row][i] * b[i][col];
//                log_info("Mul %d", multiply[row][col]);
//            }
//        }
//    }
    // for(int row=0; i<shape_a[0]; i++)
    //     for(int col=0; j<shape_b[1]; j++)
    //         for(int i=0; i<shape_a[1] ;i++)
    //             multiply[row][col] = a[row][i] + b[i][col]
//}

void mat_mul_2D(){
    log_info("mat_mul_2D\n");

    int multiply[4];
    int sum=0;
    int l = 0;
    log_info("shape1[0] %d \n", shape1[0]);
    for(int i=0; i<shape1[0]; i++){
        for(int j=0; j<shape2[1]; j++){
            for(int k=0; k<shape1[0]; k++){
                log_info(" i, j, k %d %d %d :\n", i, j, k);
                log_info(" k+ a_dim2*i*j  : (k * b_dim2) + i  %d %d :\n", tensor1[k+ shape1[1]*i], tensor2[(k * shape2[1]) + j] );
                sum += tensor1[k+ shape1[1]*i] * tensor2[(k * shape2[1]) + j];
            }
            multiply[l] = sum;
            log_info(" multiply[%d] %d :\n", l, multiply[l]);
            sum=0;
            l++;
        }
    }
}

int mul(int a, int b){
    log_info("multiplication\n");
    int res;
    log_info("Mul of A %d and B %d \n", a , b);
    res = a * b;
    log_info("Mul Result : %d \n", res);
    return res;
}

// Todo : Handling of cast values of Tensorflow.
int div(int a, int b){
    log_info("division\n");
    int res;
    log_info("Division from A %d the value B %d \n", a , b);
    res = a / b;
    log_info("Division Result : %d \n", res);
    return res;
}

void receive_data(uint key, uint payload) {
    log_info("key %d , data %d\n", key, payload);
    ++counter;
    // Check size1 of vertex 1
    if (key == pre_vertex1_key && payload > 1){
        log_info("V1:size1 is greater than 1, matrix reception");
        size1 = payload;
        // reserve space for tensor
        tensor1 = (uint32_t*) spin1_malloc(size1 * sizeof(uint32_t));

        is_matrix1 = 1;
    }
    // If matrix get rank1
    else if (is_matrix1 ==1 && key == pre_vertex1_key+1){
        log_info("V1:rank1 received %d\n", payload);
        rank1 = payload;
        shape1 = (uint32_t*) spin1_malloc(rank1 * sizeof(uint32_t));
    }
    // Get Shape of Tensor
    else if (is_matrix1 ==1 && key > pre_vertex1_key+1 && key <= pre_vertex1_key+1+ rank1){
        shape1[key-2] = payload;
        log_info("V1:key-2 %d\n", key-2);
        log_info("V1:shape1 value %d\n", shape1[key-2]);
    }

    // Get Tensor values
    else if (key > pre_vertex1_key+1+ rank1 && key <= pre_vertex1_key+1+ rank1 + size1){
        tensor1[key-pre_vertex1_key] = payload;
        log_info("V1:tensor1 value %d\n", tensor1[key-pre_vertex1_key]);
    }


    // Check size2 of vertex 2
    // if (key == pre_vertex2_key && payload > 1){
    //     log_info("V2:size2 is greater than 1, matrix reception");
    //     size2 = payload;
    //     // reserve space for tensor
    //     tensor2 = (uint32_t*) spin1_malloc(size2 * sizeof(uint32_t));

    //     is_matrix2 = 1;
    // }
    // If matrix get rank2
    // else if (is_matrix2 ==1 && key == pre_vertex2_key+1){
    //     log_info("V2:rank2 received %d\n", payload);
    //     rank2 = payload;
    //     shape2 = (uint32_t*) spin1_malloc(rank2 * sizeof(uint32_t));
    // }
    // // Get Shape of Tensor
    // else if (is_matrix2 ==1 && key > pre_vertex2_key+1 && key <= pre_vertex2_key+1+ rank2){
    //     shape2[key-2] = payload;
    //     log_info("V2:shape2 value %d\n", shape2[key-2]);
    // }

    // // Get Tensor values
    // else if (key > pre_vertex2_key+1+ rank2 && key <= pre_vertex2_key+1+ rank2 + size2){
    //     tensor2[key-pre_vertex2_key] = payload;
    //     log_info("V2:tensor2 value %d\n", tensor2[key-pre_vertex2_key]);
    // }

    if (is_matrix1 ==1){

        if(counter == (2 + size1 + rank1)) {
            log_info("Both tensors received\n");
            log_info("V1:shape1 test value %d\n", shape1[0]);

            //mat_mul_2D();

        }
    }
    // mat_mul();

    // if(counter == 1){
    //     value_a = payload;
    // }
    // else{
    //     value_b = payload;

    //     if(oper_type == 1){
    //         result = addition(value_a, value_b);
    //     }

    //     if(oper_type == 2){
    //         result = mul(value_a, value_b);
    //     }

    //     if(oper_type == 3){
    //         result = sub(value_a, value_b);
    //     }

    //     // if(oper_type == 4){
    //     //     
    //     // }

    //     // if(oper_type == 5){
    //     //     result = div(value_a, value_b);
    //     // }

    //     if(key_exist == 1){
    //         send_value(result);
    //     }
  
    //     record_data(result);
    //     spin1_exit(0);

    // }
    
}

static bool initialize() {
    log_info("Initialise addition: started\n");

    // Get the address this core's DTCM data starts at from SDRAM
    address = data_specification_get_data_address();
    log_info("address is %u\n", address);

    // Read the header
    if (!data_specification_read_header(address)) {
        log_error("failed to read the data spec header");
        return false;
    }

    // read prevertex keys
    address_t prevertex_keys_region_address = data_specification_get_region(PREVERTEX_KEYS, address);
    pre_vertex1_key = prevertex_keys_region_address[0];
    pre_vertex2_key = prevertex_keys_region_address[1];
    log_info("prevertex 1 key is %d\n", pre_vertex1_key);
    log_info("prevertex 2 key is %d\n", pre_vertex2_key);

    // read my oper type value
    address_t oper_type_region_address = data_specification_get_region(OPER_TYPE, address);
    oper_type = oper_type_region_address[OPER_TYPE_POSITION];
    log_info("my oper type value is %d\n", oper_type);

    // initialise transmission keys
    address_t transmission_region_address = data_specification_get_region(
            TRANSMISSIONS, address);
    log_info("transmission_region_address  is %u\n", transmission_region_address);
    // a pointer to uint32 and if the first element of this array exists so has key do the code bellow
    if (transmission_region_address[HAS_KEY] == 1) {
        key_exist = 1;
        my_key = transmission_region_address[MY_KEY];
        log_info("my key is %d\n", my_key);
    } else {
        log_info("Addition vertex without key, just perform the addition and record the result");
    }

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
    log_info("starting Tensor operation\n");

    // initialise the model
    if (!initialize()) {
        rt_error(RTE_SWERR);
    }

    spin1_callback_on(MCPL_PACKET_RECEIVED, receive_data, MC_PACKET);

    log_info("Starting\n");

spin1_start(SYNC_WAIT);
}
