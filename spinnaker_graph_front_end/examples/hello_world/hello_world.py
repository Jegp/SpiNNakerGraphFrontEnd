"""
Hello World program on Spinnaker

Each core stores into its region in SDRAM the string:
"Hello World from $chip.x, $chip.y, $core"

We then fetch the written data and print it on the python console.
"""

import spinnaker_graph_front_end as front_end

from spinnaker_graph_front_end.examples.hello_world.hello_world_vertex\
    import HelloWorldVertex

import logging
import os


def do_run():
    logger = logging.getLogger(__name__)

    front_end.setup(n_chips_required=None)

    machine = front_end.machine()

    # machine_time_step = 1000
    # time_scale_factor = 1

    # calculate total number of 'free' cores for the given board
    # (i.e. does not include those busy with SARK or reinjection)
    total_number_of_cores = len([
        processor for chip in machine.chips for processor in chip.processors
        if not processor.is_monitor])

    # fill all cores with a HelloWorldVertex each
    for x in range(0, total_number_of_cores):
        front_end.add_machine_vertex(
            HelloWorldVertex,
            {},
            label="Hello World at x {}".format(x))

    front_end.run(10)

    placements = front_end.placements()
    buffer_manager = front_end.buffer_manager()

    for placement in sorted(placements.placements,
                            key=lambda p: (p.x, p.y, p.p)):
        hello_world = placement.vertex.read(placement, buffer_manager)
        logger.info("{}, {}, {} > {}".format(
            placement.x, placement.y, placement.p, hello_world))

    front_end.stop()


def run_script():
    """
    Runs the script making sure you are in the correct directory
        so files can be found
    """
    class_file = __file__
    path = os.path.dirname(os.path.abspath(class_file))
    os.chdir(path)
    do_run()


if __name__ == '__main__':
    do_run()
