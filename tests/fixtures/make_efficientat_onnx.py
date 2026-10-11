#!/usr/bin/env python3
"""Generate a tiny deterministic EfficientAT-compatible ONNX fixture.

It does not use actual EfficientAT weights. Its output is 527 fixed logits,
and it verifies ONNX IO handling, score conversion and label mapping.
"""
from pathlib import Path
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
logits = np.zeros((1, 527), dtype=np.float32)
logits[0, 0] = 40.0
logits[0, 1] = 60.0
logits[0, 526] = 80.0
const = numpy_helper.from_array(logits, name="constant_logits")
node = helper.make_node("Constant", [], ["logits"], value=const)
graph = helper.make_graph(
    [node], "efficientat_synthetic",
    [helper.make_tensor_value_info("logmel", TensorProto.FLOAT, [1, 1, 128, "frames"])],
    [helper.make_tensor_value_info("logits", TensorProto.FLOAT, [1, 527])],
)
model = helper.make_model(graph, producer_name="libaudition-test",
                          opset_imports=[helper.make_operatorsetid("", 13)])
model.ir_version = min(model.ir_version, 9)
onnx.checker.check_model(model)
onnx.save(model, out / "efficientat_synthetic.onnx")
(out / "labels.txt").write_text(
    "".join(f"class_{i}\n" for i in range(527)), encoding="utf-8")
