import pathlib

files = {
    'module_in_loop.stride': '''_domainDefinition RootDomain {
	framework: _JitFramework
	rate: -1
    substitutions: [ ]
    domainIncludes: []
    inputs: [ signal List[3] { domain: RootDomain default: 1 type: _IntType } ]
    outputs: [ signal Out { domain: RootDomain type: _IntType } ]
	domainFunctionCode: ''
}
module Equal {
    ports: [ mainOutputPort OutputPort { block: Output }, mainInputPort InputPort { block: Input } ]
    blocks: [ signal Input[2] {domain: OutputPort.domain type: _IntType} switch Output {domain: OutputPort.domain} ]
    streams: [ [Input[0], Input[1]] >> __Equal() >> Output; ]
}
platformModule __Equal@Int_Bool {
    inputs: [_IntType, _IntType]
	outputs: [_SwitchType]
    processing: "%%outtokens:0%% = llvm::icmp eq %%intokens:0%%, %%intokens:1%%"
    inherits: [_Signal]
}
module AddTwo {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [ signal Input { type: _IntType domain: OutputPort.domain } signal Output { type: _IntType domain: OutputPort.domain } ]
    streams: [ Input + 2 >> Output; ]
}
loop TestLoop {
	ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
	blocks: [
		signal Input[InputPort.size] { type: _IntType domain: OutputPort.domain }
		signal Output {default: 0 type: _IntType domain: OutputPort.domain }
		signal Index { default: 0 type: _IntType reset: Reset domain: OutputPort.domain }
		switch Done { default: off domain: OutputPort.domain }
        signal AddResult { type: _IntType domain: OutputPort.domain }
	]
	terminateWhen: Done
	streams: [
		Input[Index] >> AddTwo() >> AddResult;
        AddResult + Output >> Output;
		Index + 1 >> Index;
		[ Index , InputPort.size ] >> Equal() >> Done;
	]
}
List >> TestLoop() >> Out;
''',

    'reaction_in_loop.stride': '''_domainDefinition RootDomain {
	framework: _JitFramework
	rate: -1
    substitutions: [ ]
    domainIncludes: []
    inputs: [ signal List[3] { domain: RootDomain default: 1 type: _IntType } ]
    outputs: [ signal Out { domain: RootDomain type: _IntType } ]
	domainFunctionCode: ''
}
module Equal {
    ports: [ mainOutputPort OutputPort { block: Output }, mainInputPort InputPort { block: Input } ]
    blocks: [ signal Input[2] {domain: OutputPort.domain type: _IntType} switch Output {domain: OutputPort.domain} ]
    streams: [ [Input[0], Input[1]] >> __Equal() >> Output; ]
}
platformModule __Equal@Int_Bool {
    inputs: [_IntType, _IntType]
	outputs: [_SwitchType]
    processing: "%%outtokens:0%% = llvm::icmp eq %%intokens:0%%, %%intokens:1%%"
    inherits: [_Signal]
}
reaction AddTwo {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [ signal Input { type: _IntType domain: OutputPort.domain } signal Output { type: _IntType domain: OutputPort.domain } ]
    streams: [ Input + 2 >> Output; ]
}
loop TestLoop {
	ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
	blocks: [
		signal Input[InputPort.size] { type: _IntType domain: OutputPort.domain }
		signal Output {default: 0 type: _IntType domain: OutputPort.domain }
		signal Index { default: 0 type: _IntType reset: Reset domain: OutputPort.domain }
		switch Done { default: off domain: OutputPort.domain }
        signal AddResult { type: _IntType domain: OutputPort.domain }
	]
	terminateWhen: Done
	streams: [
		Input[Index] >> AddTwo() >> AddResult;
        AddResult + Output >> Output;
		Index + 1 >> Index;
		[ Index , InputPort.size ] >> Equal() >> Done;
	]
}
List >> TestLoop() >> Out;
''',

    'loop_in_loop.stride': '''_domainDefinition RootDomain {
	framework: _JitFramework
	rate: -1
    substitutions: [ ]
    domainIncludes: []
    inputs: [ signal List[3] { domain: RootDomain default: 1 type: _IntType } ]
    outputs: [ signal Out { domain: RootDomain type: _IntType } ]
	domainFunctionCode: ''
}
module Equal {
    ports: [ mainOutputPort OutputPort { block: Output }, mainInputPort InputPort { block: Input } ]
    blocks: [ signal Input[2] {domain: OutputPort.domain type: _IntType} switch Output {domain: OutputPort.domain} ]
    streams: [ [Input[0], Input[1]] >> __Equal() >> Output; ]
}
platformModule __Equal@Int_Bool {
    inputs: [_IntType, _IntType]
	outputs: [_SwitchType]
    processing: "%%outtokens:0%% = llvm::icmp eq %%intokens:0%%, %%intokens:1%%"
    inherits: [_Signal]
}
loop AddTwo {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [
        signal Input { type: _IntType domain: OutputPort.domain }
        signal Output { default: 0 type: _IntType domain: OutputPort.domain }
        switch Done { default: off domain: OutputPort.domain }
        signal Step { default: 0 type: _IntType domain: OutputPort.domain reset: Reset }
    ]
    terminateWhen: Done
    streams: [
        Input + 2 >> Output;
        Step + 1 >> Step;
        [Step, 1] >> Equal() >> Done;
    ]
}
loop TestLoop {
	ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
	blocks: [
		signal Input[InputPort.size] { type: _IntType domain: OutputPort.domain }
		signal Output {default: 0 type: _IntType domain: OutputPort.domain }
		signal Index { default: 0 type: _IntType reset: Reset domain: OutputPort.domain }
		switch Done { default: off domain: OutputPort.domain }
        signal AddResult { type: _IntType domain: OutputPort.domain }
	]
	terminateWhen: Done
	streams: [
		Input[Index] >> AddTwo() >> AddResult;
        AddResult + Output >> Output;
		Index + 1 >> Index;
		[ Index , InputPort.size ] >> Equal() >> Done;
	]
}
List >> TestLoop() >> Out;
''',

    'module_in_reaction.stride': '''_domainDefinition RootDomain {
	framework: _JitFramework
	rate: -1
    substitutions: [ ]
    domainIncludes: []
    inputs: [ signal In {domain: RootDomain type: _RealType} ]
    outputs: [ signal Out {domain: RootDomain type: _RealType} ]
	domainFunctionCode: ''
}
module AddTwo {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [ signal Input { type: _RealType domain: OutputPort.domain } signal Output { type: _RealType domain: OutputPort.domain } ]
    streams: [ Input + 2.0 >> Output; ]
}
reaction TestReaction {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [
        signal Input { type: _RealType domain: OutputPort.domain }
        signal Output { type: _RealType domain: OutputPort.domain }
    ]
    streams: [
        Input >> AddTwo() >> Output;
    ]
}
In >> TestReaction() >> Out;
''',

    'reaction_in_reaction.stride': '''_domainDefinition RootDomain {
	framework: _JitFramework
	rate: -1
    substitutions: [ ]
    domainIncludes: []
    inputs: [ signal In {domain: RootDomain type: _RealType} ]
    outputs: [ signal Out {domain: RootDomain type: _RealType} ]
	domainFunctionCode: ''
}
reaction AddTwo {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [ signal Input { type: _RealType domain: OutputPort.domain } signal Output { type: _RealType domain: OutputPort.domain } ]
    streams: [ Input + 2.0 >> Output; ]
}
reaction TestReaction {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [
        signal Input { type: _RealType domain: OutputPort.domain }
        signal Output { type: _RealType domain: OutputPort.domain }
    ]
    streams: [
        Input >> AddTwo() >> Output;
    ]
}
In >> TestReaction() >> Out;
''',

    'loop_in_reaction.stride': '''_domainDefinition RootDomain {
	framework: _JitFramework
	rate: -1
    substitutions: [ ]
    domainIncludes: []
    inputs: [ signal In {domain: RootDomain type: _IntType} ]
    outputs: [ signal Out {domain: RootDomain type: _IntType} ]
	domainFunctionCode: ''
}
module Equal {
    ports: [ mainOutputPort OutputPort { block: Output }, mainInputPort InputPort { block: Input } ]
    blocks: [ signal Input[2] {domain: OutputPort.domain type: _IntType} switch Output {domain: OutputPort.domain} ]
    streams: [ [Input[0], Input[1]] >> __Equal() >> Output; ]
}
platformModule __Equal@Int_Bool {
    inputs: [_IntType, _IntType]
	outputs: [_SwitchType]
    processing: "%%outtokens:0%% = llvm::icmp eq %%intokens:0%%, %%intokens:1%%"
    inherits: [_Signal]
}
loop AddTwo {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [
        signal Input { type: _IntType domain: OutputPort.domain }
        signal Output { default: 0 type: _IntType domain: OutputPort.domain }
        switch Done { default: off domain: OutputPort.domain }
        signal Step { default: 0 type: _IntType domain: OutputPort.domain reset: Reset }
    ]
    terminateWhen: Done
    streams: [
        Input + 2 >> Output;
        Step + 1 >> Step;
        [Step, 1] >> Equal() >> Done;
    ]
}
reaction TestReaction {
    ports: [ mainInputPort InputPort { block: Input }, mainOutputPort OutputPort { block: Output } ]
    blocks: [
        signal Input { type: _IntType domain: OutputPort.domain }
        signal Output { type: _IntType domain: OutputPort.domain }
    ]
    streams: [
        Input >> AddTwo() >> Output;
    ]
}
In >> TestReaction() >> Out;
'''
}

base_dir = pathlib.Path('C:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/tests/data')
for name, content in files.items():
    (base_dir / name).write_text(content, encoding='utf-8')

print('All 6 files created successfully.')
