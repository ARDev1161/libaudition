#!/usr/bin/env python3
"""Compare native 1s and 5s EfficientAT inference on original ESC-10 audio.

Uses the official upstream PyTorch model and AugmentMelSTFT, rather than
fixed-100-frame ONNX export, to isolate the window-duration hypothesis.
"""
import argparse
import csv
import json
from pathlib import Path
import torch
import numpy as np
import wave
from models.mn.model import get_model as get_mobilenet
from models.dymn.model import get_model as get_dymn
from models.preprocess import AugmentMelSTFT
from helpers.utils import NAME_TO_WIDTH

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--model",choices=["mn10_as","dymn10_as"],required=True)
    p.add_argument("--input",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    args=p.parse_args()
    torch.set_num_threads(1)
    model=(get_dymn(width_mult=NAME_TO_WIDTH(args.model),pretrained_name=args.model)
           if args.model.startswith("dymn") else
           get_mobilenet(width_mult=NAME_TO_WIDTH(args.model),pretrained_name=args.model))
    model.eval()
    frontend=AugmentMelSTFT(n_mels=128,sr=32000,win_length=800,hopsize=320,
                            freqm=0,timem=0).eval()
    with wave.open(str(args.input), "rb") as f:
        if (f.getnchannels(), f.getsampwidth(), f.getframerate()) != (1, 2, 32000):
            raise ValueError("Expected 32 kHz mono PCM16 input")
        pcm = np.frombuffer(f.readframes(f.getnframes()), dtype="<i2")
    if len(pcm) < 160000:
        raise ValueError("Need at least 5 seconds of source audio")
    waveform = torch.from_numpy(pcm.astype(np.float32) / 32768.0).reshape(1, -1)
    labels_path=Path("metadata/class_labels_indices.csv")
    with labels_path.open(newline="",encoding="utf-8") as f:
        labels=[row["display_name"] for row in csv.DictReader(f)]
    report={"model":args.model,"file":args.input.name,"windows":[]}
    with torch.no_grad():
        for seconds in (1,2,3,5):
            signal=waveform[:,:seconds*32000]
            mel=frontend(signal)
            prediction,_=model(mel.unsqueeze(0))
            logits=prediction.reshape(-1)
            indices=torch.argsort(logits,descending=True,stable=True)[:10].tolist()
            report["windows"].append({
                "seconds":seconds,"mel_shape":list(mel.shape),
                "logit_min":float(logits.min()),"logit_max":float(logits.max()),
                "top10":[{"label":labels[i],"logit":float(logits[i]),
                          "score":float(torch.sigmoid(logits[i]))} for i in indices]})
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2))
if __name__=="__main__":
    main()
