Project created by: TaeSeo Um and Apurv Deshpande

We have put the entire codebase + testcases into one compileable codefile

HOW TO COMPILE: g++ -std=c++14 ifds_taint.cpp -o ifds_taint
HOW TO RUN: ./ifds_taint

Abstract—This project presents the design and implementation
of a static taint analysis tool targeting a simplified intermediate
representation (IR) in C that incorporates basic control flow
constructs such as branches and loops. Using a fixed-point
dataflow analysis approach, the analyzer tracks the propagation
of tainted data from predefined sources to sinks across control
flow graphs (CFGs). The tool is evaluated for correctness across
a series of manually defined test cases, focusing on taint merging
and loop handling. Performance benchmarks are conducted to
assess scalability with respect to CFG size and variable count,
revealing expected limitations in analysis speed due to repeated
taint map lookups. While the tool demonstrates the feasibility of
taint tracking in simple IRs, it highlights challenges in extending
such methods to real-world codebases.
Index Terms—Taint Analysis, Inter-Procedrural Control Flow
Graph (ICFG), IFDS, Dataflow Analysis, Fixed-Point Iteration
