# Pocket Bard model

`bard_model.h` (one directory up) is generated; these scripts reproduce it.

```bash
pip install torch numpy          # CPU build is enough
python3 train.py 96 48 8000 bard.pt    # hidden 96, embedding 48, 8000 steps (~40 min on 8 cores)
python3 export.py bard.pt ../bard_model.h
```

`train.py` downloads the tinyshakespeare corpus (Shakespeare's plays, public
domain) on first run. `export.py` folds the embedding into a per-character
input table, quantizes all weights to int8 with a float scale per row, and
reports the held-out loss of the float model and of the exact int8 arithmetic
the app uses. It also writes `<out>.golden.npy`, the logits for a fixed prompt,
for checking the C implementation.
