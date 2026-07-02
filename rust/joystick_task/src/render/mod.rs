//! Subject-display rendering: the production frame-paced trial loop.
//!
//! Everything latency-critical learned in the M1 spike is baked in here:
//!   - OVERRIDE-REDIRECT window: GNOME/Mutter X11 composites all monitors on
//!     the 60 Hz primary's clock and refuses to unredirect a WM-fullscreen
//!     window even with _NET_WM_BYPASS_COMPOSITOR; an unmanaged window
//!     bypasses it (measured: 40 ms -> 30 ms). No keyboard focus — all input
//!     arrives over gRPC.
//!   - FIFO + frame pacing: NVIDIA ignores desired_maximum_frame_latency and
//!     lets a free-running loop queue ~5 frames (~20 ms). The pacer waits
//!     until margin_ms before the predicted vblank, one-shot drains the queue,
//!     and rolls over to the NEXT vblank when a frame finishes inside the
//!     margin window (else it double-submits per refresh). Measured
//!     command-to-photon: p50 11.3 ms @240 Hz.
//!   - Decide LAST: the swapchain acquire happens BEFORE the state-machine
//!     step, so the freshest joystick sample drives what this frame shows.
//!
//! Per frame with an active trial:
//!   acquire -> drain operator events + joystick samples -> trial.step(now)
//!   -> ship effects (BehavState log + marker, rewards) -> encode scene ->
//!   present -> pace.
//!
//! Scene parity with the Python renderer (joystick_intro.py:2694-2836), minus
//! text/HUD (glyphon lands with the M4 operator mirror; the subject-side text
//! is cosmetic) and the success pop/particle animations (config-disabled by
//! default): region outline, target ellipse (active/inactive color+opacity),
//! hold-progress ring, cursor, photodiode square.

pub mod diode;
pub mod spike;

use std::sync::Arc;
use std::time::{Duration, Instant};

use anyhow::Context as _;
use tokio::sync::mpsc::UnboundedSender;
use tonic::Status;
use winit::application::ApplicationHandler;
use winit::event::WindowEvent;
use winit::event_loop::ActiveEventLoop;
use winit::window::{Window, WindowId};

use crate::clock::ClockMap;
use crate::constants::STATE_INDICATOR_EDGE_PX;
use crate::input::JoystickState;
use crate::proto::rust_task::{trial_event::Body, BehavMarker, TrialEvent};
use crate::state::{State, Trial};

/// Operator-side inputs forwarded by the delegate mid-trial.
#[derive(Debug, Clone)]
pub enum OpEvent {
    Arrow { key: String, pressed: bool },
    EndRequested(bool),
    Touch { x: i32, y: i32 },
}

/// Side-effect requests the render thread hands to the tokio effects task.
#[derive(Debug)]
pub enum EffectMsg {
    /// Log `text` to Thalamus with an already-mapped perf_counter time.
    Log { text: String, time_ns: u64 },
    /// Deliver reward pulses (50 ms apart when repeated).
    Reward { channel: i64, repeats: i64 },
}

/// One trial's worth of plumbing, built by control.rs and handed to the
/// render thread.
pub struct Job {
    pub trial: Trial,
    pub clock: ClockMap,
    /// Lock-free freshest joystick sample (updated by the input pump).
    pub joystick: JoystickState,
    /// Every joystick sample (python-domain seconds, x, y) for behav_result.
    pub sample_rx: std::sync::mpsc::Receiver<(f64, f64, f64)>,
    /// Operator events from the delegate's request stream.
    pub op_rx: std::sync::mpsc::Receiver<OpEvent>,
    /// Trial event stream back to the delegate.
    pub event_tx: UnboundedSender<Result<TrialEvent, Status>>,
    /// Log/reward execution on the tokio side.
    pub effect_tx: UnboundedSender<EffectMsg>,
}

