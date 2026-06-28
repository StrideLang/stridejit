

# Module with nested Reaction or Loop

```
{
    # Internal persistents first
    Persistent1,
    Persistent2,
    # Then stream instances
    enum NestedGen1 {
        Persistent1,

    }
    enum NestedGen1 {
        Nested

    }
}
```

Parameter layout for generated functions
1. Main Output
2. Property outputs 
3. Main Input
4. Property Inputs
5. External
6. Instance struct


Do we put more in the instance struct?