#!/usr/bin/env python3
"""Genera la música y los efectos del launcher (composición original, libre de derechos).

Todo se sintetiza aquí: koto (cuerdas pulsadas aditivas), shakuhachi (flauta con soplido),
taiko, campana rin, bordón, viento y crepitar de fuego, con reverberación por convolución.
Escala japonesa in-sen / miyako-bushi sobre Re.

Requiere: numpy y ffmpeg (para codificar a OGG Vorbis).
Uso: python3 tools/make_audio.py
"""
import os
import subprocess
import tempfile
import wave

import numpy as np

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "audio")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1600)


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12.0)


def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def band_noise(n, lo, hi, seed=None):
    """Ruido blanco filtrado en frecuencia (FFT)."""
    r = np.random.default_rng(seed) if seed is not None else rng
    x = r.standard_normal(n)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1 / SR)
    X *= ((f >= lo) & (f <= hi)).astype(float) * 1.0
    y = np.fft.irfft(X, n)
    return y / (np.max(np.abs(y)) + 1e-9)


def lowpass(x, cutoff):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    X *= 1.0 / (1.0 + (f / cutoff) ** 4)
    return np.fft.irfft(X, len(x))


def pan(mono, p):
    """p en [-1, 1] -> estéreo con ley de potencia constante."""
    a = (p + 1) * np.pi / 4
    return np.stack([mono * np.cos(a), mono * np.sin(a)], axis=1)


# --- Instrumentos -----------------------------------------------------------------------------

def koto(freq, dur=3.5, vel=1.0, bright=1.0):
    t = t_axis(dur)
    out = np.zeros_like(t)
    # ligera bajada de afinación al pulsar (la cuerda se asienta)
    bend = 1.0 + 0.012 * np.exp(-t * 30)
    B = 0.00035  # inarmonicidad
    for n in range(1, 14):
        fn = n * freq * np.sqrt(1 + B * n * n)
        if fn > SR / 2.2:
            break
        amp = abs(np.sin(np.pi * n * 0.23)) / n ** (1.25 - 0.25 * bright)
        decay = 1.1 + 0.75 * n
        out += amp * np.exp(-t * decay) * np.sin(2 * np.pi * fn * np.cumsum(bend) / SR)
    # ataque de la púa
    click = band_noise(len(t), 1500, 9000) * np.exp(-t * 400) * 0.25
    out = (out + click) * vel
    out *= np.minimum(1.0, t / 0.002)
    return out


def shakuhachi(freq, dur, vel=1.0):
    t = t_axis(dur)
    # entrada con "meri": empieza medio tono abajo y sube
    slide = 2 ** (-0.5 / 12 * np.exp(-t * 9))
    vib = 1 + 0.006 * np.sin(2 * np.pi * 5.2 * t) * np.clip((t - 0.4) / 0.6, 0, 1)
    f = freq * slide * vib
    ph = 2 * np.pi * np.cumsum(f) / SR
    tone = np.sin(ph) + 0.22 * np.sin(2 * ph) + 0.1 * np.sin(3 * ph) + 0.04 * np.sin(4 * ph)
    breath = band_noise(len(t), freq * 0.8, freq * 4.0) * 0.28
    env = np.clip(t / 0.28, 0, 1) * np.clip((dur - t) / 0.45, 0, 1)
    # soplido más intenso al comienzo de la nota
    breath *= 0.6 + 0.9 * np.exp(-t * 3)
    return (tone * 0.75 + breath) * env * vel


def taiko(vel=1.0, dur=1.6, low=55.0):
    t = t_axis(dur)
    f = low + 75 * np.exp(-t * 16)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 4.5)
    skin = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 14) * 0.35
    thump = lowpass(band_noise(len(t), 20, 2000), 900) * np.exp(-t * 35) * 0.6
    return (body + skin + thump) * vel


