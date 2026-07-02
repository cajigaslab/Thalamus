//! M1 latency-floor spike.
//!
//! Opens a fullscreen window on the subject display and toggles the photodiode
//! square (70x70 px, bottom-right, same margins as joystick_intro.py:2828-2836)
//! between black and white every `--toggle-frames` frames. Each toggle's
//! "command time" (CLOCK_MONOTONIC, ns, captured immediately before the GPU
//! work for that frame is encoded) is written to a CSV. Pointing the rig
//! photodiode at the square and recording it (scope or Thalamus analog node)
//! gives the photon time; the per-toggle difference is the command-to-photon
//! latency this whole patch exists to minimize.
//!
//! Run matrix that matters (see rust/README.md M1):
//!   --present-mode fifo       production path, vsync, no tearing
//!   --present-mode mailbox    low latency, no tearing (if supported)
//!   --present-mode immediate  tear-allowed floor
//! ...each on X11 vs Wayland session.
//!
//! Exit: Escape, q, window close, or --seconds elapsing. Frame-interval
//! statistics are printed on exit (a sanity check that we are actually paced at
//! the display rate and not stuttering).

use std::io::Write as _;
use std::sync::Arc;
use std::time::{Duration, Instant};

use anyhow::Context;
use winit::application::ApplicationHandler;
use winit::event::{ElementState, KeyEvent, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::keyboard::{Key, NamedKey};
use winit::window::{Fullscreen, Window, WindowId};

/// Photodiode square geometry — must match joystick_intro.py defaults so the
/// rig's diode, already taped over the Python task's square, needs no move.
const STATE_WIDTH: f64 = 70.0;
const STATE_INDICATOR_X: f64 = 30.0; // right margin
const STATE_INDICATOR_Y: f64 = 70.0; // bottom margin

#[derive(clap::Args, Debug, Clone)]
pub struct SpikeArgs {
    /// Present mode: fifo (vsync, production), mailbox, or immediate (floor).
    #[arg(long, default_value = "fifo")]
    pub present_mode: String,

    /// Monitor index (0-based, order of winit's available_monitors).
    #[arg(long, default_value_t = 0)]
    pub monitor: usize,

    /// Run windowed instead of borderless fullscreen (debugging only —
    /// fullscreen is required for compositor unredirection).
    #[arg(long)]
    pub windowed: bool,

    /// Toggle the photodiode square every N frames (24 @ 240 Hz = 5 Hz square wave).
    #[arg(long, default_value_t = 24)]
    pub toggle_frames: u64,

    /// Stop after this many seconds (0 = run until Escape).
    #[arg(long, default_value_t = 30)]
    pub seconds: u64,

    /// Write per-toggle timestamps to this CSV path.
    #[arg(long, default_value = "spike_toggles.csv")]
    pub csv: String,

    /// Print the monitor indices/names/geometry and exit (no window).
    #[arg(long)]
    pub list_monitors: bool,

    /// Thalamus core endpoint for live photodiode capture. When reachable, the
    /// spike records the diode channel during the run and prints
    /// command-to-photon latency at exit.
    #[arg(long, default_value = "http://localhost:50050")]
    pub thalamus: String,

    /// Node exposing the photodiode ("Node 5" is the rig's channel picker over
    /// the NIDAQ "Analog in").
    #[arg(long, default_value = "Node 5")]
    pub diode_node: String,

    /// Photodiode channel name on that node.
    #[arg(long, default_value = "Photodiode")]
    pub diode_channel: String,

    /// Skip photodiode capture (CSV-only run, e.g. with no core running).
    #[arg(long)]
    pub no_diode: bool,

    /// X11: create the window override-redirect (unmanaged by the WM) sized to
    /// the monitor, instead of WM fullscreen. Tests whether Mutter's refusal to
    /// unredirect is the latency source. Keyboard exit does not work in this
    /// mode — the run ends via --seconds.
    #[arg(long)]
    pub override_redirect: bool,

    /// Frame pacing for fifo: start each frame this many ms BEFORE the
    /// predicted vblank, so the swapchain queue stays ~1 deep instead of the
    /// driver's ~5-frame render-ahead. 0 = free-run (no pacing). ~1.5 is a
    /// sensible start at 240 Hz (frame budget 4.17 ms).
    #[arg(long, default_value_t = 0.0)]
    pub pace_margin_ms: f64,
}

/// Absolute CLOCK_MONOTONIC in ns — same clock as std::time::Instant on Linux
/// and as C++ steady_clock, so these values are directly comparable with
/// Thalamus-side timestamps when the photodiode is recorded through Thalamus.
fn monotonic_ns() -> u64 {
    let mut ts = libc::timespec { tv_sec: 0, tv_nsec: 0 };
    unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut ts) };
    (ts.tv_sec as u64) * 1_000_000_000 + ts.tv_nsec as u64
}

