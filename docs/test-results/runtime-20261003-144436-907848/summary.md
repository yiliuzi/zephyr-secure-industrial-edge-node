# Runtime Smoke Test

Overall: PASS
Samples received: 141
Observation window: 30 seconds

| Transition | Count |
| --- | ---: |
| NORMAL -> WARNING | 4 |
| WARNING -> FAULT | 3 |
| FAULT -> RECOVERY | 3 |
| RECOVERY -> NORMAL | 3 |

## Errors

None observed.

Scope: QEMU smoke test. Does not establish hardware timing,
long-term stability, or behavior under forced queue overload.
