"""Compare the saved Spot Cone screenshot with its independent rough-card model.

Run here with Python, NumPy, Pillow and Matplotlib. This analyzes the existing
image; it does not launch the app or acquire another capture. Geometry comes from
the saved 1920x1080 layout: 640px controls, 6.4m orthographic scene width.
"""

import json
import math
from pathlib import Path

import matplotlib
import numpy as np
from PIL import Image

matplotlib.use("Agg")
import matplotlib.pyplot as plt

root = Path(__file__).resolve().parent
image = np.asarray(Image.open(root / "screens/spot-cone.png").convert("RGB"))
y, x = np.indices(image.shape[:2])
radius = np.hypot(x + 0.5 - 1280, y + 0.5 - 540) / 200
cosine = 3 / np.sqrt(9 + radius**2)
inner, outer = math.cos(math.pi / 12), math.cos(math.pi / 6)
angular = np.clip((cosine - outer) / (inner - outer), 0, 1) ** 2
intensity = 1000 / (2 * math.pi * (1 - inner + (inner - outer) / 3))

# Independent roughness=1, normal-view GGX moments from ReferenceScene.h.
energy, bias = 1 - math.log(2), 3.3614294725518486e-5
weight = 1 + 0.04 * (1 / energy - 1)
transmission = 1 - weight * (0.04 * energy + 0.96 * bias)
half_cosine = np.sqrt((1 + cosine) / 2)
brdf = (0.04 + 0.96 * (1 - half_cosine) ** 5) * weight / (
    2 * math.pi * (1 + cosine)
) + 0.18 * transmission / math.pi
hdr = (
    intensity * angular * (1 - ((9 + radius**2) / 400) ** 2) ** 2
    / (9 + radius**2) * cosine * brdf
)
expected = np.clip(hdr * 2 ** (-5.449541159604736), 0, 1) ** (1 / 2.2) * 255
mask = radius <= 2
error = np.abs(image.astype(float) - expected[:, :, None])[mask]
result = {
    "pixel_count": int(mask.sum()),
    "maximum_ideal_difference_8bit_codes": float(error.max()),
    "mean_ideal_difference_8bit_codes": float(error.mean()),
    "inner_radius_m": 3 * math.tan(math.pi / 12),
    "outer_radius_m": 3 * math.tan(math.pi / 6),
    "note": "Screenshot comparison includes material packing, dithering, display quantization and pixel registration. Native HDR tests carry the numerical acceptance budget.",
}
(root / "spot-capture-comparison.json").write_text(
    json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n"
)
sample = np.arange(1280, 1680)
fig, ax = plt.subplots(figsize=(9, 4.5), constrained_layout=True)
ax.plot(radius[540, sample], expected[540, sample], label="Independent model", lw=2)
ax.plot(radius[540, sample], image[540, sample, 0], label="Saved screenshot", lw=1)
for position, label in [(result["inner_radius_m"], "Inner 15°"), (result["outer_radius_m"], "Outer 30°")]:
    ax.axvline(position, color="gray", linestyle="--", lw=1)
    ax.text(position + 0.025, 170, label, fontsize=9)
ax.set(xlabel="Distance from footprint center (m)", ylabel="Display value (0–255)",
       title="Spot Cone: captured radial profile vs independent model", ylim=(0, 190))
ax.legend(loc="lower left")
ax.grid(alpha=0.2)
fig.savefig(root / "spot-capture-profile.png", dpi=150)
print(json.dumps(result, indent=2))