/// Ask the X11 compositor to unredirect us (`_NET_WM_BYPASS_COMPOSITOR=1`).
///
/// On GNOME/Mutter X11 the composited desktop runs on ONE frame clock (the
/// 60 Hz primary on this rig), so a composited window eats a ~16.7 ms
/// compositor quantum regardless of present mode — measured directly on this
/// rig as a uniform +[0,21] ms on both fifo and immediate. Mutter honors this
/// hint and flips the window straight to scanout.
fn bypass_compositor(window: &Window) {
    use raw_window_handle::{HasWindowHandle, RawWindowHandle};
    let Ok(handle) = window.window_handle() else { return };
    let xid = match handle.as_raw() {
        RawWindowHandle::Xlib(h) => h.window,
        RawWindowHandle::Xcb(h) => u64::from(h.window.get()),
        _ => return, // Wayland etc.: no such hint
    };
    let status = std::process::Command::new("xprop")
        .args([
            "-id", &format!("0x{xid:x}"),
            "-f", "_NET_WM_BYPASS_COMPOSITOR", "32c",
            "-set", "_NET_WM_BYPASS_COMPOSITOR", "1",
        ])
        .status();
    match status {
        Ok(s) if s.success() => {
            tracing::info!("compositor bypass requested (_NET_WM_BYPASS_COMPOSITOR=1)")
        }
        other => tracing::warn!(?other, "could not set _NET_WM_BYPASS_COMPOSITOR via xprop"),
    }
}

fn parse_present_mode(s: &str) -> anyhow::Result<wgpu::PresentMode> {
    Ok(match s.to_ascii_lowercase().as_str() {
        "fifo" => wgpu::PresentMode::Fifo,
        "mailbox" => wgpu::PresentMode::Mailbox,
        "immediate" => wgpu::PresentMode::Immediate,
        other => anyhow::bail!("unknown present mode {other:?} (fifo|mailbox|immediate)"),
    })
}

/// Uniforms for the single fullscreen-triangle pipeline. WGSL std140-compatible
/// layout: two vec2 then two vec4 -> offsets 0, 8, 16, 32; size 48.
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
struct Uniforms {
    rect_min: [f32; 2],
    rect_max: [f32; 2],
    fg: [f32; 4],
    bg: [f32; 4],
}

const SHADER: &str = r#"
struct Uniforms {
    rect_min: vec2<f32>,
    rect_max: vec2<f32>,
    fg: vec4<f32>,
    bg: vec4<f32>,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4<f32> {
    // Fullscreen triangle: (-1,-1) (-1,3) (3,-1).
    let x = f32(i32(vi) / 2) * 4.0 - 1.0;
    let y = f32(i32(vi) % 2) * 4.0 - 1.0;
    return vec4<f32>(x, y, 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) pos: vec4<f32>) -> @location(0) vec4<f32> {
    if (pos.x >= u.rect_min.x && pos.x < u.rect_max.x
        && pos.y >= u.rect_min.y && pos.y < u.rect_max.y) {
        return u.fg;
    }
    return u.bg;
}
"#;

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
    fn new(window: Arc<Window>, present_mode: wgpu::PresentMode) -> anyhow::Result<Self> {
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
                label: Some("spike"),
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
        if !caps.present_modes.contains(&present_mode) {
            anyhow::bail!(
                "present mode {present_mode:?} not supported here; available: {:?}",
                caps.present_modes
            );
        }

        let size = window.inner_size();
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode,
            alpha_mode: caps.alpha_modes[0],
            view_formats: vec![],
            // 1 = block until the previous frame is consumed: smallest queue,
            // lowest latency. THE key knob for command-to-photon.
            desired_maximum_frame_latency: 1,
        };
        surface.configure(&device, &config);

        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("spike"),
            source: wgpu::ShaderSource::Wgsl(SHADER.into()),
        });

        let uniform_buf = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("spike uniforms"),
            size: std::mem::size_of::<Uniforms>() as u64,
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
            label: Some("spike"),
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

#[derive(Default)]
struct ToggleRow {
    toggle_idx: u64,
    frame_idx: u64,
    white: bool,
    /// CLOCK_MONOTONIC ns immediately before this frame's GPU work was encoded.
    t_cmd_ns: u64,
    /// CLOCK_MONOTONIC ns after present() returned.
    t_present_ret_ns: u64,
}