/// Ship one trial-machine effect: BehavState logs go to Thalamus (via the
/// effects task) AND to the delegate stream as a marker; rewards go to the
/// effects task. Shared by the render loop and control.rs (trial.begin()).
pub fn ship_effect(
    clock: &ClockMap,
    effect_tx: &UnboundedSender<EffectMsg>,
    event_tx: &UnboundedSender<Result<TrialEvent, Status>>,
    effect: crate::state::Effect,
) {
    match effect {
        crate::state::Effect::LogState(state) => {
            let text = format!("BehavState={state}");
            let time_ns = clock.now_python_ns();
            let _ = effect_tx.send(EffectMsg::Log {
                text: text.clone(),
                time_ns,
            });
            let _ = event_tx.send(Ok(TrialEvent {
                body: Some(Body::Marker(BehavMarker { text, time_ns })),
            }));
        }
        crate::state::Effect::Reward { channel, repeats } => {
            let _ = effect_tx.send(EffectMsg::Reward { channel, repeats });
        }
    }
}

#[derive(Debug, Clone)]
pub struct ExecutorArgs {
    pub monitor: usize,
    pub windowed: bool,
    pub pace_margin_ms: f64,
}

// ---------------------------------------------------------------------------
// Frame pacing (see module docs and rust/README.md "M1 RESULTS").
// ---------------------------------------------------------------------------

struct FramePacer {
    period_ns: u64,
    margin_ns: u64,
    vblank_anchor: Option<Instant>,
    drained: bool,
}

impl FramePacer {
    fn new(period_ns: u64, margin_ms: f64) -> Self {
        Self {
            period_ns,
            margin_ns: (margin_ms.max(0.0) * 1e6) as u64,
            vblank_anchor: None,
            drained: false,
        }
    }

    /// Call with the instant a swapchain acquire/present unblocked — that is
    /// (approximately) a vblank.
    fn on_vblank(&mut self, at: Instant) {
        self.vblank_anchor = Some(at);
    }

    /// Sleep until margin before the next predicted vblank.
    fn pace(&mut self) {
        if self.margin_ns == 0 || self.period_ns == 0 {
            return;
        }
        let Some(anchor) = self.vblank_anchor else { return };
        if !self.drained {
            // A fifo queue drains ONE image per vblank; pacing prevents growth
            // but never shrinks the queue that filled during warmup. Pause once
            // so it empties, then hold it at ~1 deep.
            std::thread::sleep(Duration::from_nanos(10 * self.period_ns));
            self.drained = true;
        }
        let since = anchor.elapsed().as_nanos() as u64;
        let next_vblank_in = (self.period_ns - (since % self.period_ns)) as i64;
        // If already inside the margin window, roll to the FOLLOWING vblank —
        // otherwise we skip the sleep and submit twice per refresh.
        let mut wait = next_vblank_in - self.margin_ns as i64;
        if wait < 0 {
            wait += self.period_ns as i64;
        }
        std::thread::sleep(Duration::from_nanos(wait as u64));
    }
}

// ---------------------------------------------------------------------------
// Scene: a short list of shapes evaluated in one fullscreen fragment shader.
// ---------------------------------------------------------------------------

pub const MAX_SHAPES: usize = 16;

pub const KIND_FILLED_RECT: f32 = 0.0;
pub const KIND_RECT_OUTLINE: f32 = 1.0;
pub const KIND_ELLIPSE: f32 = 2.0;
pub const KIND_RING_ARC: f32 = 3.0;

/// One shape, std140-friendly: three vec4s. Shared with the CPU mirror
/// rasterizer (mirror.rs), which must match the WGSL coverage rules exactly.
#[repr(C)]
#[derive(Clone, Copy, Default, bytemuck::Pod, bytemuck::Zeroable)]
pub struct Shape {
    /// Linear-space premultipliable RGBA.
    pub color: [f32; 4],
    /// Geometry: rect = (x0, y0, x1, y1); ellipse/ring = (cx, cy, rx, ry).
    pub geo: [f32; 4],
    /// misc = (kind, thickness_px, sweep_ratio 0..1, unused).
    pub misc: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct SceneUniforms {
    /// (shape_count, 0, 0, 0)
    pub header: [f32; 4],
    pub shapes: [Shape; MAX_SHAPES],
}

impl SceneUniforms {
    pub fn empty() -> Self {
        Self { header: [0.0; 4], shapes: [Shape::default(); MAX_SHAPES] }
    }

