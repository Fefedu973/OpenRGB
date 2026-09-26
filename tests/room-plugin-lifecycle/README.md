# Virtual controller lifecycle regression tests

`RegisterVirtualRGBControllerInThread` and its unregister counterpart retain their API5 signatures, but now defer work to the Qt application thread. A later unregister or delete cancels an earlier queued request for that controller. Without a Qt application, or with `NO_GUI`, requests execute synchronously under a recursive operation mutex. No detached worker or GUI-thread join remains.

The API owns an independent shared state. Deferred callbacks keep only that state and a weak entry/revision, never a raw API pointer. API destruction cancels pending events and removes/destroys its wrappers. Registry notifications occur outside the state-data mutex; `PluginManager` obtains the registered-controller snapshot through `GetRegisteredVirtualControllers()`.

Synchronous lifecycle calls made from a different thread require the Qt application event loop. Callers must not join such a caller from the GUI thread. Lifecycle or image-sink callbacks must not delete their own controller reentrantly. As with the existing interface, client code must stop using a controller before asking the API to delete it. Image owners must detach before their own destruction.

Run from an MSVC developer prompt:

```powershell
python tests/room-plugin-lifecycle/run.py --qt-root <Qt-msvc-root> --out <temporary-build-folder>
```

The runner extracts the marked coordinator verbatim from `OpenRGBPluginAPI.cpp` on every run. Only the wrapper and ResourceManager boundaries are faked; the Qt event queue, thread dispatch, ownership, cancellation and mutex logic are production code. It separately compiles/runs `NO_GUI` without Qt headers or libraries. Tests cover pending register/delete, unregister supersession, repeated requests, stale pointers, reentrant notifications, background synchronous dispatch and API-close cancellation. They do not load plugins or access hardware.
