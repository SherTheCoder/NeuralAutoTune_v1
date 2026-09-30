"""Rewrite dilated convolutions onto a phase grid so the NPU can run them as
plain convolutions. Used for both networks.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import json
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, numpy_helper

W_TOTAL = 480
ACTS = ("Relu", "Tanh")
ELEMWISE = ("QuantizeLinear", "DequantizeLinear", "Relu", "Tanh", "Mul", "Add")


def _attr(node, name, default=None):
    for a in node.attribute:
        if a.name == name:
            return helper.get_attribute_value(a)
    return default


def rewrite(model: onnx.ModelProto, quant: dict, w_total: int = W_TOTAL):
    g = model.graph
    nodes = list(g.node)
    by_output = {o: n for n in nodes for o in n.output}
    consumers = {}
    for n in nodes:
        for i in n.input:
            consumers.setdefault(i, []).append(n)
    inits = {i.name: i for i in g.initializer}

    dilated = [n for n in nodes if n.op_type == "Conv"
               and (_attr(n, "dilations") or [1, 1])[1] > 1]
    assert dilated, "no dilated convs found -- already rewritten?"

    removed, added_tensors = [], []
    removed_pads = []
    last_relu_out = None

    for conv in nodes:
        if conv.op_type != "Conv" or conv in dilated:
            continue
        src_node = by_output.get(conv.input[0])
        if src_node is None or src_node.op_type != "Pad":
            continue
        pads_w = numpy_helper.to_array(inits[src_node.input[1]])
        n_ax = len(pads_w) // 2
        p_lo, p_hi = int(pads_w[n_ax - 1]), int(pads_w[-1])
        conv.input[0] = src_node.input[0]
        for at in list(conv.attribute):
            if at.name == "pads":
                conv.attribute.remove(at)
        conv.attribute.append(helper.make_attribute("pads", [0, p_lo, 0, p_hi]))
        removed_pads.append(src_node)

    for conv in dilated:
        d = _attr(conv, "dilations")[1]
        assert w_total % d == 0, (conv.name, d)
        assert _attr(conv, "kernel_shape") == [1, 5], conv.name
        assert _attr(conv, "pads") == [0, 0, 0, 0], \
            f"{conv.name}: expected explicit Pad node, found conv pads"

        pad = by_output[conv.input[0]]
        assert pad.op_type == "Pad", (conv.name, pad.op_type)
        pads_w = numpy_helper.to_array(inits[pad.input[1]])
        n_ax = len(pads_w) // 2
        pad_lo, pad_hi = int(pads_w[n_ax - 1]), int(pads_w[-1])
        assert pad_lo % d == 0 and pad_hi % d == 0, (
            f"{conv.name}: W pads ({pad_lo},{pad_hi}) not divisible by dilation "
            f"{d} -- cannot re-index onto the phase grid")
        h_lo, h_hi = pad_lo // d, pad_hi // d
        src = pad.input[0]
        removed.append(pad)

        cout, cin = numpy_helper.to_array(inits[conv.input[1]]).shape[:2]
        q = w_total // d

        shp_name = f"{conv.name}_pg_shape"
        pg_name = f"{conv.name}_pg_in"
        g.initializer.append(numpy_helper.from_array(
            np.array([1, cin, q, d], np.int64), shp_name))
        rs = helper.make_node("Reshape", [src, shp_name], [pg_name],
                              name=f"{conv.name}_pg_reshape")
        g.node.insert(list(g.node).index(conv), rs)
        added_tensors.append((pg_name, src, f"{conv.name}_pg_reshape"))

        conv.input[0] = pg_name
        for a in list(conv.attribute):
            if a.name in ("dilations", "kernel_shape", "pads"):
                conv.attribute.remove(a)
        conv.attribute.extend([
            helper.make_attribute("dilations", [1, 1]),
            helper.make_attribute("kernel_shape", [5, 1]),
            helper.make_attribute("pads", [h_lo, 0, h_hi, 0]),
        ])
        w = inits[conv.input[1]]
        assert list(w.dims) == [cout, cin, 1, 5], conv.name
        w.dims[:] = [cout, cin, 5, 1]

        t = conv
        saw_act = False
        while True:
            nxt = consumers.get(t.output[0], [])
            if len(nxt) != 1 or nxt[0].op_type not in ELEMWISE:
                break
            t = nxt[0]
            saw_act = saw_act or (t.op_type in ACTS)
        assert saw_act or consumers.get(t.output[0]), \
            f"{conv.name}: no activation and no consumer after the conv chain"
        last_relu_out = t.output[0]

    assert consumers.get(last_relu_out), \
        "last dilated-conv chain feeds the graph output directly; give the " \
        "model a trailing 1x1 projection so the flat restore has a consumer"
    flat_name = last_relu_out + "_flat"
    shp_name = flat_name + "_shape"
    g.initializer.append(numpy_helper.from_array(
        np.array([1, cout, 1, w_total], np.int64), shp_name))
    g.node.insert(list(g.node).index(consumers[last_relu_out][0]),
                  helper.make_node("Reshape", [last_relu_out, shp_name],
                                   [flat_name], name=flat_name + "_reshape"))
    added_tensors.append((flat_name, last_relu_out, flat_name + "_reshape"))
    for n in consumers[last_relu_out]:
        n.input[:] = [flat_name if i == last_relu_out else i for i in n.input]

    for n in removed + removed_pads:
        g.node.remove(n)
    del g.value_info[:]

    for new, src, node_name in added_tensors:
        quant["tensors"][new] = quant["tensors"][src]
        quant["nodes"][node_name] = {"names": ["__FE_GENERATED__"]}

    model = onnx.shape_inference.infer_shapes(model)
    onnx.checker.check_model(model)
    return model, quant


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", type=Path, required=True)
    ap.add_argument("--quant", type=Path, required=True)
    ap.add_argument("--out-prefix", type=Path, required=True)
    ap.add_argument("--width", type=int, default=W_TOTAL,
                    help="model W (480 SwiftF0, 320 RSN); every dilation divides it")
    a = ap.parse_args()
    model, quant = rewrite(onnx.load(a.onnx), json.loads(a.quant.read_text()),
                           w_total=a.width)
    a.out_prefix.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, f"{a.out_prefix}.onnx")
    Path(f"{a.out_prefix}_Q.json").write_text(json.dumps(quant, indent=1))
    print(f"-> {a.out_prefix}.onnx / _Q.json")


if __name__ == "__main__":
    main()