    pub fn shape_count(&self) -> usize {
        (self.header[0] as usize).min(MAX_SHAPES)
    }
}

/// Qt paints in sRGB bytes; the surface is *-Srgb so the shader must output
/// linear values.
fn srgb_u8(c: [i64; 3], alpha: f64) -> [f32; 4] {
    let lin = |v: i64| {
        let s = (v.clamp(0, 255) as f32) / 255.0;
        if s <= 0.04045 {
            s / 12.92
        } else {
            ((s + 0.055) / 1.055).powf(2.4)
        }
    };
    [lin(c[0]), lin(c[1]), lin(c[2]), alpha as f32]
}

struct SceneBuilder {
    uniforms: SceneUniforms,
}

impl SceneBuilder {
    fn new() -> Self {
        Self {
            uniforms: SceneUniforms {
                header: [0.0; 4],
                shapes: [Shape::default(); MAX_SHAPES],
            },
        }
    }

    fn push(&mut self, shape: Shape) {
        let n = self.uniforms.header[0] as usize;
        if n < MAX_SHAPES {
            self.uniforms.shapes[n] = shape;
            self.uniforms.header[0] = (n + 1) as f32;
        }
    }

    fn rect_outline(&mut self, x0: f32, y0: f32, x1: f32, y1: f32, color: [f32; 4], thickness: f32) {
        self.push(Shape {
            color,
            geo: [x0, y0, x1, y1],
            misc: [KIND_RECT_OUTLINE, thickness, 0.0, 0.0],
        });
    }

    fn filled_rect(&mut self, x0: f32, y0: f32, x1: f32, y1: f32, color: [f32; 4]) {
        self.push(Shape {
            color,
            geo: [x0, y0, x1, y1],
            misc: [KIND_FILLED_RECT, 0.0, 0.0, 0.0],
        });
    }

    fn ellipse(&mut self, cx: f32, cy: f32, rx: f32, ry: f32, color: [f32; 4]) {
        self.push(Shape {
            color,
            geo: [cx, cy, rx, ry],
            misc: [KIND_ELLIPSE, 0.0, 0.0, 0.0],
        });
    }

