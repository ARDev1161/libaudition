#!/usr/bin/env python3
"""Trace real EfficientAT waveform -> Torch mel -> PyTorch and ONNX predictions.

Unlike random-tensor export parity, this reproduces actual 32 kHz WAV frontend
numerics and compares all 527 raw logits. Failures localize the problem to
model export vs input/frontend. Requires original EfficientAT source on PYTHONPATH.
"""
import argparse
import csv
from pathlib import Path
import wave
import subprocess
import tempfile

import numpy as np
import onnxruntime as ort
import torch
import torchaudio

from models.mn.model import get_model as get_mobilenet
from models.dymn.model import get_model as get_dymn
from helpers.utils import NAME_TO_WIDTH

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--wav",type=Path,required=True)
    p.add_argument("--model",choices=["mn10_as","dymn10_as"],required=True)
    p.add_argument("--onnx",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--cpp-benchmark",type=Path)
    p.add_argument("--cpp-labels",type=Path)
    p.add_argument("--cpp-mel-dump",type=Path)
    a=p.parse_args()
    torch.set_num_threads(1)
    with wave.open(str(a.wav),"rb") as f:
        if (f.getnchannels(),f.getsampwidth(),f.getframerate())!=(1,2,32000):
            raise ValueError("Expected 32k mono PCM16")
        needed = 160000 if a.model == "mn10_as" else 32000
        pcm=np.frombuffer(f.readframes(needed),dtype="<i2")
    if len(pcm)!=needed:
        raise ValueError("Expected at least one second")
    x=torch.from_numpy(pcm.astype(np.float32)/32768.0).reshape(1,1,-1)
    with torch.no_grad():
        x=torch.nn.functional.conv1d(x,torch.tensor([[[-0.97,1.0]]]))
        spec=torch.stft(x.squeeze(1),n_fft=1024,hop_length=320,win_length=800,
                        center=True,normalized=False,
                        window=torch.hann_window(800,periodic=False),
                        return_complex=True).abs().square()
        banks,_=torchaudio.compliance.kaldi.get_mel_banks(
            128,1024,32000,0.0,15000.0,vtln_low=100.0,
            vtln_high=-500.0,vtln_warp_factor=1.0)
        banks=torch.nn.functional.pad(banks,(0,1))
        mel=((torch.log(torch.matmul(banks,spec)+1e-5)+4.5)/5).unsqueeze(1)
        assert tuple(mel.shape)==(1,1,128,needed//320),tuple(mel.shape)
        model=(get_dymn(width_mult=NAME_TO_WIDTH(a.model),pretrained_name=a.model)
               if a.model.startswith("dymn") else
               get_mobilenet(width_mult=NAME_TO_WIDTH(a.model),pretrained_name=a.model))
        model.eval()
        reference=model(mel)[0].detach().numpy()
    if a.cpp_mel_dump:
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/"cpp-mel.f32"
            canonical=Path(folder)/"input.wav"
            with wave.open(str(canonical),"wb") as dst:
                dst.setnchannels(1)
                dst.setsampwidth(2)
                dst.setframerate(32000)
                dst.writeframes(pcm.tobytes())
            subprocess.run([str(a.cpp_mel_dump.resolve()),str(output),str(canonical)],
                           capture_output=True,text=True,check=True)
            cpp_mel=np.fromfile(output,dtype="<f4").reshape(1,1,128,needed//320)
            mel_diff=np.abs(cpp_mel-mel.numpy())
            print("C++/Torch mel max_abs:",float(mel_diff.max()))
            if not np.isfinite(mel_diff).all() or mel_diff.max()>0.003:
                raise SystemExit(f"FAIL: C++ vs Torch real-WAV mel {mel_diff.max():.6f}")
    session=ort.InferenceSession(str(a.onnx),providers=["CPUExecutionProvider"])
    logits=session.run(None,{session.get_inputs()[0].name:mel.numpy()})[0]
    delta=np.abs(logits-reference)
    labels=[]
    with open("metadata/class_labels_indices.csv",newline="",encoding="utf-8") as f:
        labels=[r["display_name"] for r in csv.DictReader(f)]
    def top(values):
        scores=1/(1+np.exp(-np.clip(values.reshape(-1),-80,80)))
        return [{"label":labels[i],"probability":float(scores[i]),"logit":float(values.reshape(-1)[i])}
                for i in np.argsort(-values.reshape(-1),kind='stable')[:5]]
    import json
    report={
        "model":a.model,"wav":str(a.wav),
        "max_abs_onnx_torch_logit_error":float(delta.max()),
        "mean_abs_onnx_torch_logit_error":float(delta.mean()),
        "mel_min":float(mel.min()),"mel_max":float(mel.max()),
        "torch_top5":top(reference),"onnx_top5":top(logits),
    }
    if a.cpp_benchmark:
        if not a.cpp_labels:
            p.error("--cpp-labels is required with --cpp-benchmark")
        result=subprocess.run([str(a.cpp_benchmark.resolve()),str(a.onnx.resolve()),
                               str(a.cpp_labels.resolve()),str(a.wav.resolve())],
                              capture_output=True,text=True,check=True)
        lines=result.stdout.splitlines()
        if len(lines)!=6:
            raise ValueError(f"Invalid C++ benchmark output: {result.stdout}")
        cpp=[line.rsplit(" ",1) for line in lines[1:]]
        report["cpp_top5"]=[{"label":label,"probability":float(score)} for label,score in cpp]
        # Saturated sigmoid scores create ties, so compare probabilities by
        # class name rather than requiring identical top-five ordering.
        score_map={label:float(1/(1+np.exp(-np.clip(logit,-80,80))))
                   for label,logit in zip(labels,logits.reshape(-1))}
        for actual in report["cpp_top5"]:
            expected=score_map[actual["label"]]
            if abs(actual["probability"]-expected)>0.002:
                raise SystemExit(f"FAIL: C++/Python WAV probability parity: {actual} vs {expected}")
    a.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2))
    if not np.isfinite(delta).all() or delta.max()>0.002:
        raise SystemExit("FAIL: real WAV ONNX/PyTorch logits differ")

if __name__=="__main__":
    main()
