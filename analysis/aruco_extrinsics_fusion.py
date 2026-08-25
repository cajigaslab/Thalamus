#!/usr/bin/env python3
"""
ArUco extrinsic-calibration + multi-camera pose fusion (offline / post-recording).

Turns the per-camera ArUco board poses saved by a Thalamus STORAGE2 recording
into ONE fused 3D scene:

  1. Reads the recording's `xsens` records for an ARUCO node (6-DOF board pose
     per camera, in each camera's own frame).
  2. Solves the rig EXTRINSICS (relative pose of every camera) purely from the
     data, using frames where the SAME board is co-visible in >=2 cameras.
  3. FUSES each board's pose across cameras into one reference frame, rejecting
     outlier / pose-flip camera estimates robustly.
  4. Reports per-board fused pose + honest per-axis uncertainty (cross-camera
     agreement = accuracy proxy, temporal std = precision).

This is deliberately dependency-light (numpy only) and CPU-cheap: it does a
handful of small SE(3) averages per frame. It is meant to run AFTER a session,
not in real time.

Contract notes (verified against src/aruco_node.cpp + src/storage2_node.cpp):
  - segment.id = camera_index*100 + board_index
  - camera_index follows the ARUCO node's "Sources" order in the config json.
  - board_index is the board's position in a POINTER-keyed std::map, so it is
    stable WITHIN one recording but NOT guaranteed across restarts. The geometry
    here only relies on within-recording consistency; board *names* are attached
    best-effort by correlating camera-coverage against the named analog channels.
  - quaternion is stored (q0,q1,q2,q3) = (w,x,y,z).

Usage:
  .venv/bin/python3 analysis/aruco_extrinsics_fusion.py <recording_binary> \
      [--config <sidecar.json>] [--aruco-node "Aruco Static"] \
      [--ref-camera "Distortion Cam Rear"] [--ref-board "Screen 2x2"] \
      [--reject-mm 5.0] [--max-records 0]

If --config is omitted it looks for "<recording>.json" next to the binary.
"""
import argparse
import bisect
import collections
import os
import struct
import sys

import numpy as np

# --- locate thalamus_pb2 (repo layout: <repo>/thalamus/thalamus_pb2.py) --------
_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(_HERE)
for cand in (os.path.join(_REPO, "thalamus"), _REPO):
    if os.path.isfile(os.path.join(cand, "thalamus_pb2.py")):
        sys.path.insert(0, cand)
        break
import thalamus_pb2 as tp  # noqa: E402

METRIC_SUFFIXES = ("_reproj_px", "_n_markers", "_px_per_mm", "_jitter_mm")


# ============================ quaternion / SE(3) =============================
def quat_normalize(q):
    q = np.asarray(q, float)
    n = np.linalg.norm(q)
    if n == 0:
        return np.array([1.0, 0, 0, 0])
    q = q / n
    if q[0] < 0:  # canonical hemisphere (w >= 0)
        q = -q
    return q


def quat_to_R(q):
    w, x, y, z = quat_normalize(q)
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
        [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
        [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)],
    ])


def R_to_quat(R):
    R = np.asarray(R, float)
    t = np.trace(R)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        w = 0.25 * s
        x = (R[2, 1] - R[1, 2]) / s
        y = (R[0, 2] - R[2, 0]) / s
        z = (R[1, 0] - R[0, 1]) / s
    else:
        i = int(np.argmax(np.diagonal(R)))
        if i == 0:
            s = np.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]) * 2
            w = (R[2, 1] - R[1, 2]) / s
            x = 0.25 * s
            y = (R[0, 1] + R[1, 0]) / s
            z = (R[0, 2] + R[2, 0]) / s
        elif i == 1:
            s = np.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]) * 2
            w = (R[0, 2] - R[2, 0]) / s
            x = (R[0, 1] + R[1, 0]) / s
            y = 0.25 * s
            z = (R[1, 2] + R[2, 1]) / s
        else:
            s = np.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]) * 2
            w = (R[1, 0] - R[0, 1]) / s
            x = (R[0, 2] + R[2, 0]) / s
            y = (R[1, 2] + R[2, 1]) / s
            z = 0.25 * s
    return quat_normalize([w, x, y, z])