    fn ring_arc(&mut self, cx: f32, cy: f32, radius: f32, thickness: f32, sweep: f32, color: [f32; 4]) {
        self.push(Shape {
            color,
            geo: [cx, cy, radius, radius],
            misc: [KIND_RING_ARC, thickness, sweep, 0.0],
        });
    }
}

/// Build this frame's scene from the trial state — the Rust analogue of the
/// Python renderer() (joystick_intro.py:2694-2836).
fn build_scene(trial: &Trial, w: i64, h: i64) -> SceneUniforms {
    let mut s = SceneBuilder::new();

    let (region_left, region_top, region_w, region_h) = trial.region_bounds_ratios();
    let left_px = (region_left * w as f64) as f32;
    let top_px = (region_top * h as f64) as f32;
    let region_w_px = ((region_w * w as f64) as i64).max(1) as f32;
    let region_h_px = ((region_h * h as f64) as i64).max(1) as f32;
    let min_dim = region_w_px.min(region_h_px) as f64;

    // Region outline, gray 1px (@2710-2712).
    s.rect_outline(
        left_px,
        top_px,
        left_px + region_w_px,
        top_px + region_h_px,
        srgb_u8([120, 120, 120], 1.0),
        1.0,
    );

    // Target (@2714-2750), subject-visible only during start_on.
    if !trial.cfg.cursor_only_mode && trial.state == State::StartOn {
        let t = &trial.current_target;
        let (tx, ty) = trial.to_region_pixels(trial.target_x, trial.target_y, w, h);
        let radius = (t.radius_ratio * min_dim) as i64 as f32;
        let (rgb, opacity) = if trial.cursor_inside_target {
            (t.active_color, t.active_opacity)
        } else {
            (t.color, t.opacity)
        };
        // Python: alpha = round(255 * opacity) on the fill color.
        let alpha = (255.0 * opacity).round() / 255.0;
        s.ellipse(tx as f32, ty as f32, radius, radius, srgb_u8(rgb, alpha));

        // Hold-progress ring (@2724-2750): gated exactly like Python.
        let target_animation_enabled = trial.cfg.animations_enabled && trial.cfg.target_animation_enabled;
        if target_animation_enabled
            && trial.cfg.show_hold_progress_ring
            && trial.hold_progress_ratio > 0.0
        {
            let progress = trial.hold_progress_ratio.clamp(0.0, 1.0);
            let eased = progress * progress * (3.0 - 2.0 * progress);
            let ring_radius = radius + (0.022 * min_dim).max(5.0) as i64 as f32;
            let ring_width = (0.014 * min_dim).max(radius as f64 * 0.18).max(4.0) as i64 as f32;
            // Backdrop ring then the progress arc (simplified from Python's
            // three-outline treatment; same radii/width).
            s.ring_arc(tx as f32, ty as f32, ring_radius, ring_width + 4.0, 1.0,
                srgb_u8([0, 0, 0], 150.0 / 255.0));
            let mut arc_color = srgb_u8(t.active_color, 245.0 / 255.0);
            arc_color[3] = 245.0 / 255.0;
            s.ring_arc(tx as f32, ty as f32, ring_radius, ring_width, eased as f32, arc_color);
        }
    }

    // Cursor (@2798-2801).
    let cursor_diameter = (trial.cfg.cursor_diameter_ratio * min_dim) as i64;
    let r = (cursor_diameter / 2) as f32;
    let (cx, cy) = trial.to_region_pixels(trial.cursor_x, trial.cursor_y, w, h);
    let cc = trial.cfg.cursor_color;
    s.ellipse(
        cx as f32,
        cy as f32,
        r,
        r,
        srgb_u8([cc[0] as i64, cc[1] as i64, cc[2] as i64], 1.0),
    );

    // Photodiode / state square (@2828-2836).
    let edge = STATE_INDICATOR_EDGE_PX as f32;
    let sx = w as f32 - edge - trial.cfg.state_indicator_x as f32;
    let sy = h as f32 - edge - trial.cfg.state_indicator_y as f32;
    let b = trial.state_brightness;
    s.filled_rect(sx, sy, sx + edge, sy + edge, srgb_u8([b, b, b], 1.0));

    s.uniforms
}

/// Idle scene between trials: black field + dark photodiode square. The next
/// run_trial repaints within a frame, mirroring Python's back-to-back runs.
fn idle_scene(w: i64, h: i64, state_indicator_x: i64, state_indicator_y: i64) -> SceneUniforms {
    let mut s = SceneBuilder::new();
    let edge = STATE_INDICATOR_EDGE_PX as f32;
    let sx = w as f32 - edge - state_indicator_x as f32;
    let sy = h as f32 - edge - state_indicator_y as f32;
    s.filled_rect(sx, sy, sx + edge, sy + edge, srgb_u8([0, 0, 0], 1.0));
    s.uniforms
}

const SHADER: &str = r#"
struct Shape {
    color: vec4<f32>,
    geo: vec4<f32>,
    misc: vec4<f32>,
};
struct Scene {
    header: vec4<f32>,
    shapes: array<Shape, 16>,
};
@group(0) @binding(0) var<uniform> scene: Scene;

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4<f32> {
    let x = f32(i32(vi) / 2) * 4.0 - 1.0;
    let y = f32(i32(vi) % 2) * 4.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}

fn coverage(p: vec2<f32>, sh: Shape) -> f32 {
    let kind = sh.misc.x;
    if (kind < 0.5) { // filled rect
        if (p.x >= sh.geo.x && p.x < sh.geo.z && p.y >= sh.geo.y && p.y < sh.geo.w) {
            return 1.0;
        }
        return 0.0;
    }
    if (kind < 1.5) { // rect outline
        let t = sh.misc.y;
        let outer = p.x >= sh.geo.x && p.x < sh.geo.z && p.y >= sh.geo.y && p.y < sh.geo.w;
        let inner = p.x >= sh.geo.x + t && p.x < sh.geo.z - t && p.y >= sh.geo.y + t && p.y < sh.geo.w - t;
        if (outer && !inner) {
            return 1.0;
        }
        return 0.0;
    }
    if (kind < 2.5) { // filled ellipse
        let d = (p - sh.geo.xy) / max(sh.geo.zw, vec2<f32>(0.5, 0.5));
        if (dot(d, d) <= 1.0) {
            return 1.0;
        }
        return 0.0;
    }
    // ring arc: radius sh.geo.z, thickness sh.misc.y, sweep sh.misc.z of a full
    // turn starting at 12 o'clock going clockwise (Qt drawArc 90*16, negative).
    let d = p - sh.geo.xy;
    let dist = length(d);
    let half_t = sh.misc.y * 0.5;
    if (abs(dist - sh.geo.z) > half_t) {
        return 0.0;
    }
    // theta: 0 at up, increasing clockwise (screen y-down).
    let theta = atan2(d.x, -d.y);
    let two_pi = 6.28318530718;
    var pos = theta;
    if (pos < 0.0) {
        pos = pos + two_pi;
    }
    if (pos <= sh.misc.z * two_pi) {
        return 1.0;
    }
    return 0.0;
}

@fragment
fn fs_main(@builtin(position) pos: vec4<f32>) -> @location(0) vec4<f32> {
    var color = vec3<f32>(0.0, 0.0, 0.0); // black field, like the Qt canvas
    let n = i32(scene.header.x);
    for (var i = 0; i < n; i = i + 1) {
        let sh = scene.shapes[i];
        let cov = coverage(pos.xy, sh) * sh.color.a;
        color = mix(color, sh.color.rgb, cov);
    }
    return vec4<f32>(color, 1.0);
}
"#;

// ---------------------------------------------------------------------------
// GPU state
// ---------------------------------------------------------------------------

struct Gpu {
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    pipeline: wgpu::RenderPipeline,
    uniform_buf: wgpu::Buffer,
    bind_group: wgpu::BindGroup,
}

impl Gpu {
    fn new(window: Arc<Window>) -> anyhow::Result<Self> {
        let instance = wgpu::Instance::default();
        let surface = instance.create_surface(window.clone())?;
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: Some(&surface),
            force_fallback_adapter: false,
        }))
        .context("no compatible GPU adapter")?;
        let info = adapter.get_info();
        tracing::info!(name = %info.name, backend = ?info.backend, "adapter");

        let (device, queue) = pollster::block_on(adapter.request_device(
            &wgpu::DeviceDescriptor {
                label: Some("executor"),
                required_features: wgpu::Features::empty(),
                required_limits: wgpu::Limits::downlevel_defaults(),
                memory_hints: wgpu::MemoryHints::default(),
            },
            None,
        ))?;

        let caps = surface.get_capabilities(&adapter);
        let format = caps
            .formats
            .iter()
            .copied()
            .find(|f| f.is_srgb())
            .unwrap_or(caps.formats[0]);
        let size = window.inner_size();
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::Fifo,
            alpha_mode: caps.alpha_modes[0],
            view_formats: vec![],
            desired_maximum_frame_latency: 1,
        };
        surface.configure(&device, &config);

        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("scene"),
            source: wgpu::ShaderSource::Wgsl(SHADER.into()),
        });
        let uniform_buf = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("scene uniforms"),
            size: std::mem::size_of::<SceneUniforms>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: None,
            entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            }],
        });
        let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: None,
            layout: &bgl,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: uniform_buf.as_entire_binding(),
            }],
        });
        let layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: None,
            bind_group_layouts: &[&bgl],
            push_constant_ranges: &[],
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("scene"),
            layout: Some(&layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: "vs_main",
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: "fs_main",
                targets: &[Some(format.into())],
                compilation_options: Default::default(),
            }),
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
            cache: None,
        });

        Ok(Self { surface, device, queue, config, pipeline, uniform_buf, bind_group })
    }

    fn resize(&mut self, width: u32, height: u32) {
        self.config.width = width.max(1);
        self.config.height = height.max(1);
        self.surface.configure(&self.device, &self.config);
    }
}

