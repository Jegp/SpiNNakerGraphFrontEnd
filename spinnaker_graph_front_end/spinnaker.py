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

import logging
import os
from spinn_utilities.abstract_base import AbstractBase
from spinn_utilities.overrides import overrides
from spinn_utilities.log import FormatAdapter
from spinn_front_end_common.interface.abstract_spinnaker_base import (
    AbstractSpinnakerBase)
from spinn_front_end_common.interface.config_handler import ConfigHandler
from spinn_front_end_common.utilities import SimulatorInterface
from spinn_front_end_common.utilities import globals_variables
from spinn_front_end_common.utilities.failed_state import FailedState
from ._version import __version__ as version

logger = FormatAdapter(logging.getLogger(__name__))

#: The name of the configuration file
CONFIG_FILE_NAME = "spiNNakerGraphFrontEnd.cfg"


def _is_allocated_machine(config):
    return (config.get("Machine", "spalloc_server") != "None" or
            config.get("Machine", "remote_spinnaker_url") != "None")


class GraphFrontEndSimulatorInterface(
        SimulatorInterface, metaclass=AbstractBase):
    """ The simulator interface exported by the graph front end. A very thin\
        layer over the capabilities of the Front End Common package.
    """
    __slots__ = ()


class SpiNNaker(AbstractSpinnakerBase, GraphFrontEndSimulatorInterface):
    """ The implementation of the SpiNNaker simulation interface.

    .. note::
        You should not normally instantiate this directly from user code.
        Call :py:func:`~spinnaker_graph_front_end.setup` instead.
    """
    #: The base name of the configuration file (but no path)
    __slots__ = (
        "_user_dsg_algorithm"
    )

    #: The name of the configuration validation configuration file
    VALIDATION_CONFIG_NAME = "validation_config.cfg"

    @staticmethod
    def extended_config_path():
        """ The full name of the configuration file.

        :rtype: str
        """
        return os.path.join(os.path.dirname(__file__), CONFIG_FILE_NAME)

    def __init__(
            self, executable_finder, host_name=None, graph_label=None,
            database_socket_addresses=(), dsg_algorithm=None,
            n_chips_required=None, n_boards_required=None,
            extra_pre_run_algorithms=(),
            extra_post_run_algorithms=(), time_scale_factor=None,
            machine_time_step=None, default_config_paths=(),
            extra_xml_paths=()):
        """
        :param executable_finder:
            How to find the executables
        :type executable_finder:
            ~spinn_front_end_common.utilities.utility_objs.ExecutableFinder
        :param str host_name:
            The SpiNNaker machine address
        :param str graph_label:
            A label for the graph
        :param database_socket_addresses:
            Extra sockets that will want to be notified about the location of
            the runtime database.
        :type database_socket_addresses:
            ~collections.abc.Iterable(~spinn_utilities.socket_address.SocketAddress)
        :param str dsg_algorithm:
            Algorithm to use for generating data
        :param int n_chips_required:
            How many chips are required.
            *Prefer ``n_boards_required`` if possible.*
        :param int n_boards_required:
            How many boards are required. Unnecessary with a local board.
        :param ~collections.abc.Iterable(str) extra_pre_run_algorithms:
            The names of any extra algorithms to call before running
        :param ~collections.abc.Iterable(str) extra_post_run_algorithms:
            The names of any extra algorithms to call after running
        :param int time_scale_factor:
            The time slow-down factor
        :param int machine_time_step:
            The size of the machine time step, in microseconds
        :param ~collections.abc.Iterable(str) default_config_paths:
            Where to look for configurations
        :param ~collections.abc.Iterable(str) extra_xml_paths:
            Where to look for algorithm descriptors
        """
        # DSG algorithm store for user defined algorithms
        self._user_dsg_algorithm = dsg_algorithm

        front_end_versions = [("SpiNNakerGraphFrontEnd", version)]

        # support extra configs
        this_default_config_paths = list()
        this_default_config_paths.append(self.extended_config_path())
        if default_config_paths is not None:
            this_default_config_paths.extend(default_config_paths)

        super().__init__(
            configfile=CONFIG_FILE_NAME,
            executable_finder=executable_finder,
            graph_label=graph_label,
            database_socket_addresses=database_socket_addresses,
            extra_algorithm_xml_paths=extra_xml_paths,
            n_chips_required=n_chips_required,
            n_boards_required=n_boards_required,
            default_config_paths=this_default_config_paths,
            validation_cfg=os.path.join(os.path.dirname(__file__),
                                        self.VALIDATION_CONFIG_NAME),
            front_end_versions=front_end_versions)

        if _is_allocated_machine(self.config) and \
                n_chips_required is None and n_boards_required is None:
            self.set_n_boards_required(1)

        extra_mapping_inputs = dict()
        extra_mapping_inputs["CreateAtomToEventIdMapping"] = self.config.\
            getboolean("Database", "create_routing_info_to_atom_id_mapping")

        self.update_extra_mapping_inputs(extra_mapping_inputs)
        self.prepend_extra_pre_run_algorithms(extra_pre_run_algorithms)
        self.extend_extra_post_run_algorithms(extra_post_run_algorithms)

        self.set_up_machine_specifics(host_name)
        self.set_up_timings(machine_time_step, time_scale_factor)

        # if not set at all, set to 1 for real time execution.
        if self.time_scale_factor is None:
            self._config.set("Machine", "time_scale_factor", 1)
        logger.info("Setting time scale factor to {}."
                    .format(self.time_scale_factor))
        logger.info("Setting machine time step to {} micro-seconds."
                    .format(self.machine_time_step))

    @property
    def is_allocated_machine(self):
        """ Is this an allocated machine? Otherwise, it is local.

        :rtype: bool
        """
        return _is_allocated_machine(self.config)

    def run(self, run_time):
        """ Run a simulation for a fixed amount of time

        :param int run_time: the run duration in milliseconds.
        """
        # pylint: disable=arguments-differ

        # set up the correct DSG algorithm
        if self._user_dsg_algorithm is not None:
            self.dsg_algorithm = self._user_dsg_algorithm

        # run normal procedure
        super().run(run_time)

    def __repr__(self):
        return "SpiNNaker Graph Front End object for machine {}".format(
            self._hostname)


class _GraphFrontEndFailedState(GraphFrontEndSimulatorInterface, FailedState):
    """ The special object that indicates that the simulator has failed.
    """
    __slots__ = ()

    @property
    @overrides(FailedState.config)
    def config(self):
        logger.warning(
            "Accessing config before setup is not recommended as setup could"
            " change some config values. ")
        handler = ConfigHandler(
            CONFIG_FILE_NAME, [SpiNNaker.extended_config_path()], [])
        return handler.config


# At import time change the default FailedState
globals_variables.set_failed_state(_GraphFrontEndFailedState())
