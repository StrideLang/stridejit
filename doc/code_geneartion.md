# Stridejit Code Generation



## Variables

### Domain variables
A domain variable will be delcared as a
[global][1] in the
module and made available only to modules that run in the domain.

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

### Module Variables

Input and output port blocks are passed to the generated function as pointers, allocated in the parent scope and given a name that references the module they belong to, the name of the internal block, an index/identifier for the instance and optionally additional characters to make the name unique in the scope, e.g. ```VarName_Module_001``` 
Output blocks are presented first, followed by input blocks, followed by local variables.

For nested modules, the identifier for the containing module is appended to the variable name, e.g. ```VarName_ChildModule_005_Parent_004```.

The return value of the generated function is currently unused, it could be used for debugging purposes, or could be used in future optimizations moving an output from the function parameters to the function return value.

#### Local variables

Local variables are declared in the parent scope as described above and passed to the generated
function. If variable is not read before it is written too, the compiler could consider and
measure whether placing it in the function stack provides better performance.

### Reaction/Loop local variables