// ---------------------------------------------------------------------------
// Executor app
// ---------------------------------------------------------------------------

/// Wake-up message for the winit user-event channel.
#[derive(Debug)]
pub enum Wake {
    /// A trial job was queued; pick it up on the next frame.
    Job,
    /// SIGINT/SIGTERM received; exit the event loop between frames so the
    /// X window, swapchain, and core gRPC connections tear down cleanly
    /// (abrupt termination mid-flip is suspected of wedging the session,
    /// 2026-07-02 Ctrl+C freeze).
    Shutdown,
}

pub struct Executor {
    args: ExecutorArgs,
    job_rx: std::sync::mpsc::Receiver<Job>,
    window: Option<Arc<Window>>,
    gpu: Option<Gpu>,
    pacer: Option<FramePacer>,
    active: Option<Job>,
    /// Last idle square offsets (from the most recent trial's config).
    idle_indicator: (i64, i64),
    /// Publishes each frame's shape list for the operator mirror (mirror.rs).
    mirror_tx: tokio::sync::watch::Sender<crate::mirror::MirrorScene>,
}

impl Executor {
    pub fn new(
        args: ExecutorArgs,
        job_rx: std::sync::mpsc::Receiver<Job>,
        mirror_tx: tokio::sync::watch::Sender<crate::mirror::MirrorScene>,
    ) -> Self {
        Self {
            args,
            job_rx,
            window: None,
            gpu: None,
            pacer: None,
            active: None,
            idle_indicator: (30, 70),
            mirror_tx,
        }
    }

