# Joystick Control Modes: Direct, Cumulative, and Blend

Explains the three `control_mode` cursor laws in the Rust joystick task
(`rust/joystick_task/src/state.rs`, `step()`), with exact derivations from the
running code, and the neuroscience/BCI-decoder literature backing why
**cumulative** (velocity/integrative) control — not **direct** (position)
control — is the principled target for anything that will eventually be
driven by a neural decoder. Companion to `docs/closed_loop_decoder.md`, which
covers the decoder side of this same contract.

## Why this document exists

"Just map joystick position to cursor position" (`direct`) looks like the
obvious, simplest design, and collaborators regularly ask why the lab trains
animals on a more complicated **cumulative** control law instead, with
**blend** as an even more complicated bridge between the two. This doc gives
the exact math for all three modes and the field evidence for why cumulative
is not incidental complexity — it is rehearsing the same control law the
downstream neural decoder will use.

## 1. The three control laws

All three read the same inputs each frame: joystick deflection
`j ∈ [-1, 1]` per axis (after `apply_direction_influence`, which scales `j` by
the configured up/down/left/right influence factors), and `dt`, the frame
time in seconds (clamped to `[0, 0.05]`). Cursor position `c` lives in
normalized `[0, 1]` screen space per axis, center at `c = 0.5`. Defaults are
from `rust/joystick_task/src/config.rs`.

### Direct — position control

`state.rs` ~948–955 (`control_mode == "direct"` branch):

```
c = 0.5 + j · R          (R = direct_range, default 0.45)
```

Cursor is a **stateless function of instantaneous stick deflection** — no
integration, no memory. Every frame recomputes `c` from scratch (guarded by
`analog_active || direct_recenter_when_idle`, default `true`, so an idle
stick snaps the cursor back to center). Release the stick and the cursor
returns to `0.5` immediately. This is exactly a proportional (position-gain)
joystick: `c` and `j` are related by a fixed affine map, nothing more.

### Cumulative — velocity / integrative control

`state.rs` ~1000–1010 (the `else` / cumulative branch):

```
c[n+1] = c[n] + j · S · dt      (S = cumulative_speed, default 0.70)
```

which is the forward-Euler discretization of

```
ċ(t) = j(t) · S        i.e.     c(t) = c(0) + ∫₀ᵗ j(τ)·S dτ
```

The stick no longer commands a *position* — it commands a **rate**. Cursor
position is the running time-integral of stick deflection. This is genuine
state: center the stick and the cursor **holds** wherever it is (subject to
the `zero_drift_mode`/`zero_drift_buffer` deadband that zeroes `j` below a
small magnitude, preventing integrator drift from sensor noise/bias at rest).

### Blend — a leaky integrator that morphs between the two

`state.rs` ~956–999 (`control_mode == "blend"` branch). This is the
training bridge: `position_velocity_blend` (`b ∈ [0, 1]`, config default
`0.0`) continuously morphs the control law from direct (`b = 0`) to
cumulative (`b = 1`). The code always does the cumulative integration step,
then relaxes the result toward the direct target:

```
P = 0.5 + j · R                                   (the direct target, same R)
relax = clamp( (1 − b) / b · dt , 0, 1 )          (b = 0 -> relax = 1, b = 1 -> relax = 0)

c ← c + j · S · dt          (unconditional velocity/cumulative step)
c ← c + (P − c) · relax     (relax toward the direct target, same idle guard as direct)
```

This is a discretized **leaky integrator**. Written as a continuous ODE (the
form the discretization above approximates for small `dt`):

```
ċ(t) = j(t)·S + (1/τ) · (P(t) − c(t))          where   τ = b / (1 − b)   seconds
```

`τ` is the position-memory time constant. Two limits fall out algebraically,
not by special-casing:

- `b → 0` ⇒ `τ → 0` ⇒ `relax → 1` ⇒ `c ← P` every frame — the cumulative step
  is computed but then **fully overwritten** by the snap to `P`. Reduces
  exactly to direct.
- `b → 1` ⇒ `τ → ∞` ⇒ `relax → 0` ⇒ the relax term vanishes and only the
  cumulative integration survives. Reduces exactly to cumulative.

