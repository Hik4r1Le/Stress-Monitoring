import numpy as np
import matplotlib.pyplot as plt
import json

# Parameters (customizable)
duration = 1440
baseline = 70
sigma = 3
alpha = 0.2
dt = 10.0

t = np.arange(0, duration, dt)

# Raw HR with RSA variation
hr_raw = baseline + np.random.normal(0, 1, len(t))
hr_raw += 2 * np.sin(2 * np.pi * t / 15)  # RSA

# Noisy artifact
noise_idx = (t >= 270) & (t < 360)
hr_raw[noise_idx] += np.random.normal(0, 10, noise_idx.sum())

# Invalid segment
invalid_idx = (t >= 450) & (t < 480)
hr_raw[invalid_idx] = np.nan

# Gradual ramp
ramp_idx = (t >= 600) & (t < 990)
hr_raw[ramp_idx] += np.linspace(0, 8, ramp_idx.sum())

# Spike
spike_idx = (t >= 1080) & (t < 1140)
hr_raw[spike_idx] = baseline + 25 + np.random.normal(0, 2, spike_idx.sum())

# EMA (handle NaN)
ema = np.zeros_like(hr_raw)
ema[0] = hr_raw[0] if not np.isnan(hr_raw[0]) else baseline
for i in range(1, len(hr_raw)):
    if np.isnan(hr_raw[i]):
        ema[i] = ema[i-1]
    else:
        ema[i] = alpha * hr_raw[i] + (1 - alpha) * ema[i-1]

# Thresholds
grad_thr = baseline + 1.4 * sigma
spike_thr = baseline + 3 * sigma

# States
state = np.zeros_like(hr_raw)
state[ema > grad_thr] = 1
state[ema > spike_thr] = 2

state_raw = np.zeros_like(hr_raw)
valid_raw = ~np.isnan(hr_raw)
state_raw[valid_raw & (hr_raw > grad_thr)] = 1
state_raw[valid_raw & (hr_raw > spike_thr)] = 2

# SpO2
spo2 = 97 + np.random.normal(0, 0.3, len(t))

# Plot (added raw state)
fig, axs = plt.subplots(5, 1, figsize=(12, 12), sharex=True)
axs[0].plot(t, hr_raw, 'o', ms=3, alpha=0.5, label='Raw HR')
axs[0].set_ylabel("Raw HR (BPM)")
axs[0].set_title("Synthetic Stress Test")
axs[0].set_ylim(65, 100)

axs[1].plot(t, ema, label="EMA HR", color='blue')
axs[1].axhline(baseline, color='black', ls='--')
axs[1].axhline(grad_thr, color='orange', ls='--')
axs[1].axhline(spike_thr, color='red', ls='--')
axs[1].set_ylabel("EMA HR")
axs[1].legend()
axs[1].set_ylim(65, 100)

axs[2].plot(t, spo2, color='purple')
axs[2].set_ylabel("SpO₂ (%)")
axs[2].set_ylim(95, 100)

axs[3].plot(t, state, drawstyle='steps-mid', label='EMA State')
axs[3].set_ylabel("EMA State")

axs[4].plot(t, state_raw, drawstyle='steps-mid', color='gray', label='Raw State')
axs[4].set_ylabel("Raw State")
axs[4].set_xlabel("Time (s)")

# Shade events
for ax in axs:
    ax.axvspan(270, 360, color='gray', alpha=0.2, label='Noise' if ax == axs[0] else None)
    ax.axvspan(450, 480, color='black', alpha=0.2, label='Invalid' if ax == axs[0] else None)
    ax.axvspan(600, 990, color='orange', alpha=0.2, label='Gradual' if ax == axs[0] else None)
    ax.axvspan(1080, 1140, color='red', alpha=0.2, label='Spike' if ax == axs[0] else None)
axs[0].legend()

plt.tight_layout()
plt.show()