    fn create_window(&mut self, event_loop: &ActiveEventLoop) -> anyhow::Result<()> {
        let monitor = event_loop
            .available_monitors()
            .nth(self.args.monitor)
            .context("subject monitor not found")?;
        let mhz = monitor.refresh_rate_millihertz().unwrap_or(0);
        let period_ns = if mhz > 0 { 1_000_000_000_000 / u64::from(mhz) } else { 0 };
        tracing::info!(
            name = monitor.name().unwrap_or_default(),
            size = ?monitor.size(),
            refresh_mhz = mhz,
            "subject display"
        );
        let attrs = if self.args.windowed {
            Window::default_attributes().with_title("joystick_intro (rust)")
        } else {
            use winit::platform::x11::WindowAttributesExtX11;
            Window::default_attributes()
                .with_title("joystick_intro (rust)")
                .with_decorations(false)
                .with_override_redirect(true)
                .with_position(monitor.position())
                .with_inner_size(monitor.size())
        };
        let window = Arc::new(event_loop.create_window(attrs)?);
        window.set_cursor_visible(false);
        self.gpu = Some(Gpu::new(window.clone())?);
        self.pacer = Some(FramePacer::new(period_ns, self.args.pace_margin_ms));
        window.request_redraw();
        self.window = Some(window);
        Ok(())
    }

    /// Advance the active trial by one frame. Returns true when it finished
    /// (successfully or not) and the Job should be dropped.
    fn step_active(&mut self, w: i64, h: i64) -> bool {
        let Some(job) = self.active.as_mut() else { return false };

        // Delegate hung up (operator stopped the task): abort silently.
        if job.event_tx.is_closed() {
            tracing::info!("trial aborted (delegate closed the stream)");
            return true;
        }

        // Operator events.
        while let Ok(op) = job.op_rx.try_recv() {
            let now_s = job.clock.now_python_ns() as f64 / 1e9;
            match op {
                OpEvent::Arrow { key, pressed } => job.trial.on_arrow_key(&key, pressed),
                OpEvent::EndRequested(v) => job.trial.on_end_requested(v),
                OpEvent::Touch { x, y } => job.trial.on_touch(x as i64, y as i64, now_s),
            }
        }
        // Joystick samples (bookkeeping; the live value rides JoystickState).
        while let Ok((t_s, x, y)) = job.sample_rx.try_recv() {
            job.trial.on_joystick_sample(t_s, x, y);
        }

        let (ax, ay) = job.joystick.get();
        let out = job.trial.step(&crate::state::StepInput {
            now_s: job.clock.now_python_ns() as f64 / 1e9,
            analog_x: ax,
            analog_y: ay,
            width_px: w,
            height_px: h,
        });
        for effect in out.effects {
            ship_effect(&job.clock, &job.effect_tx, &job.event_tx, effect);
        }

        if let Some(outcome) = out.done {
            match job.trial.behav.to_json() {
                Ok(json) => {
                    let _ = job.event_tx.send(Ok(TrialEvent {
                        body: Some(Body::BehavResultJson(json)),
                    }));
                }
                Err(e) => {
                    let _ = job.event_tx.send(Err(Status::internal(format!(
                        "behav_result serialization failed: {e}"
                    ))));
                }
            }
            let _ = job.event_tx.send(Ok(TrialEvent {
                body: Some(Body::ConfigUpdatesJson(job.trial.config_updates())),
            }));
            let _ = job.event_tx.send(Ok(TrialEvent {
                body: Some(Body::Success(outcome.success)),
            }));
            self.idle_indicator = (
                job.trial.cfg.state_indicator_x.max(0),
                job.trial.cfg.state_indicator_y.max(0),
            );
            return true;
        }
        false
    }

