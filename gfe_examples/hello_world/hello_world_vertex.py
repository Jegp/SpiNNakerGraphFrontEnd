# Copyright (c) 2017-2019 The University of Manchester
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

from enum import IntEnum
import logging
from spinn_utilities.overrides import overrides
from pacman.model.graphs.machine import MachineVertex
from pacman.model.resources import ResourceContainer, ConstantSDRAM
from spinn_front_end_common.utilities.constants import SYSTEM_BYTES_REQUIREMENT
from spinn_front_end_common.utilities.helpful_functions import (
    locate_memory_region_for_placement)
from spinn_front_end_common.abstract_models.impl import (
    MachineDataSpecableVertex)
from spinn_front_end_common.interface.buffer_management.buffer_models import (
    AbstractReceiveBuffersToHost)
from spinn_front_end_common.interface.buffer_management.recording_utilities \
    import (
        get_recording_header_size)
from spinnaker_graph_front_end.utilities import SimulatorVertex

logger = logging.getLogger(__name__)


class DataRegions(IntEnum):
    SYSTEM = 0
    STRING_DATA = 1


class Channels(IntEnum):
    HELLO = 0


class HelloWorldVertex(
        SimulatorVertex, MachineDataSpecableVertex,
        AbstractReceiveBuffersToHost):

    def __init__(self, n_hellos, label=None, constraints=None):
        super().__init__(label, "hello_world.aplx", constraints=constraints)

        self._string_data_size = n_hellos * 13

    @property
    @overrides(MachineVertex.resources_required)
    def resources_required(self):
        resources = ResourceContainer(sdram=ConstantSDRAM(
            SYSTEM_BYTES_REQUIREMENT +
            get_recording_header_size(len(Channels)) +
            self._string_data_size))

        return resources

    @overrides(MachineDataSpecableVertex.generate_machine_data_specification)
    def generate_machine_data_specification(
            self, spec, placement, machine_graph, routing_info, iptags,
            reverse_iptags, machine_time_step, time_scale_factor):
        # Generate the system data region for simulation .c requirements
        self.generate_system_region(spec)

        # Make the data regions for hello world; it's just a recording region
        self.generate_recording_region(
            spec, DataRegions.STRING_DATA, [self._string_data_size])

        # End-of-Spec:
        spec.end_specification()

    def read(self):
        """ Get the data written into SDRAM

        :return: string output
        """
        raw_data, missing_data = self.get_recording_channel_data(0)
        if missing_data:
            raise Exception("missing data!")
        return str(bytearray(raw_data))

    @overrides(AbstractReceiveBuffersToHost.get_recorded_region_ids)
    def get_recorded_region_ids(self):
        return [Channels.HELLO]

    @overrides(AbstractReceiveBuffersToHost.get_recording_region_base_address)
    def get_recording_region_base_address(self, txrx, placement):
        return locate_memory_region_for_placement(
            placement, DataRegions.STRING_DATA, txrx)
