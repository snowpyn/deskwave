from deskwave_host.api.server import RateLimiter


def test_rate_limiter_bounds_tracked_identities() -> None:
    limiter = RateLimiter(maximum_identities=3)
    for index in range(10):
        assert limiter.allow(f"pair-request:192.0.2.{index}")
    assert len(limiter._attempts) == 3