def rin(freq=880.0, dur=4.0, vel=1.0):
    """Campana de templo: parciales inarmónicos con batidos."""
    t = t_axis(dur)
    out = np.zeros_like(t)
    for ratio, amp, dec in [(1.0, 1.0, 0.9), (2.76, 0.55, 1.6), (5.40, 0.3, 2.6), (8.93, 0.15, 4.0), (1.003, 0.6, 0.8)]:
        out += amp * np.sin(2 * np.pi * freq * ratio * t) * np.exp(-t * dec)
    out *= np.minimum(1.0, t / 0.003)
    return out * vel * 0.5


def hyoshigi(vel=1.0, pitch=1.0):
    """Tablillas de madera: golpe seco y resonante."""
    t = t_axis(0.35)
    out = (np.sin(2 * np.pi * 820 * pitch * t) * np.exp(-t * 38)
           + 0.6 * np.sin(2 * np.pi * 1390 * pitch * t) * np.exp(-t * 55)
           + 0.3 * band_noise(len(t), 2000, 8000) * np.exp(-t * 200))
    return out * vel


def katana(dur=1.1, vel=1.0):
    """Desenvaine: roce de metal ascendente y resonancia brillante."""
    t = t_axis(dur)
    scrape = band_noise(len(t), 3000, 12000) * np.exp(-((t - 0.08) / 0.07) ** 2) * 0.5
    ring = np.zeros_like(t)
    for f0, a, d in [(2210, 1.0, 3.2), (3470, 0.7, 4.0), (5230, 0.5, 5.5), (7880, 0.3, 7.0)]:
        f = f0 * (1 + 0.01 * np.clip(t / 0.15, 0, 1))
        ring += a * np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * d)
    ring *= np.clip((t - 0.05) / 0.03, 0, 1)
    return (scrape + ring * 0.35) * vel


# --- Mezcla -----------------------------------------------------------------------------------

def reverb(stereo, seconds=2.6, wet=0.32, seed=5):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    r = np.random.default_rng(seed)
    out = np.zeros((stereo.shape[0] + n, 2))
    for ch in range(2):
        ir = r.standard_normal(n) * np.exp(-t * 2.6)
        ir = lowpass(ir, 5000)
        ir /= np.sqrt(np.sum(ir ** 2))
        size = 1 << int(np.ceil(np.log2(stereo.shape[0] + n)))
        y = np.fft.irfft(np.fft.rfft(stereo[:, ch], size) * np.fft.rfft(ir, size), size)[: stereo.shape[0] + n]
        out[:, ch] = y * wet
    out[: stereo.shape[0]] += stereo * (1 - wet * 0.5)
    return out


def add(buf, sig, start_s, p=0.0):
    i = int(start_s * SR)
    s = pan(sig, p) if sig.ndim == 1 else sig
    end = min(buf.shape[0], i + s.shape[0])
    if i < end:
        buf[i:end] += s[: end - i]


def finish(stereo, peak=0.89):
    stereo = np.tanh(stereo / (np.max(np.abs(stereo)) + 1e-9) * 1.2)
    return stereo / np.max(np.abs(stereo)) * peak


def write_ogg(stereo, name, quality=5):
    pcm = (np.clip(stereo, -1, 1) * 32767).astype(np.int16)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        path = tmp.name
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    out = os.path.join(OUT, name)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", path, "-c:a", "libvorbis", "-q:a", str(quality), out],
                   check=True)
    os.remove(path)
    print("  ", name, f"{os.path.getsize(out) / 1024:.0f} KB")


# --- Música -----------------------------------------------------------------------------------

