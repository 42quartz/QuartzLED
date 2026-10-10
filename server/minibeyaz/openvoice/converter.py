"""Tone color conversion only (OpenVoice V2 converter), on in-memory audio."""
import librosa
import numpy as np
import torch

from openvoice import utils
from openvoice.mel_processing import spectrogram_torch
from openvoice.models import SynthesizerTrn


class ToneColorConverter:
    def __init__(self, config_path: str, ckpt_path: str, device: str = "cpu"):
        self.hps = utils.get_hparams_from_file(config_path)
        self.device = device
        self.model = SynthesizerTrn(
            len(getattr(self.hps, "symbols", [])),
            self.hps.data.filter_length // 2 + 1,
            n_speakers=self.hps.data.n_speakers,
            **self.hps.model,
        ).to(device)
        self.model.eval()
        state = torch.load(ckpt_path, map_location=torch.device(device))
        self.model.load_state_dict(state["model"], strict=False)
        self.sample_rate = self.hps.data.sampling_rate

    def _spec(self, audio: np.ndarray) -> torch.Tensor:
        y = torch.FloatTensor(audio).to(self.device).unsqueeze(0)
        d = self.hps.data
        return spectrogram_torch(y, d.filter_length, d.sampling_rate, d.hop_length, d.win_length, center=False)

    def embedding(self, clips: list[tuple[np.ndarray, int]]) -> torch.Tensor:
        """Speaker (tone color) embedding averaged over (audio, sample_rate) clips."""
        gs = []
        for audio, sr in clips:
            audio = librosa.resample(audio.astype(np.float32), orig_sr=sr, target_sr=self.sample_rate)
            with torch.no_grad():
                gs.append(self.model.ref_enc(self._spec(audio).transpose(1, 2)).unsqueeze(-1))
        return torch.stack(gs).mean(0)

    def convert(self, audio: np.ndarray, sr: int, src_se: torch.Tensor, tgt_se: torch.Tensor,
                tau: float = 0.3) -> np.ndarray:
        """Re-voice `audio` from src_se to tgt_se; returns float32 at `sr`."""
        x = librosa.resample(audio.astype(np.float32), orig_sr=sr, target_sr=self.sample_rate)
        with torch.no_grad():
            spec = self._spec(x)
            lengths = torch.LongTensor([spec.size(-1)]).to(self.device)
            out = self.model.voice_conversion(spec, lengths, sid_src=src_se, sid_tgt=tgt_se, tau=tau)[0][0, 0]
        out = out.cpu().float().numpy()
        return librosa.resample(out, orig_sr=self.sample_rate, target_sr=sr)
