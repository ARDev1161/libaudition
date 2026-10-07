# ADR-0005: Scheduling belongs to the application

**Status:** Accepted

libacoustic blocks are maximally configurable and do not classify themselves as
"real-time" or "non-real-time" execution units. The embedding application chooses
threading, executor, priority, batching, and inline/offline execution. The library
provides sessions, bounded queues, cancellation, and capability metadata only.
