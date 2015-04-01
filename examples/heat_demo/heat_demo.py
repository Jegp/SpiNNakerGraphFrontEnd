"""
heat demo main entrance
"""
from examples.heat_demo.heat_demo_command_edge import HeatDemoCommandEdge
from spinn_front_end_common.utility_models.live_packet_gather import \
    LivePacketGather
import spynnaker_graph_front_end as front_end
from spynnaker_graph_front_end import ReverseIpTagMultiCastSource
from spynnaker_graph_front_end import MultiCastPartitionedEdge

from examples.heat_demo.heat_demo_vertex import HeatDemoVertexPartitioned
from examples.heat_demo.heat_demo_edge import HeatDemoEdge
from examples import heat_demo

# set up the front end and ask for the detected machines dimensions
front_end.setup(graph_label="heat_demo_graph", model_binary_folder=heat_demo)
dimenions = front_end.get_machine_dimensions()

machine_time_step = 1
time_scale_factor = 1
machine_port = 11111
machine_recieve_port = 22222

vertices = [None] * (dimenions['x'] * 4)


command_injector = \
    front_end.add_partitioned_vertex(
        ReverseIpTagMultiCastSource,
        {'n_neurons': 1, 'machine_time_step': machine_time_step,
         'timescale_factor': time_scale_factor, 'label': "injector_from_vis",
         'port': machine_port})

live_gatherer = \
    front_end.add_partitioned_vertex(
        LivePacketGather,
        {'machine_time_step': machine_time_step,
         'timescale_factor': time_scale_factor,
         'label': "gatherer from heat elements",
         'ip_address': "local_host",
         'port': machine_recieve_port}
    )

# build vertices
for x_position in range(0, (dimenions['x'] * 4)):
    for y_position in range(0, (dimenions['y'] * 4)):
        element = front_end.add_partitioned_vertex(
            HeatDemoVertexPartitioned,
            {'machine_time_step': machine_time_step,
             'time_scale_factor': time_scale_factor},
            label="heat_element at coords {}:{}".format(x_position, y_position))
        if vertices[x_position] is None:
            vertices[x_position] = list()
        vertices[x_position].append(element)

# build edges
for x_position in range(0, dimenions['x']):
    for y_position in range(0, dimenions['y']):
        # add a link from the injecotr to the heat element
        front_end.add_partitioned_edge(
            HeatDemoCommandEdge,
            {'pre_subvertex': command_injector,
             'post_subvertex': vertices[x_position][y_position],
             'n_keys': 3},
            label="injector edge for vertex {}"
                  .format(vertices[x_position][y_position].label))
        # add a link from the heat element to the live packet gatherer
        front_end.add_partitioned_edge(
            MultiCastPartitionedEdge,
            {'pre_subvertex': vertices[x_position][y_position],
             'post_subvertex': live_gatherer},
            label="gatherer edge from vertex {} to live packet gatherer"
                  .format(vertices[x_position][y_position].label))
        # check for the likely hood for a N link
        if ((x_position + 1) % dimenions['x']) != 0:
            front_end.add_partitioned_edge(
                HeatDemoEdge,
                {'pre_subvertex': vertices[x_position][y_position],
                 'post_subvertex': vertices[x_position + 1][y_position],
                 'direction': HeatDemoEdge.DIRECTIONS.NORTH},
                label="North edge between heat elements {}:{}"
                      .format(vertices[x_position][y_position],
                              vertices[x_position + 1][y_position]),)
        else:
            front_end.add_partitioned_edge(
                HeatDemoEdge,
                {'pre_subvertex': command_injector,
                 'post_subvertex': vertices[x_position][y_position],
                 'direction': HeatDemoEdge.DIRECTIONS.NORTH},
                label="injected temp for north edge of fabric for heat element"
                      "{}:{}".format(vertices[x_position][y_position],
                                     vertices[x_position + 1][y_position]),)
        # check for the likely hood for a E link
        if ((y_position + 1) % dimenions['y']) != 0:
            front_end.add_partitioned_edge(
                HeatDemoEdge,
                {'pre_subvertex': vertices[x_position][y_position],
                 'post_subvertex': vertices[x_position][y_position + 1],
                 'direction': HeatDemoEdge.DIRECTIONS.EAST},
                label="East edge between heat elements {}:{}"
                      .format(vertices[x_position][y_position],
                              vertices[x_position][y_position + 1]),)
        else:
            front_end.add_partitioned_edge(
                HeatDemoEdge,
                {'pre_subvertex': command_injector,
                 'post_subvertex': vertices[x_position][y_position],
                 'direction': HeatDemoEdge.DIRECTIONS.EAST},
                label="Injected temp for East edge of fabric for heat element"
                      " {}:{}".format(vertices[x_position][y_position],
                                      vertices[x_position][y_position + 1]),)
        # check for the likely hood for a S link
        if ((y_position - 1) % dimenions['y']) != 0:
            front_end.add_partitioned_edge(
                HeatDemoEdge,
                {'pre_subvertex': vertices[x_position][y_position],
                 'post_subvertex': vertices[x_position][y_position - 1],
                 'direction': HeatDemoEdge.DIRECTIONS.SOUTH},
                label="South edge between heat elements {}:{}"
                      .format(vertices[x_position][y_position],
                              vertices[x_position][y_position - 1]),)
        else:
            front_end.add_partitioned_edge(
                HeatDemoEdge,
                {'pre_subvertex': command_injector,
                 'post_subvertex': vertices[x_position][y_position],
                 'direction': HeatDemoEdge.DIRECTIONS.SOUTH},
                label="Injected temp for South edge of fabric for heat element"
                      " {}:{}".format(vertices[x_position][y_position],
                                      vertices[x_position][y_position - 1]),)
        # check for the likely hood for a W link
        if ((x_position - 1) % dimenions['x']) != 0:
            front_end.add_partitioned_edge(
                HeatDemoEdge,
                {'pre_subvertex': vertices[x_position][y_position],
                 'post_subvertex': vertices[x_position - 1][y_position],
                 'direction': HeatDemoEdge.DIRECTIONS.WEST},
                label="West edge between heat elements {}:{}"
                      .format(vertices[x_position][y_position],
                              vertices[x_position - 1][y_position]),)
        else:
            front_end.add_partitioned_edge(
                HeatDemoEdge,
                {'pre_subvertex': command_injector,
                 'post_subvertex': vertices[x_position][y_position],
                 'direction': HeatDemoEdge.DIRECTIONS.WEST},
                label="Injected temp for West edge of fabric for heat element"
                      " {}:{}".format(vertices[x_position][y_position],
                                      vertices[x_position - 1][y_position]),)

front_end.run(10000)
front_end.stop()