def quat_mean(quats):
    """Markley L2 mean: principal eigenvector of sum(q q^T)."""
    if len(quats) == 1:
        return quat_normalize(quats[0])
    M = np.zeros((4, 4))
    for q in quats:
        q = quat_normalize(q)
        M += np.outer(q, q)
    w, v = np.linalg.eigh(M)
    return quat_normalize(v[:, int(np.argmax(w))])


def quat_angle_deg(q1, q2):
    d = abs(float(np.dot(quat_normalize(q1), quat_normalize(q2))))
    d = min(1.0, max(-1.0, d))
    return float(np.degrees(2 * np.arccos(d)))


class SE3:
    """Rigid transform ^A T_B ; maps a point in B into A: p_A = R p_B + t."""
    __slots__ = ("R", "t")

    def __init__(self, R, t):
        self.R = np.asarray(R, float)
        self.t = np.asarray(t, float).reshape(3)

    @classmethod
    def from_qt(cls, q, t):
        return cls(quat_to_R(q), t)

    def inv(self):
        Rt = self.R.T
        return SE3(Rt, -Rt @ self.t)

    def __matmul__(self, o):
        return SE3(self.R @ o.R, self.R @ o.t + self.t)

    def apply(self, p):
        return self.R @ np.asarray(p, float) + self.t

    def quat(self):
        return R_to_quat(self.R)


def se3_mean(transforms):
    """Average a list of SE3 (rotation via quat mean, translation via mean)."""
    q = quat_mean([T.quat() for T in transforms])
    t = np.mean([T.t for T in transforms], axis=0)
    return SE3.from_qt(q, t)


# ================================ parsing ===================================
def parse_records(path, max_records=0):
    """Yield (body_type, node, time, msg) for each StorageRecord in the file."""
    n = 0
    with open(path, "rb") as f:
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            size = struct.unpack(">Q", hdr)[0]
            if size == 0 or size > (1 << 31):
                break
            payload = f.read(size)
            if len(payload) < size:
                break
            r = tp.StorageRecord()
            try:
                r.ParseFromString(payload)
            except Exception:
                continue
            body = r.WhichOneof("body")
            yield body, r.node, r.time, r
            n += 1
            if max_records and n >= max_records:
                break


def load_config(cfg_path, aruco_node):
    """Return (camera_index->name list, set of board names) from the config json."""
    import json
    cams, boards = [], []
    if not cfg_path or not os.path.isfile(cfg_path):
        return cams, boards
    d = json.load(open(cfg_path))

    def walk(o):
        if isinstance(o, dict):
            if o.get("type") == "ARUCO" and o.get("name") == aruco_node:
                for s in o.get("Sources", []) or []:
                    cams.append(s)
                for b in o.get("Boards", []) or []:
                    if b.get("Name"):
                        boards.append(b["Name"])
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(d)
    return cams, boards


def parse_analog_channel(name, camera_names):
    """Split 'Cam X_Board Name_metric' -> (camera, board, metric) best-effort."""
    for suf in METRIC_SUFFIXES:
        if name.endswith(suf):
            metric = suf[1:]
            head = name[: -len(suf)]
            for cam in sorted(camera_names, key=len, reverse=True):
                if head.startswith(cam + "_"):
                    return cam, head[len(cam) + 1:], metric
            # camera unknown: everything up to last '_' is camera
            if "_" in head:
                cam, board = head.rsplit("_", 1)
                return cam, board, metric
    return None, None, None


# ============================ core computation ===============================
def collect(path, aruco_node, camera_names, max_records):
    """
    Returns:
      frames: list of {(cam_idx, board_idx): SE3}  (one entry per xsens record)
      analog_cov: {board_name: set(cam_name)} of boards actually detected
    """
    frames = []
    analog_cov = collections.defaultdict(set)
    for body, node, t, r in parse_records(path, max_records):
        if node != aruco_node:
            continue
        if body == "xsens":
            obs = {}
            for s in r.xsens.segments:
                cam_idx, board_idx = divmod(int(s.id), 100)
                obs[(cam_idx, board_idx)] = SE3.from_qt(
                    (s.q0, s.q1, s.q2, s.q3), (s.x, s.y, s.z))
            if obs:
                frames.append((t, obs))
        elif body == "analog":
            a = r.analog
            data = list(a.data)
            for sp in a.spans:
                cam, board, metric = parse_analog_channel(sp.name, camera_names)
                if metric == "n_markers" and sp.end <= len(data) and sp.end > sp.begin:
                    if any(v > 0 for v in data[sp.begin:sp.end]):
                        analog_cov[board].add(cam)
    return frames, analog_cov


