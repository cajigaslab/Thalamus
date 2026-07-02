//! Operator live-mirror (M4).
//!
//! Streams downscaled JPEG frames of the subject display to the Python
//! delegate over RustTask.frames (~30 Hz).
//!
//! Design: NO GPU readback. The render thread publishes each frame's SHAPE
//! LIST (a few hundred bytes, `render::SceneUniforms`) into a watch channel;
//! this module re-rasterizes those shapes on the CPU at mirror resolution
//! inside the frames() stream task. The subject present path is never touched
//! — publishing the shapes is a memcpy, and everything else happens on tokio
//! worker threads at the operator's frame rate.
//!
//! Do NOT reuse Canvas's existing WebSocket server (canvas.py:672/678/134):
//! that path forwards Python QPainter draw commands to a browser (wrong
//! direction). We reuse the principle (off-critical-path mirror channel), not
//! the wiring. See docs/rust_bci_patch.md "Operator live mirror".

use crate::render::{Shape, SceneUniforms, KIND_ELLIPSE, KIND_FILLED_RECT, KIND_RECT_OUTLINE};

/// What the render thread publishes every frame.
#[derive(Clone)]
pub struct MirrorScene {
    pub uniforms: SceneUniforms,
    /// Subject surface size the shape coordinates are in.
    pub width: u32,
    pub height: u32,
}

impl Default for MirrorScene {
    fn default() -> Self {
        Self {
            uniforms: SceneUniforms::empty(),
            width: 1920,
            height: 1080,
        }
    }
}

/// Coverage test identical to the WGSL shader (render/mod.rs SHADER).
fn coverage(px: f32, py: f32, sh: &Shape) -> f32 {
    let kind = sh.misc[0];
    if kind == KIND_FILLED_RECT {
        let in_box = px >= sh.geo[0] && px < sh.geo[2] && py >= sh.geo[1] && py < sh.geo[3];
        return in_box as u8 as f32;
    }
    if kind == KIND_RECT_OUTLINE {
        let t = sh.misc[1];
        let outer = px >= sh.geo[0] && px < sh.geo[2] && py >= sh.geo[1] && py < sh.geo[3];
        let inner = px >= sh.geo[0] + t
            && px < sh.geo[2] - t
            && py >= sh.geo[1] + t
            && py < sh.geo[3] - t;
        return (outer && !inner) as u8 as f32;
    }
    if kind == KIND_ELLIPSE {
        let dx = (px - sh.geo[0]) / sh.geo[2].max(0.5);
        let dy = (py - sh.geo[1]) / sh.geo[3].max(0.5);
        return (dx * dx + dy * dy <= 1.0) as u8 as f32;
    }
    // Ring arc.
    let dx = px - sh.geo[0];
    let dy = py - sh.geo[1];
    let dist = (dx * dx + dy * dy).sqrt();
    if (dist - sh.geo[2]).abs() > sh.misc[1] * 0.5 {
        return 0.0;
    }
    let mut theta = dx.atan2(-dy); // 0 at 12 o'clock, clockwise
    if theta < 0.0 {
        theta += std::f32::consts::TAU;
    }
    (theta <= sh.misc[2] * std::f32::consts::TAU) as u8 as f32
}

fn linear_to_srgb_u8(l: f32) -> u8 {
    let l = l.clamp(0.0, 1.0);
    let s = if l <= 0.003_130_8 {
        12.92 * l
    } else {
        1.055 * l.powf(1.0 / 2.4) - 0.055
    };
    (s * 255.0 + 0.5) as u8
}

/// Rasterize the shape list into an RGB buffer of `out_w` x `out_h`, sampling
/// shape coordinates in the subject's pixel space.
pub fn rasterize(scene: &MirrorScene, out_w: u32, out_h: u32) -> Vec<u8> {
    let sx = scene.width as f32 / out_w as f32;
    let sy = scene.height as f32 / out_h as f32;
    let n = scene.uniforms.shape_count();
    let shapes = &scene.uniforms.shapes[..n];
    let mut rgb = vec![0u8; (out_w * out_h * 3) as usize];
    let mut i = 0;
    for y in 0..out_h {
        let py = (y as f32 + 0.5) * sy;
        for x in 0..out_w {
            let px = (x as f32 + 0.5) * sx;
            // Linear-space composite over black, like the shader.
            let mut c = [0.0f32; 3];
            for sh in shapes {
                let cov = coverage(px, py, sh) * sh.color[3];
                if cov > 0.0 {
                    for k in 0..3 {
                        c[k] = c[k] + (sh.color[k] - c[k]) * cov;
                    }
                }
            }
            rgb[i] = linear_to_srgb_u8(c[0]);
            rgb[i + 1] = linear_to_srgb_u8(c[1]);
            rgb[i + 2] = linear_to_srgb_u8(c[2]);
            i += 3;
        }
    }
    rgb
}

/// Rasterize + JPEG-encode one mirror frame. `max_width` caps the mirror
/// resolution; height follows the subject aspect ratio.
pub fn encode_frame(scene: &MirrorScene, max_width: u32) -> anyhow::Result<(Vec<u8>, u32, u32)> {
    let out_w = max_width.clamp(64, scene.width.max(64));
    let out_h = ((out_w as u64 * scene.height as u64) / scene.width.max(1) as u64).max(36) as u32;
    let rgb = rasterize(scene, out_w, out_h);
    let mut jpeg = Vec::with_capacity(32 * 1024);
    let encoder = jpeg_encoder::Encoder::new(&mut jpeg, 75);
    encoder.encode(&rgb, out_w as u16, out_h as u16, jpeg_encoder::ColorType::Rgb)?;
    Ok((jpeg, out_w, out_h))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scene_with(shapes: Vec<Shape>) -> MirrorScene {
        let mut u = SceneUniforms::empty();
        for (i, s) in shapes.iter().enumerate() {
            u.shapes[i] = *s;
        }
        u.header[0] = shapes.len() as f32;
        MirrorScene { uniforms: u, width: 1920, height: 1080 }
    }

    #[test]
    fn rasterizes_filled_rect_at_scaled_position() {
        // White 70x70 square at bottom-right of a 1920x1080 surface.
        let scene = scene_with(vec![Shape {
            color: [1.0, 1.0, 1.0, 1.0],
            geo: [1820.0, 940.0, 1890.0, 1010.0],
            misc: [KIND_FILLED_RECT, 0.0, 0.0, 0.0],
        }]);
        let (w, h) = (480, 270);
        let rgb = rasterize(&scene, w, h);
        // Center of the square in mirror coords: (1855, 975) / 4 = (463, 243).
        let at = |x: u32, y: u32| rgb[((y * w + x) * 3) as usize];
        assert_eq!(at(463, 243), 255);
        // Far away: black.
        assert_eq!(at(100, 100), 0);
    }

    #[test]
    fn encodes_valid_jpeg() {
        let scene = scene_with(vec![Shape {
            color: [0.2, 0.8, 0.3, 1.0],
            geo: [960.0, 540.0, 200.0, 200.0],
            misc: [KIND_ELLIPSE, 0.0, 0.0, 0.0],
        }]);
        let (jpeg, w, h) = encode_frame(&scene, 480).unwrap();
        assert_eq!((w, h), (480, 270));
        // JPEG magic + EOI.
        assert_eq!(&jpeg[..2], &[0xFF, 0xD8]);
        assert_eq!(&jpeg[jpeg.len() - 2..], &[0xFF, 0xD9]);
        assert!(jpeg.len() > 500);
    }
}
