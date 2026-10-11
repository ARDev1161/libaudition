#!/usr/bin/env python3
"""Export REAL upstream EfficientAT AudioSet weights as raw-logit ONNX.

Run from the upstream EfficientAT repository root after installing its dependencies:
  python3 /path/to/libaudition/tools/export_efficientat.py --model mn10_as --output /tmp/eat

Only the official model factory is used; it fetches the checkpoint from the
upstream release. MN10 defaults to [1,1,128,500] and DyMN10 to [1,1,128,100] float32 log-mel.
This script verifies output parity using CPU ONNX Runtime.
"""
import argparse
import csv
import hashlib
import json
import pathlib

import numpy as np
import onnx
import onnxruntime as ort
import torch

from models.mn.model import get_model as get_mobilenet
from models.dymn.model import get_model as get_dymn
from helpers.utils import NAME_TO_WIDTH

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", choices=["mn10_as", "dymn10_as"], required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--frames", type=int, default=None, help="mel frames, default 500 for MN10 and 100 for DyMN10")
    args = parser.parse_args()
    frames = args.frames if args.frames is not None else (500 if args.model == "mn10_as" else 100)
    if frames < 10 or frames > 2000:
        parser.error("--frames must be 10..2000")
    torch.set_num_threads(1)
    args.output.mkdir(parents=True, exist_ok=True)
    torch.manual_seed(19)
    model = (get_dymn(width_mult=NAME_TO_WIDTH(args.model), pretrained_name=args.model)
             if args.model.startswith("dymn") else
             get_mobilenet(width_mult=NAME_TO_WIDTH(args.model),
                           pretrained_name=args.model))
    model.eval().cpu()

    class RawLogits(torch.nn.Module):
        def __init__(self, inner):
            super().__init__()
            self.inner = inner
        def forward(self, x):
            prediction, _ = self.inner(x)
            return prediction

    wrapped = RawLogits(model).eval()
    x = torch.randn(1, 1, 128, frames)
    with torch.no_grad():
        reference = wrapped(x).detach().numpy()
    assert reference.shape == (1, 527), reference.shape
    path = args.output / (args.model + ".onnx")
    torch.onnx.export(wrapped, (x,), str(path), export_params=True,
                      opset_version=17, do_constant_folding=True,
                      input_names=["logmel"], output_names=["logits"],
                      dynamic_axes=None, dynamo=False)
    onnx.checker.check_model(onnx.load(str(path)))
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    predicted = session.run(["logits"], {"logmel": x.numpy()})[0]
    max_err = float(np.max(np.abs(reference - predicted)))
    mean_err = float(np.mean(np.abs(reference - predicted)))
    assert max_err < 2e-3, f"ONNX parity failed max error {max_err}"
    with open("metadata/class_labels_indices.csv", newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    labels = [row["display_name"] for row in rows]
    assert len(labels) == 527, len(labels)
    labels_path = args.output / (args.model + "_labels.txt")
    labels_path.write_text("".join(name + "\n" for name in labels), encoding="utf-8")
    report = {
        "model": args.model,
        "onnx_file": path.name,
        "onnx_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "labels_sha256": hashlib.sha256(labels_path.read_bytes()).hexdigest(),
        "input_shape": [1, 1, 128, frames],
        "output_shape": [1, 527],
        "max_abs_logit_error": max_err,
        "mean_abs_logit_error": mean_err,
    }
    (args.output / (args.model + "_manifest.json")).write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
