use std::cell::RefCell;
use std::ops::Deref;
use std::rc::{Rc, Weak};
use std::time::Duration;

use opencv::core::{Mat, Point, Scalar};
use opencv::imgproc;

use crate::api::{
    ImageFormat, ImageNode, Node, OnDrop, Request, Json, State, TaskScope, ThalamusAPI,
    run_task,
};

const WIDTH: u64 = 640;
const HEIGHT: u64 = 480;
const RADIUS: i32 = 20;
const FRAME_MS: u64 = 16;

struct BallNodeInner {
    state: State,
    state_connection: RefCell<Option<OnDrop>>,
    api: ThalamusAPI,
    time: RefCell<Duration>,
    task: RefCell<Option<TaskScope>>,
    x: RefCell<f64>,
    y: RefCell<f64>,
    vx: RefCell<f64>,
    vy: RefCell<f64>,
    frame: RefCell<Vec<u8>>,
}

pub struct BallNode {
    inner: Rc<BallNodeInner>,
}

struct ByteDeref<T: Deref<Target = Vec<u8>>> {
    inner: T,
}

impl<T: Deref<Target = Vec<u8>>> Deref for ByteDeref<T> {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        self.inner.as_slice()
    }
}

impl BallNodeInner {
    fn step(&self) {
        let dt = FRAME_MS as f64 / 1000.0;
        let mut x = self.x.borrow_mut();
        let mut y = self.y.borrow_mut();
        let mut vx = self.vx.borrow_mut();
        let mut vy = self.vy.borrow_mut();

        *x += *vx * dt;
        *y += *vy * dt;

        if *x - (RADIUS as f64) < 0.0 {
            *x = RADIUS as f64;
            *vx = vx.abs();
        } else if *x + (RADIUS as f64) > WIDTH as f64 {
            *x = WIDTH as f64 - (RADIUS as f64);
            *vx = -vx.abs();
        }

        if *y - (RADIUS as f64) < 0.0 {
            *y = RADIUS as f64;
            *vy = vy.abs();
        } else if *y + (RADIUS as f64) > HEIGHT as f64 {
            *y = HEIGHT as f64 - (RADIUS as f64);
            *vy = -vy.abs();
        }

        drop(vx);
        drop(vy);

        let cx = *x as i32;
        let cy = *y as i32;
        drop(x);
        drop(y);

        let mut frame = self.frame.borrow_mut();
        frame.fill(255);

        let mut img = Mat::new_rows_cols_with_data_mut(
            HEIGHT as i32,
            WIDTH as i32,
            frame.as_mut_slice(),
        ).expect("Failed to create Mat view");

        imgproc::circle(
            &mut img,
            Point::new(cx, cy),
            RADIUS,
            Scalar::all(0.0),
            -1,
            imgproc::LINE_8,
            0,
        ).expect("Failed to draw circle");
    }

    async fn animation_loop(this_weak: Weak<Self>) {
        let timer = {
            let Some(this) = this_weak.upgrade() else {
                return;
            };
            this.api.create_timer()
        };
        loop {
            timer.sleep(Duration::from_millis(FRAME_MS)).await;
            let Some(this) = this_weak.upgrade() else {
                return;
            };
            this.step();
            this.api.ready();
        }
    }
}

impl ImageNode for BallNode {
    fn plane(&self, _channel: i32) -> impl Deref<Target = [u8]> {
        ByteDeref { inner: self.inner.frame.borrow() }
    }

    fn num_planes(&self) -> u64 {
        1
    }

    fn format(&self) -> ImageFormat {
        ImageFormat::Gray
    }

    fn width(&self) -> u64 {
        WIDTH
    }

    fn height(&self) -> u64 {
        HEIGHT
    }

    fn frame_interval(&self) -> Duration {
        Duration::from_millis(FRAME_MS)
    }
}

impl Node for BallNode {
    fn time(&self) -> Duration {
        *self.inner.time.borrow()
    }

    fn process(&self, handle: Request, _: Json) {
        handle.respond(&Json::from_string(self.inner.api, "{}"));
    }

    fn new(api: ThalamusAPI, state: State) -> Self {
        let mut initial_frame = vec![255u8; (WIDTH * HEIGHT) as usize];
        {
            let mut img = Mat::new_rows_cols_with_data_mut(
                HEIGHT as i32,
                WIDTH as i32,
                initial_frame.as_mut_slice(),
            ).expect("Failed to create Mat view");
            imgproc::circle(
                &mut img,
                Point::new((WIDTH / 2) as i32, (HEIGHT / 2) as i32),
                RADIUS,
                Scalar::all(0.0),
                -1,
                imgproc::LINE_8,
                0,
            ).expect("Failed to draw initial circle");
        }

        let inner = Rc::new(BallNodeInner {
            state: state.clone(),
            state_connection: RefCell::new(None),
            api,
            time: RefCell::new(Duration::from_millis(0)),
            task: RefCell::new(None),
            x: RefCell::new((WIDTH / 2) as f64),
            y: RefCell::new((HEIGHT / 2) as f64),
            vx: RefCell::new(150.0),
            vy: RefCell::new(120.0),
            frame: RefCell::new(initial_frame),
        });

        let weak = Rc::downgrade(&inner);
        *inner.task.borrow_mut() = Some(run_task(async move {
            BallNodeInner::animation_loop(weak).await;
        }));

        BallNode { inner }
    }
}

impl Drop for BallNode {
    fn drop(&mut self) {
        *self.inner.task.borrow_mut() = None;
    }
}
