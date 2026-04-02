use std::{mem, rc::Rc, task::{RawWaker, RawWakerVTable, Waker}};

pub trait RcWake {
    /// Indicates that the associated task is ready to make progress and should
    /// be `poll`ed.
    ///
    /// This function can be called from an arbitrary thread, including threads which
    /// did not create the `RcWake` based [`Waker`].
    ///
    /// Executors generally maintain a queue of "ready" tasks; `wake` should place
    /// the associated task onto this queue.
    ///
    /// [`Waker`]: std::task::Waker
    fn wake(self: Rc<Self>) {
        Self::wake_by_ref(&self)
    }

    /// Indicates that the associated task is ready to make progress and should
    /// be `poll`ed.
    ///
    /// This function can be called from an arbitrary thread, including threads which
    /// did not create the `RcWake` based [`Waker`].
    ///
    /// Executors generally maintain a queue of "ready" tasks; `wake_by_ref` should place
    /// the associated task onto this queue.
    ///
    /// This function is similar to [`wake`](RcWake::wake), but must not consume the provided data
    /// pointer.
    ///
    /// [`Waker`]: std::task::Waker
    fn wake_by_ref(rc_self: &Rc<Self>);
}

pub(super) fn waker_vtable<W: RcWake + 'static>() -> &'static RawWakerVTable {
    &RawWakerVTable::new(
        clone_rc_raw::<W>,
        wake_rc_raw::<W>,
        wake_by_ref_rc_raw::<W>,
        drop_rc_raw::<W>,
    )
}

/// Creates a [`Waker`] from an `Rc<impl RcWake>`.
///
/// The returned [`Waker`] will call
/// [`RcWake.wake()`](RcWake::wake) if awoken.
pub fn waker<W>(wake: Rc<W>) -> Waker
where
    W: RcWake + 'static,
{
    let ptr = Rc::into_raw(wake).cast::<()>();

    unsafe { Waker::from_raw(RawWaker::new(ptr, waker_vtable::<W>())) }
}

// FIXME: panics on Rc::clone / refcount changes could wreak havoc on the
// code here. We should guard against this by aborting.

#[allow(clippy::redundant_clone)] // The clone here isn't actually redundant.
unsafe fn increase_refcount<T: RcWake + 'static>(data: *const ()) {
    // Retain Rc, but don't touch refcount by wrapping in ManuallyDrop
    let rc = mem::ManuallyDrop::new(unsafe { Rc::<T>::from_raw(data.cast::<T>()) });
    // Now increase refcount, but don't drop new refcount either
    let _rc_clone: mem::ManuallyDrop<_> = rc.clone();
}

// used by `waker_ref`
#[inline(always)]
unsafe fn clone_rc_raw<T: RcWake + 'static>(data: *const ()) -> RawWaker {
    unsafe { increase_refcount::<T>(data) }
    RawWaker::new(data, waker_vtable::<T>())
}

unsafe fn wake_rc_raw<T: RcWake + 'static>(data: *const ()) {
    let rc: Rc<T> = unsafe { Rc::from_raw(data.cast::<T>()) };
    RcWake::wake(rc);
}

// used by `waker_ref`
unsafe fn wake_by_ref_rc_raw<T: RcWake + 'static>(data: *const ()) {
    // Retain Rc, but don't touch refcount by wrapping in ManuallyDrop
    let rc = mem::ManuallyDrop::new(unsafe { Rc::<T>::from_raw(data.cast::<T>()) });
    RcWake::wake_by_ref(&rc);
}

unsafe fn drop_rc_raw<T: RcWake + 'static>(data: *const ()) {
    drop(unsafe { Rc::<T>::from_raw(data.cast::<T>()) })
}