def music():
    bpm = 62
    beat = 60.0 / bpm
    bar = beat * 4
    bars = 24
    length = bars * bar
    buf = np.zeros((int((length + 4) * SR), 2))

    D, Eb, G, A, Bb = 62, 63, 67, 69, 70  # in-sen sobre Re (Re4 = 62)

    # Bordón grave (Re2 + La2) y viento: base continua.
    t = t_axis(length + 4)
    drone = (np.sin(2 * np.pi * midi(38) * t) * 0.5 + np.sin(2 * np.pi * midi(45) * t + 1.0) * 0.3
             + np.sin(2 * np.pi * midi(50) * t * 1.002) * 0.12)
    drone *= 0.55 + 0.45 * np.sin(2 * np.pi * t / bar / 4) ** 2
    add(buf, drone * 0.10, 0, 0)
    wind = band_noise(len(t), 150, 900, seed=11) * (0.5 + 0.5 * np.sin(2 * np.pi * t / 23.0) ** 2)
    add(buf, np.stack([wind, np.roll(wind, 9000)], axis=1) * 0.035, 0)

    # Crepitar de fuego: chasquidos dispersos.
    n_clicks = int(length * 7)
    for _ in range(n_clicks):
        s = rng.uniform(0, length)
        c = band_noise(int(0.03 * SR), 1500, 9000) * np.exp(-np.arange(int(0.03 * SR)) / SR * 160)
        add(buf, c * rng.uniform(0.01, 0.06), s, rng.uniform(-0.8, 0.8))

    # Koto: arpegios por compás, más densos a medida que avanza la pieza.
    chords = [
        [D, A, D + 12, Eb + 12],
        [G - 12, D, G, A],
        [Bb - 12, D, A, Bb],
        [A - 12, Eb, A, D + 12],
    ]
    patterns = [
        [(0, 0), (1.5, 1), (3, 2)],
        [(0, 0), (1, 1), (2, 2), (3, 3)],
        [(0, 0), (0.5, 1), (1.5, 2), (2.5, 3), (3, 2), (3.5, 1)],
    ]
    for b in range(bars):
        chord = chords[b % 4]
        section = 0 if b < 8 else (1 if b < 16 else 2)
        if b >= 22:
            section = 0  # se aclara para volver al inicio del bucle
        for pos, idx in patterns[section]:
            note = chord[idx]
            vel = rng.uniform(0.55, 0.8) * (1.0 if pos == 0 else 0.75)
            add(buf, koto(midi(note), 3.2, vel * 0.32), b * bar + pos * beat + rng.uniform(0, 0.015),
                -0.35 + 0.18 * idx)
        # nota grave del koto cada dos compases
        if b % 2 == 0:
            add(buf, koto(midi(chord[0] - 12), 4.0, 0.3, bright=0.6), b * bar, -0.1)

    # Shakuhachi: melodía en las secciones B y C.
    melody_b = [(0, 4, A), (4, 2, G), (6, 2, Bb), (8, 6, A), (14, 2, G),
                (16, 4, D + 12), (20, 2, Eb + 12), (22, 2, D + 12), (24, 6, A), (30, 2, Bb)]
    melody_c = [(0, 3, D + 12), (3, 1, Eb + 12), (4, 4, D + 12), (8, 2, Bb), (10, 2, A), (12, 4, G),
                (16, 4, A), (20, 2, Bb), (22, 2, A), (24, 8, D + 12)]
    for start_bar, mel in [(8, melody_b), (16, melody_c)]:
        for st, ln, note in mel:
            add(buf, shakuhachi(midi(note), ln * beat + 0.3, 0.17), start_bar * bar + st * beat, 0.25)

    # Taiko: golpes graves en la sección B, patrón más marcado en la C.
    for b in range(8, 22):
        if b < 16:
            if b % 2 == 0:
                add(buf, taiko(0.55), b * bar, -0.05)
        else:
            add(buf, taiko(0.6), b * bar, -0.05)
            add(buf, taiko(0.3, low=70), b * bar + 2.5 * beat, 0.1)
            if b % 4 == 3:
                for k, v in enumerate([0.25, 0.32, 0.42]):
                    add(buf, taiko(v, 0.8, low=80), b * bar + (3 + k / 3) * beat, 0.15)
    # campana al comienzo de cada sección
    for b in (0, 8, 16):
        add(buf, rin(midi(D + 24), 5.0, 0.09), b * bar + 0.01, 0.4)

    wet = reverb(buf, 3.0, 0.34)
    # Bucle sin cortes: la cola que sobrepasa el final se suma al principio.
    n = int(length * SR)
    loop = wet[:n].copy()
    tail = wet[n:]
    loop[: tail.shape[0]] += tail[: n]
    write_ogg(finish(loop, 0.8), "music.ogg", 5)


