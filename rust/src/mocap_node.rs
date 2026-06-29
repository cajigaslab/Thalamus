use std::cell::RefCell;
use std::rc::{Rc, Weak};
use std::time::Duration;

use crate::api::{
    MocapNode, Node, Json, State, TaskScope, ThalamusAPI,
    run_task,
};
use crate::ffi::ThalamusMocapSegment;

const FRAME_MS: u64 = 16;

struct IdentityMocapNodeInner {
    api: ThalamusAPI,
    time: RefCell<Duration>,
    task: RefCell<Option<TaskScope>>,
    segments: Vec<ThalamusMocapSegment>,
}

pub struct IdentityMocapNode {
    inner: Rc<IdentityMocapNodeInner>,
}

impl IdentityMocapNodeInner {
    async fn loop_(this_weak: Weak<Self>) {
        let timer = {
            let Some(this) = this_weak.upgrade() else { return };
            this.api.create_timer()
        };
        loop {
            timer.sleep(Duration::from_millis(FRAME_MS)).await;
            let Some(this) = this_weak.upgrade() else { return };
            this.api.ready();
        }
    }
}

impl MocapNode for IdentityMocapNode {
    fn segments(&self) -> impl std::ops::Deref<Target = [ThalamusMocapSegment]> {
        &self.inner.segments[..]
    }

    fn pose_name(&self) -> impl std::ops::Deref<Target = str> {
        "identity"
    }
}

impl Node for IdentityMocapNode {
    fn time(&self) -> Duration {
        *self.inner.time.borrow()
    }

    fn process(&self, handle: crate::api::Request, _: Json) {
        handle.respond(&Json::from_string(self.inner.api, "{}"));
    }

    fn new(api: ThalamusAPI, _state: State) -> Self {
        let segment = ThalamusMocapSegment {
            frame: 0,
            segment_id: 0,
            time: 0,
            position: [0.0, 0.0, 0.0],
            rotation: [0.0, 0.0, 0.0, 1.0],
            actor: 0,
        };

        let inner = Rc::new(IdentityMocapNodeInner {
            api,
            time: RefCell::new(Duration::from_millis(0)),
            task: RefCell::new(None),
            segments: vec![segment],
        });

        let weak = Rc::downgrade(&inner);
        *inner.task.borrow_mut() = Some(run_task(async move {
            IdentityMocapNodeInner::loop_(weak).await;
        }));

        IdentityMocapNode { inner }
    }
}

impl Drop for IdentityMocapNode {
    fn drop(&mut self) {
        *self.inner.task.borrow_mut() = None;
    }
}
