FROM python:3.13-slim
RUN apt-get update -q && apt-get install -y -q --no-install-recommends pulseaudio-utils \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY minibeyaz.py speech_tr.py survey.html ./
CMD ["python", "-u", "minibeyaz.py"]