# --- Efectos ----------------------------------------------------------------------------------

def mix_in(buf, sig, start_s, gain=1.0):
    """Suma una señal mono en `buf` a partir de `start_s`, recortando lo que sobre."""
    i = int(start_s * SR)
    n = min(len(sig), len(buf) - i)
    if n > 0:
        buf[i:i + n] += sig[:n] * gain


def sfx():
    def render(sig, name, dur=None, rev=0.25):
        st = pan(sig, 0) if sig.ndim == 1 else sig
        out = reverb(st, 1.4, rev)
        if dur:
            out = out[: int(dur * SR)]
            out[-int(0.05 * SR):] *= np.linspace(1, 0, int(0.05 * SR))[:, None]
        write_ogg(finish(out, 0.85), name, 6)

    # mover: pulsación grave y apagada de koto (sin agudos que molesten al repetirse)
    move = koto(midi(62), 0.6, 1.0, bright=0.35)
    t = t_axis(0.6)
    move *= np.exp(-t * 5.0)                      # más corta
    move = lowpass(move, 1800)                    # sin brillo metálico
    move *= np.minimum(1.0, t / 0.004)            # ataque suave, sin chasquido
    render(move, "move.ogg", 0.5, 0.12)

    # elegir: taiko + desenvaine de katana
    sel = np.zeros(int(1.6 * SR))
    mix_in(sel, taiko(0.9, 1.2, low=62), 0, 0.8)
    mix_in(sel, katana(1.2), 0.02, 0.9)
    render(sel, "select.ogg", 1.4, 0.22)

    # cancelar: dos golpes de hyōshigi descendentes
    canc = np.zeros(int(0.7 * SR))
    mix_in(canc, hyoshigi(0.9, 1.0), 0)
    mix_in(canc, hyoshigi(0.7, 0.8), 0.11)
    render(canc, "cancel.ogg", 0.65, 0.2)

    # empezar partida: doble taiko grave + koto + katana
    start = np.zeros(int(3.2 * SR))
    for tt, v in [(0.0, 1.0), (0.38, 0.85)]:
        mix_in(start, taiko(v, 2.2, low=48), tt)
    mix_in(start, koto(midi(50), 3.0, 0.6, bright=0.8), 0.38)
    mix_in(start, katana(1.3), 0.4, 0.7)
    render(start, "start.ogg", 3.0, 0.3)

    # cerrar: taiko muy grave, koto descendente y campana lejana
    close = np.zeros(int(2.6 * SR))
    mix_in(close, taiko(1.0, 2.4, low=40), 0)
    for i, n in enumerate([62, 57, 50]):
        mix_in(close, koto(midi(n), 2.0, 0.42 - i * 0.08, bright=0.5), 0.05 + i * 0.16)
    mix_in(close, rin(midi(74), 2.4, 0.5), 0.3)
    render(close, "close.ogg", 2.5, 0.35)

    # logro desbloqueado: campana rin + arpegio de koto ascendente
    ach = np.zeros(int(3.5 * SR))
    mix_in(ach, rin(midi(86), 3.4, 1.4), 0)
    for i, n in enumerate([74, 79, 81, 86]):
        mix_in(ach, koto(midi(n), 2.0, 0.45, bright=1.2), 0.12 + i * 0.09)
    render(ach, "achievement.ogg", 3.3, 0.35)


if __name__ == "__main__":
    print("Música:")
    music()
    print("Efectos:")
    sfx()
