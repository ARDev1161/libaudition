# ADR-0011: Pluggable logging with spdlog default adapter

**Status:** Accepted

Algorithms log through `ILogSink`. A synchronous spdlog adapter is provided for
low-overhead default use. ROS 2 or embedded applications can supply a different sink.
