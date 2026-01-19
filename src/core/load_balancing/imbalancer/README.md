# Imbalancer LB Policy (Skeleton)

This is a **scaffolding** load‑balancing (LB) policy named `imbalancer`.
It delegates actual picking to a **child policy** (default: `round_robin`)
and is intended as a starting point for integrating coordinator‑driven logic
into gRPC’s internal LB stack.

## What it does now

- Registers a new LB policy name: `imbalancer`
- Parses config with optional fields:
  - `childPolicy`: string (default `round_robin`)
  - `childPolicyConfig`: object (default `{}`)
- Instantiates the child policy and forwards updates to it

## What you would add

- Hook coordinator state into `UpdateLocked(...)`
- Use metrics (queue delay, capacity) to adjust picking or child policy selection
- Add xDS support if needed

## Example service config

```json
{
  "loadBalancingConfig": [
    {
      "imbalancer": {
        "childPolicy": "round_robin",
        "childPolicyConfig": {}
      }
    }
  ]
}
```

## Build notes

This policy is registered in `grpc_plugin_registry.cc` and compiled by
`grpc_imbalancer/CMakeLists.txt`. Rebuild gRPC after modifying.
