use std::cell::{Ref, RefCell};
use std::ops::Deref;
use std::rc::{Rc, Weak};
use std::time::Duration;

use crate::api::{
    ExtNode, ImageFormat, ImageNode, Node, OnDrop, Request, Json, State, StateValue,
    ThalamusAPI,
};

struct ArucoNodeInner {
    state: State,
    state_connection: RefCell<Option<OnDrop>>,
    api: ThalamusAPI,
    get_node: RefCell<OnDrop>,
    node_ready: RefCell<OnDrop>,
    format: RefCell<ImageFormat>,
    width: RefCell<u64>,
    height: RefCell<u64>,
    frame_interval: RefCell<Duration>,
    time: RefCell<Duration>,
    upstream: RefCell<Option<ExtNode>>
}

pub struct ArucoNode {
    inner: Rc<ArucoNodeInner>,
}

impl ArucoNodeInner {
    fn on_image(&self, node: ExtNode) {
        let Some(image) = node.image() else {
            return;
        };

        if !image.has_image_data() {
            return;
        }

        let _num_planes = image.num_planes() as usize;
        let width = image.width();
        let height = image.height();
        let format = image.format();
        let frame_interval = image.frame_interval();

        *self.width.borrow_mut() = width;
        *self.height.borrow_mut() = height;
        *self.format.borrow_mut() = format;
        *self.frame_interval.borrow_mut() = frame_interval;
        *self.time.borrow_mut() = node.time();

        *self.upstream.borrow_mut() = Some(node);
        //{
        //    let mut planes = self.planes.borrow_mut();
        //    planes.resize_with(num_planes, ImagePlane::default);
        //    for i in 0..num_planes {
        //        planes[i] = image.plane(i as i32);
        //    }
        //}

        self.api.ready();
    }

    fn on_change(self: Rc<Self>, _source: &State, _action: i32, key: StateValue, value: StateValue) {
        let StateValue::String(key_str) = key else {
            return;
        };

        if key_str != "source" {
            return;
        }

        let StateValue::String(source_name) = value else {
            *self.get_node.borrow_mut() = OnDrop::noop();
            *self.node_ready.borrow_mut() = OnDrop::noop();
            return;
        };

        let weak = Rc::downgrade(&self);
        *self.get_node.borrow_mut() = self.api.get_node(
            crate::api::NodeSelector::Name(&source_name),
            move |node| {
                let weak2 = Weak::clone(&weak);
                weak.upgrade().map(|this| {
                    *this.node_ready.borrow_mut() = node.subscribe(move |node| {
                        weak2.upgrade().map(|this| {
                            this.on_image(node);
                        });
                    });
                });
            },
        );
    }
}

impl ImageNode for ArucoNode {
    fn plane(&self, channel: i32) -> impl Deref<Target = [u8]> {
        Ref::map(self.inner.upstream.borrow(), |opt| {
            opt.as_ref().unwrap().image().unwrap().plane(channel)
        })
    }

    fn num_planes(&self) -> u64 {
        self.inner.upstream.borrow().as_ref().unwrap().image().unwrap().num_planes()
    }

    fn format(&self) -> ImageFormat {
        *self.inner.format.borrow()
    }

    fn width(&self) -> u64 {
        *self.inner.width.borrow()
    }

    fn height(&self) -> u64 {
        *self.inner.height.borrow()
    }

    fn frame_interval(&self) -> Duration {
        *self.inner.frame_interval.borrow()
    }

    fn has_image_data(&self) -> bool {
        self.inner.upstream.borrow().as_ref().unwrap().image().unwrap().has_image_data()
    }
}

impl Node for ArucoNode {
    fn time(&self) -> Duration {
        *self.inner.time.borrow()
    }

    fn process(&self, handle: Request, _: Json) {
        handle.respond(&Json::from_string(self.inner.api, "{}"));
    }

    fn new(api: ThalamusAPI, state: State) -> Self {
        let inner = Rc::new(ArucoNodeInner {
            state: state.clone(),
            state_connection: RefCell::new(None),
            api,
            get_node: RefCell::new(OnDrop::noop()),
            node_ready: RefCell::new(OnDrop::noop()),
            format: RefCell::new(ImageFormat::Gray),
            width: RefCell::new(0),
            height: RefCell::new(0),
            frame_interval: RefCell::new(Duration::from_millis(0)),
            time: RefCell::new(Duration::from_millis(0)),
            upstream: RefCell::new(None),
        });

        let change_ref = Rc::downgrade(&inner);
        let callback = move |source: &State, action: i32, key: StateValue, value: StateValue| {
            change_ref.upgrade().map(|val| {
                val.on_change(source, action, key, value);
            });
        };
        *inner.state_connection.borrow_mut() = Some(inner.state.connect(callback));
        state.recap();

        ArucoNode { inner }
    }
}

impl Drop for ArucoNode {
    fn drop(&mut self) {
        *self.inner.node_ready.borrow_mut() = OnDrop::noop();
        *self.inner.get_node.borrow_mut() = OnDrop::noop();
    }
}
