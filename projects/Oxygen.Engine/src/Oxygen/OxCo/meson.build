oxco_inc = include_directories('..')

deps = [base_dep]

oxco_headers = [
    'Algorithms.h',
    'Awaitables.h',
    'Co.h',
    'Concepts/Awaitable.h',
    'Concepts/Awaiter.h',
    'Concepts/Cancellation.h',
    'Concepts/Promise.h',
    'Coroutine.h',
    'Event.h',
    'EventLoop.h',
    'Executor.h',
    'GraphicsExecutor.h',
    'ParkingLot.h',
    'Run.h',
    'SuspendForever.h',
    'TaskCancelledException.h',
    'Yield.h',
    'Detail/AwaitFn.h',
    'Detail/AwaitableAdapter.h',
    'Detail/AwaitableStateChecker.h',
    'Detail/CoAwaitFactory.h',
    'Detail/CoTag.h',
    'Detail/CoRoutineFrame.h',
    'Detail/GetAwaitable.h',
    'Detail/IntrusiveList.h',
    'Detail/MuxHelper.h',
    'Detail/MuxBase.h',
    'Detail/MuxRange.h',
    'Detail/MuxTuple.h',
    'Detail/Optional.h',
    'Detail/ParkingLotImpl.h',
    'Detail/PointerBits.h',
    'Detail/Promise.h',
    'Detail/ProxyFrame.h',
    'Detail/Queue.h',
    'Detail/ReadyAwaitable.h',
    'Detail/Result.h',
    'Detail/ScopeGuard.h',
    'Detail/TaskParent.h',
    'Detail/TaskAwaitable.h',
]

# Define flags
compiler_args = []

# For header-only libraries, we only need to create a dependency
oxco_dep = declare_dependency(
    sources: [oxco_headers],
    include_directories: oxco_inc,
    dependencies: deps,
    compile_args: [compiler_args],
)

# Install headers
install_headers(oxco_headers, subdir: 'OxCo')
