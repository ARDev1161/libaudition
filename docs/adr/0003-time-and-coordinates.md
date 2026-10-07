# ADR-0003: Time and coordinate conventions

**Status:** Accepted

Use clock-domain-aware nanosecond timestamps and REP-103-like +X forward, +Y left,
+Z up coordinates. Protobuf Timestamp is an integration serialization for UTC,
not the internal time type.