def solve_extrinsics(frames):
    """
    Accumulate ^{camA}T_{camB} from every frame where a board is seen by both.
    Returns:
      rel[(a,b)] = averaged SE3 ^{a}T_{b}
      rel_disp[(a,b)] = (translation std mm, rotation std deg, n samples)
      cams = sorted set of camera indices seen
    """
    pair_T = collections.defaultdict(list)
    cams = set()
    for _t, obs in frames:
        by_board = collections.defaultdict(dict)
        for (cam, board), T in obs.items():
            by_board[board][cam] = T
            cams.add(cam)
        for board, cam_T in by_board.items():
            cs = sorted(cam_T)
            for i in range(len(cs)):
                for j in range(len(cs)):
                    if i == j:
                        continue
                    a, b = cs[i], cs[j]
                    # ^{a}T_{b} = ^{a}T_{board} * (^{b}T_{board})^-1
                    pair_T[(a, b)].append(cam_T[a] @ cam_T[b].inv())
    rel, rel_disp = {}, {}
    for pair, Ts in pair_T.items():
        mean = se3_mean(Ts)
        tstd = float(np.mean(np.std([T.t for T in Ts], axis=0)) * 1000.0)
        rstd = float(np.mean([quat_angle_deg(mean.quat(), T.quat()) for T in Ts]))
        rel[pair] = mean
        rel_disp[pair] = (tstd, rstd, len(Ts))
    return rel, rel_disp, sorted(cams)


def build_camera_frames(rel, cams, ref_cam):
    """BFS over the pairwise graph -> ^{ref}T_{cam} for every reachable camera."""
    ref_T = {ref_cam: SE3(np.eye(3), np.zeros(3))}
    queue = [ref_cam]
    while queue:
        a = queue.pop(0)
        for (x, y), T in rel.items():
            if x == a and y not in ref_T:
                ref_T[y] = ref_T[a] @ T          # ^{ref}T_y = ^{ref}T_a * ^{a}T_y
                queue.append(y)
    return ref_T


def robust_mean_points(points, reject_mm):
    """Median-based outlier rejection, then mean. points: Nx3 (meters)."""
    P = np.asarray(points, float)
    if len(P) == 1:
        return P[0], np.zeros(3), np.array([True])
    med = np.median(P, axis=0)
    d = np.linalg.norm(P - med, axis=1) * 1000.0
    keep = d <= reject_mm
    if keep.sum() == 0:
        keep = d <= np.median(d) * 2 + 1e-9
    kept = P[keep]
    return kept.mean(axis=0), kept.std(axis=0), keep


