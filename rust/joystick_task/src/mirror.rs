//! Operator live-mirror (M4).
//!
//! STATUS: stub. Streams downscaled JPEG frames of the subject display to the
//! Python delegate over RustTask.frames (~30 Hz). Produced on a SEPARATE thread
//! from a copy of the last presented frame so it adds ZERO latency to the subject
//! present path.
//!
//! Do NOT reuse Canvas's existing WebSocket server (canvas.py:672/678/134): that
//! path forwards Python QPainter draw commands to a browser (wrong direction).
//! We reuse the principle (off-critical-path mirror channel), not the wiring.
//! See docs/rust_bci_patch.md "Operator live mirror".

/// Downscale + JPEG-encode a presented RGBA frame for the operator mirror.
/// TODO(M4): implement with turbojpeg; cap rate at FrameRequest.max_hz.
pub fn encode_frame(_rgba: &[u8], _width: u32, _height: u32, _max_width: u32) -> Vec<u8> {
    unimplemented!("M4: downscale + JPEG encode for RustTask.frames")
}
