# Stridejit Code Generation

## Domains

### Functions

Domains provide an init and a processing function. The init function might be a noop for the case of one-shot domains.

### Memory

The type of domain determines how memory for that domain is stored.

 * One shot domains: All memory is process function local, so it gets initialized every time the domain ticks.
 * Reactive domains and regular domains can have their memory as external, allocated in the parent context and considered "external" memory, or they can have it as [global][1] depending on configuration options.

Domain data can be passed discretely or packed into a single domain data struct. Initially, all internal instance data will be packed into a struct. An optimization can be explored where some of the data is unpacked into arguments and the performance evauated.
 
#### Domain global variables
A domain variable declared global is made available only to modules that run in the domain.

```
@<Name> = <internal|external> global <datatype> [defaultvalue], [align X]
```

When used in the domain functions a read is called to make them available.

TODO: checkout https://llvm.org/docs/LangRef.html#linkage-types

#### Domain I/O values
These are defined in the domain declarations in the ```ínput``` and ```output```
fields. They are externally allocated and passed to the ```_init``` and 
```_processing``` functions of the domains as reference parameters.

[1]: https://llvm.org/docs/LangRef.html#global-variables

#### Other Domain Variables

Variables that reset at the start of domain execution are placed in the domain function stack. For domains that are not one shot, the memory can be stored in the parent domain, externally or as global. There can be investigation on the best method, and potentially iterative optimization to find the best configuration for domain data (Gemini says: Unpacking them into discrete arguments is only worth it if a specific variable is heavily accessed inside a deeply nested loop, LLVM's LoopVectorizer and LICM (Loop Invariant Code Motion) usually handle this for you.)

## Local Data to code generator instances

In code generators (modules, reactions and loops), any signals or variables that have a definite domain assigned and are written to, and read before a write in the function stream code are considered  stateful and will be declared in that domain scope (as global or external within the code generator instance struct).

### Persistence

Any internal block that is read before it is written to is considered persistent and will be emitted to the scope data struct. They are reset only at domain init and on explicit reset calls.
An internal block that is not persistent, but is read before it is written, must be reset (initialized) on every tick.

### Code Generator instance

Every code generator that is stateful (recursively checking all internal blocks and function instances) will have an associated data struct that can hold the data for all internal and nested persistent data.

Every instance will have its own struct representing its internal persistent data, recursively nesting any internal persistent instances.

## Process

 * For every domain and code generator, evaluate streams and blocks recursively to determine:
  
    *  which blocks are persistent and which are not
    *  generated function signature and argument mapping
    *  The nested structs resulting from the relationships.




## Old info

Input and output port blocks are passed to the generated function as pointers, allocated in the parent scope and given a name that references the module they belong to, the name of the internal block, an index/identifier for the instance and optionally additional characters to make the name unique in the scope, e.g. ```VarName_Module_001``` 
Output blocks are presented first, followed by input blocks, followed by local variables.

For nested modules, the identifier for the containing module is appended to the variable name, e.g. ```VarName_ChildModule_005_Parent_004```.

The return value of the generated function is currently unused, it could be used for debugging purposes, or could be used in future optimizations moving an output from the function parameters to the function return value.

#### Local variables

Local variables are declared in the parent scope as described above and passed to the generated
function. If variable is not read before it is written too, the compiler could consider and
measure whether placing it in the function stack provides better performance.
Input and output blocks are passed by reference.

### Reaction/Loop local variables

Local variables are declared in the function scope, which triggers a reset on
every call.

# Scope

All scope is local, i.e. within the same scope level, or within the same blocks/streams pair of properties inside a code generator.

Inheriting from _DomainMember allows an object to be scoped in the parent scope as well as the root scope.