def fuse(frames, ref_T, reject_mm):
    """
    Transform every board pose into the reference frame and fuse.
    Returns per-board dict with fused pose, cross-camera residual, temporal std.
    """
    # per (board) -> list over frames of dict cam-> ^{ref}T_board
    board_frames = collections.defaultdict(list)
    for _t, obs in frames:
        per_board = collections.defaultdict(dict)
        for (cam, board), T in obs.items():
            if cam in ref_T:
                per_board[board][cam] = ref_T[cam] @ T
        for board, cam_T in per_board.items():
            board_frames[board].append(cam_T)

    results = {}
    for board, flist in board_frames.items():
        per_frame_pos = []          # fused position each frame
        per_frame_quat = []
        cross_cam_res = []          # cross-camera spread each multi-cam frame (mm)
        cams_seen = collections.Counter()
        for cam_T in flist:
            cams = sorted(cam_T)
            for c in cams:
                cams_seen[c] += 1
            pts = np.array([cam_T[c].t for c in cams])
            fused_pos, _std, keep = robust_mean_points(pts, reject_mm)
            quats = [cam_T[c].quat() for c, k in zip(cams, keep) if k]
            per_frame_pos.append(fused_pos)
            per_frame_quat.append(quat_mean(quats))
            if len(cams) >= 2:
                # spread of camera estimates about their median (mm)
                med = np.median(pts, axis=0)
                cross_cam_res.append(
                    float(np.mean(np.linalg.norm(pts - med, axis=1)) * 1000.0))
        per_frame_pos = np.array(per_frame_pos)
        static_pos = np.median(per_frame_pos, axis=0)
        temporal_std_mm = per_frame_pos.std(axis=0) * 1000.0
        static_quat = quat_mean(per_frame_quat)
        ori_spread_deg = float(np.mean(
            [quat_angle_deg(static_quat, q) for q in per_frame_quat]))
        results[board] = dict(
            n_frames=len(flist),
            cams_seen=dict(cams_seen),
            position_m=static_pos,
            quaternion_wxyz=static_quat,
            temporal_std_mm=temporal_std_mm,
            cross_cam_residual_mm=(float(np.mean(cross_cam_res))
                                   if cross_cam_res else None),
            ori_spread_deg=ori_spread_deg,
        )
    return results


# ================================ mapping ===================================
def map_board_names(frames, analog_cov, camera_names):
    """
    Best-effort board_index -> name by matching camera-coverage signatures
    between the xsens indices and the named analog channels.
    """
    cov_idx = collections.defaultdict(set)
    for _t, obs in frames:
        for (cam, bidx) in obs:
            cov_idx[bidx].add(cam)
    # cam name -> index
    cam_name_to_idx = {name: i for i, name in enumerate(camera_names)}
    cov_name = {}
    for board, camset in analog_cov.items():
        idxs = {cam_name_to_idx[c] for c in camset if c in cam_name_to_idx}
        if idxs:
            cov_name[board] = idxs
    mapping = {}
    used = set()
    for bidx, camset in cov_idx.items():
        match = [nm for nm, cs in cov_name.items()
                 if cs == camset and nm not in used]
        if len(match) == 1:
            mapping[bidx] = match[0]
            used.add(match[0])
        else:
            mapping[bidx] = f"board{bidx}"
    return mapping, cov_idx


# ================================= report ===================================
def pick_ref_camera(rel_disp, cams, camera_names, requested):
    if requested is not None:
        for i, nm in enumerate(camera_names):
            if nm == requested or requested in nm:
                return i
        try:
            return int(requested)
        except ValueError:
            pass
    # else: camera involved in the most pairs (best-connected)
    deg = collections.Counter()
    for (a, b) in rel_disp:
        deg[a] += 1
    return deg.most_common(1)[0][0] if deg else (cams[0] if cams else 0)