struct App {
    args: SpikeArgs,
    window: Option<Arc<Window>>,
    gpu: Option<Gpu>,
    start: Option<Instant>,
    frame_idx: u64,
    white: bool,
    frame_times_ns: Vec<u64>,
    toggles: Vec<ToggleRow>,
    error: Option<anyhow::Error>,
    /// Display refresh period (from the target monitor), for frame pacing.
    period_ns: u64,
    /// Last moment a swapchain acquire/present unblocked — that instant is a
    /// vblank, and anchors the pacing prediction.
    vblank_anchor: Option<Instant>,
    /// One-shot queue drain performed (see pace()).
    drained: bool,
}

impl App {
    fn new(args: SpikeArgs) -> Self {
        Self {
            args,
            window: None,
            gpu: None,
            start: None,
            frame_idx: 0,
            white: false,
            frame_times_ns: Vec::with_capacity(240 * 300),
            toggles: Vec::with_capacity(4096),
            error: None,
            period_ns: 0,
            vblank_anchor: None,
            drained: false,
        }
    }

    /// Sleep until `pace_margin_ms` before the next predicted vblank, keeping
    /// the fifo swapchain queue ~1 frame deep (the driver otherwise lets us
    /// render ~5 frames ahead, adding ~20 ms command-to-photon).
    fn pace(&mut self) {
        if self.args.pace_margin_ms <= 0.0 || self.period_ns == 0 {
            return;
        }
        let Some(anchor) = self.vblank_anchor else { return };
        if !self.drained {
            // A fifo queue drains ONE image per vblank: pacing prevents growth
            // but never shrinks a queue that filled during the unpaced warmup
            // (measured: it stays ~5 deep = ~20 ms forever). Pause submissions
            // for several refreshes once, so the queue empties before the
            // paced steady state begins.
            std::thread::sleep(Duration::from_nanos(10 * self.period_ns));
            self.drained = true;
        }
        let margin = (self.args.pace_margin_ms * 1e6) as i64;
        let since = anchor.elapsed().as_nanos() as u64;
        let next_vblank_in = (self.period_ns - (since % self.period_ns)) as i64;
        // Target: (next vblank - margin). If we are already inside the margin
        // window (the previous frame's work finished just before its vblank),
        // roll over to the FOLLOWING vblank — otherwise we'd skip the sleep
        // and submit twice per refresh, refilling the queue the drain emptied.
        let mut wait = next_vblank_in - margin;
        if wait < 0 {
            wait += self.period_ns as i64;
        }
        std::thread::sleep(Duration::from_nanos(wait as u64));
    }

    fn fail(&mut self, event_loop: &ActiveEventLoop, err: anyhow::Error) {
        self.error = Some(err);
        event_loop.exit();
    }

    fn draw(&mut self) -> anyhow::Result<()> {
        let gpu = self.gpu.as_mut().unwrap();

        // Acquire FIRST: under fifo this is where the driver blocks on vsync.
        // Everything after — especially the toggle decision — happens as late
        // as possible, mirroring the production rule "sample input last".
        // (Stamping t_cmd before a blocking acquire would bill the wait to the
        // display chain and inflate the measured latency.)
        let t_acquire = Instant::now();
        let frame = match gpu.surface.get_current_texture() {
            Ok(f) => f,
            Err(wgpu::SurfaceError::Lost | wgpu::SurfaceError::Outdated) => {
                let (w, h) = (gpu.config.width, gpu.config.height);
                gpu.resize(w, h);
                return Ok(()); // skip this frame; next redraw reacquires
            }
            Err(e) => return Err(e.into()),
        };
        if t_acquire.elapsed() > Duration::from_micros(500) {
            // The unblock instant is (approximately) a vblank.
            self.vblank_anchor = Some(Instant::now());
        }

        // Toggle decision for THIS frame, then stamp the command time. This is
        // the moment the state machine will "decide" things in M3, so it is the
        // start of the command-to-photon interval.
        let is_toggle_frame = self.frame_idx % self.args.toggle_frames == 0;
        if is_toggle_frame {
            self.white = !self.white;
        }
        let t_cmd_ns = monotonic_ns();
        self.frame_times_ns.push(t_cmd_ns);

        let (w, h) = (gpu.config.width as f64, gpu.config.height as f64);
        let x0 = (w - STATE_WIDTH - STATE_INDICATOR_X) as f32;
        let y0 = (h - STATE_WIDTH - STATE_INDICATOR_Y) as f32;
        let level = if self.white { 1.0 } else { 0.0 };
        let uniforms = Uniforms {
            rect_min: [x0, y0],
            rect_max: [x0 + STATE_WIDTH as f32, y0 + STATE_WIDTH as f32],
            fg: [level, level, level, 1.0],
            // Dark gray background like the task's field, distinct from the square.
            bg: [0.1, 0.1, 0.1, 1.0],
        };
        gpu.queue.write_buffer(&gpu.uniform_buf, 0, bytemuck::bytes_of(&uniforms));

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
            // Present blocked on vsync — its return is also a vblank anchor.
            self.vblank_anchor = Some(Instant::now());
        }
        let t_present_ret_ns = monotonic_ns();

