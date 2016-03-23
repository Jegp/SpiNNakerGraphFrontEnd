
from pacman.model.partitioned_graph.partitioned_vertex import PartitionedVertex
from pacman.model.resources.cpu_cycles_per_tick_resource import \
    CPUCyclesPerTickResource
from pacman.model.resources.dtcm_resource import DTCMResource
from pacman.model.resources.resource_container import ResourceContainer
from pacman.model.resources.sdram_resource import SDRAMResource

from spinn_front_end_common.abstract_models.\
    abstract_partitioned_data_specable_vertex import \
    AbstractPartitionedDataSpecableVertex
from spinn_front_end_common.interface.buffer_management.\
    buffer_models.receives_buffers_to_host_basic_impl import \
    ReceiveBuffersToHostBasicImpl

from spinn_front_end_common.utilities import constants

from data_specification.data_specification_generator import \
    DataSpecificationGenerator

from spinnaker_graph_front_end.utilities.conf import config

from enum import Enum

import logging

logger = logging.getLogger(__name__)


class HelloWorldVertex(
        PartitionedVertex, AbstractPartitionedDataSpecableVertex,
        ReceiveBuffersToHostBasicImpl):

    DATA_REGIONS = Enum(
        value="DATA_REGIONS",
        names=[('SYSTEM', 0),
               ('STRING_DATA', 1),
               ('BUFFERED_STATE', 2)])

    CORE_APP_IDENTIFIER = 0xBEEF

    def __init__(self, label, machine_time_step, time_scale_factor,
                 constraints=None):

        resources = ResourceContainer(cpu=CPUCyclesPerTickResource(45),
                                      dtcm=DTCMResource(100),
                                      sdram=SDRAMResource(100))

        PartitionedVertex.__init__(
            self, label=label, resources_required=resources,
            constraints=constraints)
        AbstractPartitionedDataSpecableVertex.__init__(
            self, machine_time_step, time_scale_factor)
        ReceiveBuffersToHostBasicImpl.__init__(self)

        self._buffer_size_before_receive = config.getint(
            "Buffers", "buffer_size_before_receive")

        self._time_between_requests = config.getint(
            "Buffers", "time_between_requests")

        self._string_data_size = 5000

        self.placement = None

    def get_binary_file_name(self):
        return "hello_world.aplx"

    def model_name(self):
        return "Hello_World_Vertex"

    def generate_data_spec(
            self, placement, sub_graph, routing_info, hostname, report_folder,
            ip_tags, reverse_ip_tags, write_text_specs,
            application_run_time_folder):
        """
        method to determine how to generate their data spec for a non neural
        application

        :param placement: the placement object for the dsg
        :param sub_graph: the partitioned graph object for this dsg
        :param routing_info: the routing info object for this dsg
        :param hostname: the machines hostname
        :param ip_tags: the collection of iptags generated by the tag allocator
        :param reverse_ip_tags: the collection of reverse iptags generated by
        the tag allocator
        :param report_folder: the folder to write reports to
        :param write_text_specs: bool which says if test specs should be written
        :param application_run_time_folder: the folder where application files
         are written
        """
        self.placement = placement

        data_writer, report_writer = \
            self.get_data_spec_file_writers(
                placement.x, placement.y, placement.p, hostname, report_folder,
                write_text_specs, application_run_time_folder)

        spec = DataSpecificationGenerator(data_writer, report_writer)
        # Setup words + 1 for flags + 1 for recording size
        setup_size = (constants.DATA_SPECABLE_BASIC_SETUP_INFO_N_WORDS + 2) * 4

        # Reserve SDRAM space for memory areas:

        # Create the data regions for hello world
        self._reserve_memory_regions(spec, setup_size)
        self._write_basic_setup_info(spec, self.DATA_REGIONS.SYSTEM.value)
        self.write_recording_data(
            spec, ip_tags, [self._string_data_size],
            self._buffer_size_before_receive, self._time_between_requests)

        # End-of-Spec:
        spec.end_specification()
        data_writer.close()

        # return file path for dsg targets
        return data_writer.filename

    def _reserve_memory_regions(self, spec, system_size):
        """
        *** Modified version of same routine in abstract_models.py These could
        be combined to form a common routine, perhaps by passing a list of
        entries. ***
        Reserve memory for the system, indices and spike data regions.
        The indices region will be copied to DTCM by the executable.
        :param spec:
        :param system_size:
        :return:
        """
        spec.reserve_memory_region(region=self.DATA_REGIONS.SYSTEM.value,
                                   size=system_size, label='systemInfo')
        self.reserve_buffer_regions(
            spec, self.DATA_REGIONS.BUFFERED_STATE.value,
            [self.DATA_REGIONS.STRING_DATA.value],
            [self._string_data_size])

    def read(self, placement, buffer_manager):
        """
        returns the data written into sdram
        :param placement: the location of this vertex
        :param buffer_manager: the buffer manager
        :return: string output
        """
        data_pointer, missing_data = buffer_manager.get_data_for_vertex(
            placement, self.DATA_REGIONS.STRING_DATA.value,
            self.DATA_REGIONS.BUFFERED_STATE.value)
        if missing_data:
            raise Exception("missing data!")
        record_raw = data_pointer.read_all()
        output = str(record_raw)
        return output

    def is_partitioned_data_specable(self):
        """
        helper method for isinstance
        :return:
        """
        return True