Intuition: a subject can hold the stick off-center and let the cursor
approach a resting offset at rate set by `τ` (larger `τ` ⇒ slower pull back
toward the position mapping, more of the trajectory is "genuinely
integrated"), while still inheriting cumulative's fundamental integration
behavior at every `b > 0`.

## 2. The one real logarithm in this system: `b ↔ τ`

There is no log-shaped curve anywhere mapping joystick position to
cumulative-mode speed — that mapping (`S`) is linear, per section 1. The
actual logarithm in this codebase is the **blend-knob-to-time-constant**
relationship derived above:

```
τ = b / (1 − b)
```

which is the *odds* of `b` (against `1 − b`). Taking the log:

```
log(τ) = log( b / (1 − b) ) = logit(b)
```

i.e. **`b` is the logistic-sigmoid coordinate for `log(τ)`**:

```
b = σ(log τ) = 1 / (1 + e^(−log τ))
```

This is why `position_velocity_blend` is a good UI slider despite the
underlying dynamics spanning orders of magnitude in `τ`: linear steps in `b`
produce *exponentially* spaced steps in the memory time constant, so the
slider feels perceptually even across the full 0→1 range instead of every
interesting value being crammed against one end.

| `b` | `τ = b/(1−b)` |
|---|---|
| 0.25 | 0.33 s |
| 0.50 | 1 s |
| 0.75 | 3 s |
| 0.90 | 9 s |

(Sanity check: `0.9/0.1 = 9`. ✓.)

The first implementation attempt did **not** do this — it cross-faded the
two laws' *outputs* directly (`c = (1−b)·P + b·(c + j·S·dt)`). That failed
because the velocity term is `dt`-scaled (~0.003 per frame at 240 Hz) while
`P` is an absolute position (~0.45 from center) — the position term
dominated for every `b` below ~0.99, so the slider felt like pure direct
control almost everywhere. The fix was to interpolate the **dynamics**
(the position-memory time constant `τ`), not the **outputs** — you cannot
cross-fade a stateless law and a stateful law and get something in between;
you have to blend what makes them stateful/stateless in the first place.

## 3. Why cumulative (integrative) control is the principled choice

The case for cumulative is not "it's what we happened to build" — it tracks
several independent lines of evidence in the motor-control and BCI-decoding
literature, laid out below with real, checked citations.

### 3a. The native motor system is already an integrator

Direct control asks the brain to output an **absolute position** command;
cumulative asks it to output a **rate** that the plant integrates over time
into position. The second is how biological reaching already works. Todorov's
model of cortical→muscle control shows that primary motor cortex output is
best understood as commanding muscle-like (force/activation) signals, which
the arm's own mechanics — not a further neural stage — integrate through
velocity into position; the *apparent* kinematic tuning of M1 neurons (to
position, velocity, direction) falls out as a consequence of that low-level
force/dynamics control, not because cortex is directly specifying position
(Todorov, 2000, *Nature Neuroscience* 3(4):391–398, "Direct cortical control
of muscle activation in voluntary arm movements: a model"). A `direct`
joystick mapping — deflection ⇒ absolute cursor position — skips this
integrative stage entirely; `cumulative` preserves it.

### 3b. Motor cortex encodes rate/velocity strongly, not raw position

Independent of the mechanistic argument above, empirical single- and
population-level recordings in M1 show velocity (and speed) are robustly and
richly encoded, on par with or more prominently than static position, during
natural reaching:

- Moran & Schwartz (1999, *J Neurophysiol* 82(5):2676–2692, "Motor cortical
  representation of speed and direction during reaching") found that
  time-varying **speed** — not just the classic Georgopoulos preferred
  direction — is represented in M1 discharge rate throughout a reach, with a
  single equation combining speed and direction accounting for most of the
  time-varying activity.
- Paninski, Fellows, Hatsopoulos & Donoghue (2004, *J Neurophysiol*
  91(1):515–532, "Spatiotemporal tuning of motor cortical neurons for hand
  position and velocity") used continuous pursuit-tracking (rather than
  discrete center-out reaches) specifically to separate position and
  velocity coding, and found heterogeneous but substantial velocity tuning
  with different temporal dynamics than position tuning — i.e., velocity is
  not just a derivative artifact of position tuning, it is its own encoded
  quantity.

A decode/control scheme built around velocity is reading out what the
cortex is shown to represent natively; a pure position-control scheme is not.

### 3c. BCI decoder design converged on velocity/integrative decoding as the default — and independently reinvented `blend`'s leaky-integrator trick

The landmark intracortical BMI cursor/arm-control results all decode
**velocity** and integrate it into position, not the reverse:

- Taylor, Helms Tillery & Schwartz (2002, *Science* 296(5574):1829–1832,
  "Direct cortical control of 3D neuroprosthetic devices") — one of the
  first closed-loop 3D BMI cursor demonstrations, decoding a velocity-like
  command from cortical population activity.
- Velliste, Perel, Spalding, Whitford & Schwartz (2008, *Nature*
  453(7198):1098–1101, "Cortical control of a prosthetic arm for
  self-feeding") — multi-joint prosthetic arm control from cortex, same
  velocity-command paradigm, extended to a real physical task.
- Gilja, Nuyujukian, Chestek, et al. (2012, *Nature Neuroscience*
  15(12):1752–1757, "A high-performance neural prosthesis enabled by
  control algorithm design") — refined velocity-decode control law
  (`ReFIT`-style) that closed most of the performance gap to native arm
  control, still velocity-based, not position-based.

Most tellingly, the field's own attempt to fix velocity-decoding's core
weakness — **open-loop integration drift** (small systematic errors in a
decoded velocity accumulate without bound in position, exactly the failure
mode `cumulative`'s `zero_drift_mode`/`zero_drift_buffer` deadband exists to
suppress) — independently arrived at this codebase's `blend` idea. Wu, Gao,
Bienenstock, Donoghue & Black's Kalman-filter decoder (2006, *Neural
Computation* 18(1):80–118, "Bayesian population decoding of motor cortical
activity using a Kalman filter") represents hand state as position, velocity,
**and** acceleration jointly (`x_k = [x, y, vx, vy, ax, ay]`, confirmed from
the original methods section) and re-estimates *position* from the neural
observation at every time step, rather than obtaining position purely by
open-loop-integrating a decoded velocity signal. Position is continuously
re-anchored by evidence instead of drifting freely — the same structural fix
this codebase's `blend` leaky integrator applies by relaxing the integrated
cursor toward the direct positional target `P` at rate `1/τ`. Two
independent engineering efforts (a 2006 neural Kalman-filter decoder and this
task's 2026 joystick control law) converged on "don't purely integrate a
rate signal forever — periodically re-anchor it toward a position estimate"
as the fix for the same drift failure mode. That convergence is evidence the
leaky-integrator structure is close to a necessary design, not an arbitrary
one.

### 3d. Animals control BCI cursors via an internal model consistent with integrative dynamics, and this control law is durably learnable

If cumulative/velocity control merely *looked* natural but couldn't actually
be learned or sustained by a subject, none of the above would matter
practically. Two results argue it is genuinely learnable and used the way
natural movement is:

- Golub, Yu & Chase (2012, *Proc. 34th IEEE EMBC*, pp. 1327–1330, "Internal
  models engaged by brain-computer interface control") found evidence,
  from simultaneously recorded M1 population activity during closed-loop BCI
  control, that the subject compensates for ~130 ms visual feedback delay by
  predicting upcoming cursor position — i.e., using an internal *forward*
  model of the (integrative) cursor dynamics, the same predictive strategy
  used in natural reaching, not simply reacting to each instantaneous cursor
  position.
- Ganguly & Carmena (2009, *PLoS Biology* 7(7):e1000153, "Emergence of a
  stable cortical map for neuroprosthetic control") showed that with a
  **fixed** linear (velocity) decoder held constant for weeks, a small M1
  population's activity patterns settled into a stable, reliable cortical
  map that sustained accurate center-out cursor control — i.e., an
  integrative/velocity control law is not just decodable but durably,
  stably learnable as a new sensorimotor skill, not a fragile trick that
  only works transiently.

### 3e. This is the field's default, not a fringe choice

Golub, Chase, Batista & Yu's review (2016, *Current Opinion in Neurobiology*
37:53–58, "Brain-computer interfaces for dissecting cognitive processes
underlying sensorimotor control") frames velocity/integrative BCI decoding as
the standard paradigm used across the field to study sensorimotor control
itself — BCIs built this way are treated as a tool precisely *because* their
control law mirrors the brain's natural rate→integration control scheme
closely enough to engage the same internal-model machinery described in
§3d. Training an animal on `cumulative` is training it on the control law
the rest of the field already standardized on for exactly this reason.

## 4. The practical payoff: this is decoder rehearsal, not lab quirk

`blend` exists purely as a **training ramp**: an animal already competent at
`direct` transfers to `cumulative` gradually (`b: 0 → 1` across sessions)
rather than being cold-switched to an unfamiliar control law overnight.

The reason this matters beyond the joystick: `neural_decoder.py`
(`docs/closed_loop_decoder.md`) **always computes velocity** internally, and
its `--emit-mode` is deliberately paired with the task's `control_mode`:

| decoder `--emit-mode` | pairs with task `control_mode` |
|---|---|
| `position` (leaky-integrates velocity → emits position) | `direct` |
| `velocity` (raw readout, task integrates) | `cumulative` or `blend` |

A subject trained under `cumulative` has already learned to treat stick
deflection as a *rate* command whose integral is cursor position — which is
exactly the control law a velocity-decoding BMI hands them. `direct` training
teaches the opposite mapping (deflection ⇒ absolute position), which does not
transfer to a velocity-based neural decoder without relearning. `cumulative`
is the joystick-stage rehearsal of the control law the neural decoder will
eventually hand the animal; `blend` is how a subject already trained on
`direct` gets there without a disruptive cold switch.

## References

All verified live (title/authors/journal/year cross-checked against
PubMed/publisher records) while writing this doc — none from memory.

1. Todorov E (2000). Direct cortical control of muscle activation in
   voluntary arm movements: a model. *Nature Neuroscience* 3(4):391–398.
2. Moran DW, Schwartz AB (1999). Motor cortical representation of speed and
   direction during reaching. *Journal of Neurophysiology* 82(5):2676–2692.
3. Paninski L, Fellows MR, Hatsopoulos NG, Donoghue JP (2004). Spatiotemporal
   tuning of motor cortical neurons for hand position and velocity. *Journal
   of Neurophysiology* 91(1):515–532.
4. Wu W, Gao Y, Bienenstock E, Donoghue JP, Black MJ (2006). Bayesian
   population decoding of motor cortical activity using a Kalman filter.
   *Neural Computation* 18(1):80–118.
5. Taylor DM, Helms Tillery SI, Schwartz AB (2002). Direct cortical control
   of 3D neuroprosthetic devices. *Science* 296(5574):1829–1832.
6. Velliste M, Perel S, Spalding MC, Whitford AS, Schwartz AB (2008).
   Cortical control of a prosthetic arm for self-feeding. *Nature*
   453(7198):1098–1101.
7. Gilja V, Nuyujukian P, Chestek CA, et al. (2012). A high-performance
   neural prosthesis enabled by control algorithm design. *Nature
   Neuroscience* 15(12):1752–1757.
8. Golub MD, Yu BM, Chase SM (2012). Internal models engaged by
   brain-computer interface control. *Proceedings of the 34th Annual
   International Conference of the IEEE Engineering in Medicine and Biology
   Society (EMBC)*, 1327–1330.
9. Ganguly K, Carmena JM (2009). Emergence of a stable cortical map for
   neuroprosthetic control. *PLoS Biology* 7(7):e1000153.
10. Golub MD, Chase SM, Batista AP, Yu BM (2016). Brain-computer interfaces
    for dissecting cognitive processes underlying sensorimotor control.
    *Current Opinion in Neurobiology* 37:53–58.