        if is_toggle_frame {
            self.toggles.push(ToggleRow {
                toggle_idx: self.toggles.len() as u64,
                frame_idx: self.frame_idx,
                white: self.white,
                t_cmd_ns,
                t_present_ret_ns,
            });
        }
        self.frame_idx += 1;
        Ok(())
    }

    fn write_csv(&self) -> anyhow::Result<()> {
        let mut f = std::fs::File::create(&self.args.csv)
            .with_context(|| format!("creating {}", self.args.csv))?;
        writeln!(f, "# present_mode={} toggle_frames={} monitor={}",
            self.args.present_mode, self.args.toggle_frames, self.args.monitor)?;
        writeln!(f, "# timestamps are CLOCK_MONOTONIC nanoseconds")?;
        writeln!(f, "toggle_idx,frame_idx,white,t_cmd_ns,t_present_ret_ns")?;
        for t in &self.toggles {
            writeln!(f, "{},{},{},{},{}",
                t.toggle_idx, t.frame_idx, t.white as u8, t.t_cmd_ns, t.t_present_ret_ns)?;
        }
        Ok(())
    }

    fn print_stats(&self) {
        if self.frame_times_ns.len() < 3 {
            println!("too few frames for statistics");
            return;
        }
        let mut intervals: Vec<u64> = self
            .frame_times_ns
            .windows(2)
            .map(|w| w[1] - w[0])
            .collect();
        intervals.sort_unstable();
        let pct = |p: f64| intervals[((intervals.len() - 1) as f64 * p) as usize] as f64 / 1e6;
        let total_s = (self.frame_times_ns.last().unwrap() - self.frame_times_ns[0]) as f64 / 1e9;
        let fps = (self.frame_times_ns.len() - 1) as f64 / total_s;
        println!("--- spike results ({} frames, {:.1}s, {} toggles) ---",
            self.frame_times_ns.len(), total_s, self.toggles.len());
        println!("present_mode={}  effective rate: {fps:.2} fps", self.args.present_mode);
        println!(
            "frame interval ms: p50={:.3} p90={:.3} p99={:.3} max={:.3}",
            pct(0.50), pct(0.90), pct(0.99), pct(1.0)
        );
        println!("toggle timestamps -> {}", self.args.csv);
        println!("(command-to-photon = photodiode edge time - t_cmd_ns, per toggle)");
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.args.list_monitors {
            for (i, m) in event_loop.available_monitors().enumerate() {
                let size = m.size();
                let pos = m.position();
                println!(
                    "--monitor {i}: {}  {}x{} @ ({},{})  {:.2} Hz",
                    m.name().unwrap_or_else(|| "?".into()),
                    size.width, size.height, pos.x, pos.y,
                    m.refresh_rate_millihertz().unwrap_or(0) as f64 / 1000.0
                );
            }
            event_loop.exit();
            return;
        }
        if self.window.is_some() {
            return;
        }
        let monitor = event_loop.available_monitors().nth(self.args.monitor);
        if let Some(m) = &monitor {
            let mhz = m.refresh_rate_millihertz().unwrap_or(0);
            if mhz > 0 {
                self.period_ns = 1_000_000_000_000 / u64::from(mhz);
            }
            tracing::info!(
                name = m.name().unwrap_or_default(),
                size = ?m.size(),
                refresh_mhz = mhz,
                "target monitor"
            );
        }
        if self.args.pace_margin_ms > 0.0 && self.period_ns == 0 {
            tracing::warn!("pacing requested but monitor refresh rate unknown; pacing disabled");
        }
        let attrs = if self.args.override_redirect {
            use winit::platform::x11::WindowAttributesExtX11;
            let Some(m) = monitor.clone() else {
                return self.fail(event_loop, anyhow::anyhow!("monitor {} not found", self.args.monitor));
            };
            Window::default_attributes()
                .with_title("thalamus latency spike")
                .with_decorations(false)
                .with_override_redirect(true)
                .with_position(m.position())
                .with_inner_size(m.size())
        } else {
            let fullscreen = if self.args.windowed {
                None
            } else {
                Some(Fullscreen::Borderless(monitor))
            };
            Window::default_attributes()
                .with_title("thalamus latency spike")
                .with_fullscreen(fullscreen)
        };
        let window = match event_loop.create_window(attrs) {
            Ok(w) => Arc::new(w),
            Err(e) => return self.fail(event_loop, e.into()),
        };
        window.set_cursor_visible(false);
        if !self.args.windowed {
            bypass_compositor(&window);
        }

        let present_mode = match parse_present_mode(&self.args.present_mode) {
            Ok(m) => m,
            Err(e) => return self.fail(event_loop, e),
        };
        match Gpu::new(window.clone(), present_mode) {
            Ok(g) => self.gpu = Some(g),
            Err(e) => return self.fail(event_loop, e),
        }
        self.start = Some(Instant::now());
        self.window = Some(window);
        self.window.as_ref().unwrap().request_redraw();
    }

    fn window_event(
        &mut self,
        event_loop: &ActiveEventLoop,
        _id: WindowId,
        event: WindowEvent,
    ) {
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::KeyboardInput {
                event:
                    KeyEvent { logical_key, state: ElementState::Pressed, .. },
                ..
            } => match logical_key {
                Key::Named(NamedKey::Escape) => event_loop.exit(),
                Key::Character(c) if c == "q" => event_loop.exit(),
                _ => {}
            },
            WindowEvent::Resized(size) => {
                if let Some(gpu) = self.gpu.as_mut() {
                    gpu.resize(size.width, size.height);
                }
            }
            WindowEvent::RedrawRequested => {
                if self.gpu.is_none() {
                    return;
                }
                if let Err(e) = self.draw() {
                    return self.fail(event_loop, e);
                }
                if self.args.seconds > 0 {
                    if let Some(start) = self.start {
                        if start.elapsed().as_secs() >= self.args.seconds {
                            event_loop.exit();
                            return;
                        }
                    }
                }
                // Schedule the next frame. Free-run: the driver's blocking
                // acquire/present paces us (but lets ~5 frames queue). With
                // --pace-margin-ms: sleep until just before the next vblank so
                // the queue stays ~1 deep.
                self.pace();
                self.window.as_ref().unwrap().request_redraw();
            }
            _ => {}
        }
    }
}

