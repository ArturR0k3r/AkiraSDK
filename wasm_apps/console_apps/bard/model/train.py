# Copyright (c) 2026 PenEngineering S.R.L
# SPDX-License-Identifier: Apache-2.0
"""Train Pocket Bard's char-level GRU on tinyshakespeare (needs PyTorch).

usage: train.py H E steps out.pt [threads]     e.g. train.py 96 48 8000 bard.pt
Then: export.py bard.pt ../bard_model.h
"""
import sys, time, math, torch, torch.nn as nn
torch.manual_seed(1337)
H, E, STEPS, OUT = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
torch.set_num_threads(int(sys.argv[5]) if len(sys.argv) > 5 else 8)
import os, urllib.request
CORPUS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tinyshakespeare.txt')
URL = 'https://raw.githubusercontent.com/karpathy/char-rnn/master/data/tinyshakespeare/input.txt'
if not os.path.exists(CORPUS):
    urllib.request.urlretrieve(URL, CORPUS)  # Shakespeare's plays, public domain
text = open(CORPUS).read()
chars = sorted(set(text)); V = len(chars); stoi = {c: i for i, c in enumerate(chars)}
data = torch.tensor([stoi[c] for c in text], dtype=torch.long)
n = int(0.9 * len(data)); train, val = data[:n], data[n:]

class Bard(nn.Module):
    def __init__(s):
        super().__init__()
        s.emb = nn.Embedding(V, E); s.gru = nn.GRU(E, H, batch_first=True); s.out = nn.Linear(H, V)
    def forward(s, x, h=None):
        y, h = s.gru(s.emb(x), h)
        return s.out(y), h

m = Bard()
print(f'V={V} H={H} E={E} params={sum(p.numel() for p in m.parameters())}', flush=True)
opt = torch.optim.AdamW(m.parameters(), lr=3e-3, weight_decay=0.01)
T, B = 128, 64
def batch(src):
    ix = torch.randint(len(src) - T - 1, (B,))
    return torch.stack([src[i:i + T] for i in ix]), torch.stack([src[i + 1:i + T + 1] for i in ix])
@torch.no_grad()
def evaluate():
    m.eval(); tot = 0
    for _ in range(20):
        x, y = batch(val); lg, _ = m(x); tot += nn.functional.cross_entropy(lg.reshape(-1, V), y.reshape(-1)).item()
    m.train(); return tot / 20
t0 = time.time()
for step in range(STEPS + 1):
    lr = 3e-3 * 0.5 * (1 + math.cos(math.pi * step / STEPS)) + 1e-4
    for g in opt.param_groups: g['lr'] = lr
    x, y = batch(train)
    lg, _ = m(x)
    loss = nn.functional.cross_entropy(lg.reshape(-1, V), y.reshape(-1))
    opt.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(m.parameters(), 1.0); opt.step()
    if step % 500 == 0:
        print(f'step {step} train {loss.item():.3f} val {evaluate():.3f} lr {lr:.5f} {time.time() - t0:.0f}s', flush=True)
torch.save({'state': m.state_dict(), 'chars': chars, 'H': H, 'E': E}, OUT)
# sample
m.eval(); h = None; x = torch.tensor([[stoi['\n']]]); outs = []
with torch.no_grad():
    for _ in range(400):
        lg, h = m(x, h); p = torch.softmax(lg[0, -1] / 0.8, 0); c = torch.multinomial(p, 1).item(); outs.append(chars[c]); x = torch.tensor([[c]])
print(''.join(outs))
