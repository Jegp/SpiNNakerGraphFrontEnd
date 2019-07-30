from pacman.model.graphs.machine import MachineEdge
from tensorflow.python.framework import tensor_util
import logging
import os
import numpy as np
import spinnaker_graph_front_end as front_end
from spinnaker_graph_front_end.examples.TensorSample.mat_mul_vertex_non_dynamic import (MatMulVertexND)
from spinnaker_graph_front_end.examples.TensorSample.softmax_vertex_non_dynamic import (SoftmaxND)
from spinnaker_graph_front_end.examples.TensorSample.const_tensor_vertex_non_dynamic import (ConstTensorVertexND)
from spinnaker_graph_front_end.examples.TensorSample.const_scalar_vertex import (ConstScalarVertex)
from spinnaker_graph_front_end.examples.TensorSample.tf_fill_vertex import (FillVertex)

import tensorflow.compat.v1 as tf
# use functions of TensorFlow version 1 into TensorFlow version 2.
tf.disable_v2_behavior()

logger = logging.getLogger(__name__)

# This sample create only the edge gradients\grad_ys to Fill operator.

front_end.setup(n_chips_required=1, model_binary_folder=os.path.dirname(__file__))
tf.set_random_seed(0)

# Fill operation sample
# ==> [[9, 9, 9]
#     [9, 9, 9]]
# k = tf.fill([], 9)

# This app refers only to grad_ys operation that is invoke as first step inside the gradient sub-graph
# Here is a scalar so ConstScalarVertex is used

x = tf.Variable(1.0, trainable=True, name = "x")
y = tf.square(x)
z = tf.gradients([y], [x])

writer = tf.summary.FileWriter('.')
writer.add_graph(tf.get_default_graph())
writer.flush()

init = tf.global_variables_initializer()
sess = tf.Session()
sess.run(init)
t = sess.run(z)
graph = tf.get_default_graph()

const = {}
for n in tf.get_default_graph().as_graph_def().node:
    if n.op == 'Const':
        if n.name == 'gradients/grad_ys_0':
            const[n.name] = n.attr.get('value').tensor.float_val[0]
        else:
            if not n.attr["value"].tensor.tensor_shape.dim:
                const[n.name] = n.attr.get('value').tensor.float_val[0]
            else:
                const[n.name] = tensor_util.MakeNdarray(n.attr['value'].tensor)


# List of spinnaker vertices
vertices = {}
inputs = {}


def store_input_node_ids (n_id):
    current_inputs = []
    if graph._nodes_by_id[n_id]._inputs:
        for index in graph._nodes_by_id[n_id]._inputs:
            current_inputs.append(index._id)
    inputs[n_id] = current_inputs


# Add Vertices
for n_id in graph._nodes_by_id:
    print('node id :', n_id, 'and name:', graph._nodes_by_id[n_id].name)

    if 'gradients/Fill' in graph._nodes_by_id[n_id].name:
        vertices[n_id] = FillVertex("{} vertex ".format(graph._nodes_by_id[n_id].name))

    elif 'gradients/grad_ys_0' in graph._nodes_by_id[n_id].name:
        vertices[n_id] = ConstScalarVertex("{} vertex ".format(graph._nodes_by_id[n_id].name),
                                           int(const[graph._nodes_by_id[n_id].name]))  # when the floats
                                                                                       # are handled in C the cast to int will be removed
    else:
        continue

    store_input_node_ids(n_id)

    vertices[n_id].name = graph._nodes_by_id[n_id].name
    front_end.add_machine_vertex_instance(vertices[n_id])

# Add Edges
for n_id in vertices:
    # Check if this vertex has inputs nodes
    if n_id in inputs :
        # Iterate over input ids of the nodes
        for input_key in inputs[n_id]:
            # add the edge
            front_end.add_machine_edge_instance(MachineEdge(vertices[input_key], vertices[n_id],
                                                label=vertices[input_key].name + ': to ' + vertices[n_id].name),
                                                "OPERATION_PARTITION")

sess.close()

print("run simulation")
front_end.run(1)

placements = front_end.placements()
txrx = front_end.transceiver()

print("read SDRAM after run")
for placement in sorted(placements.placements,
                        key=lambda p: (p.x, p.y, p.p)):

    if isinstance(placement.vertex, ConstTensorVertexND):
        const_value = placement.vertex.read(placement, txrx)
        logger.info("CONST {}, {}, {} > {}".format(
            placement.x, placement.y, placement.p, const_value))

    if isinstance(placement.vertex, MatMulVertexND):
        oper_results = placement.vertex.read(placement, txrx)
        logger.info("Mat Mul {}, {}, {} > {}".format(
            placement.x, placement.y, placement.p, oper_results))

front_end.stop()