pub fn run(args: SpikeArgs) -> anyhow::Result<()> {
    // Start the diode capture before the window so it is already streaming
    // when the first toggle lands. Failure to connect is non-fatal: the run
    // degrades to CSV-only (offline diode correlation still possible).
    let recorder = if args.no_diode || args.list_monitors {
        None
    } else {
        Some(super::diode::DiodeRecorder::start(
            args.thalamus.clone(),
            args.diode_node.clone(),
            args.diode_channel.clone(),
        ))
    };

    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(ControlFlow::Poll);
    let mut app = App::new(args);
    event_loop.run_app(&mut app)?;
    if let Some(e) = app.error.take() {
        return Err(e);
    }
    if app.args.list_monitors {
        return Ok(());
    }
    app.write_csv()?;
    app.print_stats();

    if let Some(rec) = recorder {
        match rec.stop() {
            Ok(samples) => {
                let toggles: Vec<(u64, u64, bool)> = app
                    .toggles
                    .iter()
                    .map(|t| (t.toggle_idx, t.t_cmd_ns, t.white))
                    .collect();
                let lat = super::diode::analyze_and_report(&samples, &toggles);
                if !lat.is_empty() {
                    let path = app.args.csv.replace(".csv", "") + "_latency.csv";
                    let mut f = std::fs::File::create(&path)?;
                    writeln!(f, "toggle_idx,white,t_cmd_ns,t_photon_ns,latency_ms")?;
                    for l in &lat {
                        writeln!(f, "{},{},{},{},{:.3}",
                            l.toggle_idx, l.white as u8, l.t_cmd_ns, l.t_photon_ns, l.latency_ms)?;
                    }
                    println!("per-toggle latencies -> {path}");
                }
            }
            Err(e) => {
                tracing::warn!("photodiode capture unavailable ({e}); CSV-only run. \
                     Is the Thalamus core running with the NIDAQ node on? \
                     Use --no-diode to silence this.");
            }
        }
    }
    Ok(())
}