    fn frame(&mut self) -> anyhow::Result<()> {
        let Some(gpu) = self.gpu.as_mut() else { return Ok(()) };

        // Acquire FIRST (blocks at vsync under fifo) so the state step uses
        // the freshest inputs.
        let t_acquire = Instant::now();
        let frame = match gpu.surface.get_current_texture() {
            Ok(f) => f,
            Err(wgpu::SurfaceError::Lost | wgpu::SurfaceError::Outdated) => {
                let (w, h) = (gpu.config.width, gpu.config.height);
                gpu.resize(w, h);
                return Ok(());
            }
            Err(e) => return Err(e.into()),
        };
        if t_acquire.elapsed() > Duration::from_micros(500) {
            if let Some(p) = self.pacer.as_mut() {
                p.on_vblank(Instant::now());
            }
        }

        // Accept a newly queued trial.
        if self.active.is_none() {
            if let Ok(job) = self.job_rx.try_recv() {
                tracing::info!("trial started");
                self.active = Some(job);
            }
        }

        let (w, h) = (gpu.config.width as i64, gpu.config.height as i64);
        let finished = self.step_active(w, h);
        if finished {
            self.active = None;
            tracing::info!("trial finished");
        }

        let uniforms = match self.active.as_ref() {
            Some(job) => build_scene(&job.trial, w, h),
            None => idle_scene(w, h, self.idle_indicator.0, self.idle_indicator.1),
        };
        // Publish for the operator mirror — a memcpy into a watch cell; the
        // rasterize/encode work happens on tokio threads (mirror.rs).
        self.mirror_tx.send_replace(crate::mirror::MirrorScene {
            uniforms,
            width: w as u32,
            height: h as u32,
        });
        let gpu = self.gpu.as_mut().unwrap();
        gpu.queue
            .write_buffer(&gpu.uniform_buf, 0, bytemuck::bytes_of(&uniforms));

        let view = frame.texture.create_view(&Default::default());
        let mut encoder = gpu.device.create_command_encoder(&Default::default());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: None,
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(&gpu.pipeline);
            pass.set_bind_group(0, &gpu.bind_group, &[]);
            pass.draw(0..3, 0..1);
        }
        gpu.queue.submit([encoder.finish()]);
        let t_present = Instant::now();
        frame.present();
        if t_present.elapsed() > Duration::from_micros(500) {
            if let Some(p) = self.pacer.as_mut() {
                p.on_vblank(Instant::now());
            }
        }
        if let Some(p) = self.pacer.as_mut() {
            p.pace();
        }
        Ok(())
    }
}

impl ApplicationHandler<Wake> for Executor {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }
        if let Err(e) = self.create_window(event_loop) {
            tracing::error!("window/GPU init failed: {e:#}");
            event_loop.exit();
        }
    }

    fn user_event(&mut self, event_loop: &ActiveEventLoop, event: Wake) {
        match event {
            Wake::Job => {
                // A job arrived; the continuous redraw loop will pick it up.
                if let Some(w) = &self.window {
                    w.request_redraw();
                }
            }
            Wake::Shutdown => {
                tracing::info!("shutdown signal received; exiting render loop");
                event_loop.exit();
            }
        }
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _id: WindowId, event: WindowEvent) {
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::Resized(size) => {
                if let Some(gpu) = self.gpu.as_mut() {
                    gpu.resize(size.width, size.height);
                }
            }
            WindowEvent::RedrawRequested => {
                if let Err(e) = self.frame() {
                    tracing::error!("render frame failed: {e:#}");
                    event_loop.exit();
                    return;
                }
                if let Some(w) = &self.window {
                    w.request_redraw();
                }
            }
            _ => {}
        }
    }
}