def camname(i, camera_names):
    return camera_names[i] if i < len(camera_names) else f"cam{i}"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recording")
    ap.add_argument("--config", default=None)
    ap.add_argument("--aruco-node", default="Aruco Static")
    ap.add_argument("--ref-camera", default=None,
                    help="camera name/substring or index to use as world frame")
    ap.add_argument("--ref-board", default=None,
                    help="express scene relative to this board instead of ref camera")
    ap.add_argument("--reject-mm", type=float, default=5.0,
                    help="cross-camera outlier rejection radius (mm)")
    ap.add_argument("--max-records", type=int, default=0)
    args = ap.parse_args()

    cfg = args.config or (args.recording + ".json")
    camera_names, cfg_boards = load_config(cfg, args.aruco_node)

    print(f"# recording : {args.recording}")
    print(f"# aruco node: {args.aruco_node}")
    print(f"# cameras   : {camera_names or '(config not found - using indices)'}")
    print(f"# boards    : {cfg_boards}")

    frames, analog_cov = collect(args.recording, args.aruco_node,
                                 camera_names, args.max_records)
    if not frames:
        print("\nNo xsens (pose) records for this node. Was 'Motion' enabled "
              "for it in the Storage node?")
        return 1
    print(f"\n# pose frames: {len(frames)}")

    mapping, cov_idx = map_board_names(frames, analog_cov, camera_names)
    print("\n== board index -> name (within this recording) ==")
    for bidx in sorted(cov_idx):
        cams = ", ".join(camname(c, camera_names) for c in sorted(cov_idx[bidx]))
        print(f"   board_index {bidx} = {mapping[bidx]!r:14s} seen by: {cams}")

    rel, rel_disp, cams = solve_extrinsics(frames)
    if len(cams) < 2:
        print("\nOnly one camera saw the boards - no extrinsics to solve. "
              "Per-camera pose is still valid; multi-camera fusion needs >=2 "
              "cameras co-viewing a board.")
    ref_cam = pick_ref_camera(rel_disp, cams, camera_names, args.ref_camera)
    ref_T = build_camera_frames(rel, cams, ref_cam)

    print(f"\n== rig extrinsics (reference camera = "
          f"{camname(ref_cam, camera_names)}) ==")
    for c in cams:
        if c == ref_cam:
            print(f"   {camname(c, camera_names):22s} : reference (identity)")
        elif c in ref_T:
            base = np.linalg.norm(ref_T[c].t) * 1000.0
            disp = rel_disp.get((ref_cam, c)) or rel_disp.get((c, ref_cam))
            extra = (f"  [pair spread: {disp[0]:.2f} mm / {disp[1]:.2f} deg, "
                     f"n={disp[2]}]" if disp else "")
            print(f"   {camname(c, camera_names):22s} : baseline "
                  f"{base:7.1f} mm from ref{extra}")
        else:
            print(f"   {camname(c, camera_names):22s} : NOT connected "
                  f"(never co-viewed a board with the ref set)")

    results = fuse(frames, ref_T, args.reject_mm)

    # optional: re-express relative to a chosen board
    world_label = f"{camname(ref_cam, camera_names)} frame"
    ref_board_T = None
    if args.ref_board:
        target = None
        for bidx, nm in mapping.items():
            if (nm == args.ref_board or args.ref_board in nm) and bidx in results:
                target = bidx
                break
        if target is not None:
            r = results[target]
            ref_board_T = SE3.from_qt(r["quaternion_wxyz"], r["position_m"]).inv()
            world_label = f"{mapping[target]} frame"
        else:
            print(f"\n(!) ref-board {args.ref_board!r} not found among fused boards")

    print(f"\n== fused board poses  [world = {world_label}] ==")
    for bidx in sorted(cov_idx):
        name = mapping[bidx]
        if bidx not in results:
            continue
        r = results[bidx]
        pos = r["position_m"]
        quat = r["quaternion_wxyz"]
        if ref_board_T is not None:
            T = ref_board_T @ SE3.from_qt(quat, pos)
            pos, quat = T.t, T.quat()
        ccr = r["cross_cam_residual_mm"]
        ccr_s = f"{ccr:.2f} mm" if ccr is not None else "n/a (single-cam)"
        tstd = r["temporal_std_mm"]
        cams_s = ", ".join(f"{camname(c, camera_names)}x{n}"
                           for c, n in sorted(r["cams_seen"].items()))
        print(f"\n  {name}  ({r['n_frames']} frames; {cams_s})")
        print(f"    position (m)         : "
              f"[{pos[0]:+.4f}, {pos[1]:+.4f}, {pos[2]:+.4f}]")
        print(f"    quaternion (w,x,y,z) : "
              f"[{quat[0]:+.4f}, {quat[1]:+.4f}, {quat[2]:+.4f}, {quat[3]:+.4f}]")
        print(f"    cross-camera agree   : {ccr_s}   <- accuracy proxy")
        print(f"    temporal std (x,y,z) : "
              f"[{tstd[0]:.2f}, {tstd[1]:.2f}, {tstd[2]:.2f}] mm   <- precision")
        print(f"    orientation spread   : {r['ori_spread_deg']:.2f} deg")

    print("\n# reading guide:")
    print("#  cross-camera agree  = how much the cameras disagree after extrinsics")
    print("#                        (your best single indicator of absolute accuracy)")
    print("#  temporal std        = frame-to-frame repeatability (precision)")
    print("#  a board seen by only one camera has no cross-camera check - trust its")
    print("#  z (depth) axis least.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
