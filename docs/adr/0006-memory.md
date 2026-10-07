# ADR-0006: Buffer ownership and zero-copy views

**Status:** Accepted

`AudioBuffer` owns normalized float32 PCM and preserves structural invariants.
`AudioView` borrows immutable storage. Channel access supports planar and
interleaved buffers without copying.
