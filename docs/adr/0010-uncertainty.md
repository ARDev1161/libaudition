# ADR-0010: Preserve native uncertainty

**Status:** Accepted

Core remains dependency-light and stores probabilities, raw scores, Gaussian
variance, and spatial covariance explicitly. GTSAM/Eigen or other mathematical
libraries are implementation dependencies of fusion backends, not core dependencies.
