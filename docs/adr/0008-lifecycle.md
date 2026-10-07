# ADR-0008: Middleware-neutral lifecycle

**Status:** Accepted

No generic configure/activate/deactivate state machine is implemented in the
library. Constructed objects are configured. Stateful stream contracts expose
reset/flush/finalize only when meaningful. ROS 2 lifecycle ownership remains in
the ROS adapter/application.
