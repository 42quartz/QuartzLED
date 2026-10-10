Minimal subset of [OpenVoice](https://github.com/myshell-ai/OpenVoice) (MIT, see `LICENSE`) vendored at
commit `74a1d147b17a8c3092dd5430504bd83ef6c7eb23`: only the tone color converter model code
(`attentions.py`, `commons.py`, `mel_processing.py`, `models.py`, `modules.py`, `transforms.py`, `utils.py`),
unmodified. `converter.py` is ours: it replaces `api.py` without the TTS text front-end, Whisper/VAD
helpers and the runtime-downloaded watermark model, so it installs on current Python/numpy/librosa.
