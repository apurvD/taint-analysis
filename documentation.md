Name: TaeSeo Um and Apurv Sanjay Deshpande

# IFDS Taint Analysis Framework Documentation

This document outlines the structure and purpose of the C++ code implementing an Inter-procedural, Finite, Distributive, Subset (IFDS) taint analysis framework.

## A. Toy IR and Program Representation (`Section A`)

*   **Purpose:** Defines the data structures used to represent the program being analyzed in a simplified Intermediate Representation (IR).
*   **Components:**
    *   `InstType`: Enumerates the different types of instructions supported (e.g., `INST_ASSIGN`, `INST_SOURCE`, `INST_CALL`, `INST_BR`).
    *   `Instruction`: Represents a single instruction, storing its type, left-hand side (result variable), right-hand side (operand variables), and target name (for calls, branches, gotos).
    *   `BasicBlock`: Represents a sequence of instructions that execute sequentially without internal branches. Contains a name and a vector of `Instruction`s.
    *   `Function`: Represents a single function, containing a name, a vector of `BasicBlock`s, and a map for quick lookup of block names to their indices.
    *   `Program`: Represents the entire program as a collection of `Function`s, stored in a map keyed by function name.

## B. Inter-procedural Control Flow Graph (ICFG) (`Section B`)

*   **Purpose:** Defines and builds the Inter-procedural Control Flow Graph (ICFG), which models the flow of control between individual instructions, including across function calls and returns.
*   **Components:**
    *   `NodeID`: Uniquely identifies a specific instruction within the entire program using its function name, basic block index, and instruction index within the block. Includes a hash function (`NodeIDHash`) for use in unordered maps/sets.
    *   `ICFG`: The class responsible for storing and building the ICFG.
        *   `edges`: Stores normal intra-procedural control flow edges (sequential execution, branches, gotos).
        *   `callEdges`: Stores edges connecting call instructions to the entry points of the called functions.
        *   `returnEdges`: Stores edges connecting return instructions in callees back to the instruction immediately following the call site in callers.
        *   `build()`: The core method that iterates through the `Program` structure and populates the `edges`, `callEdges`, and `returnEdges` maps based on instruction types and program structure.
        *   Helper methods (`successorInSameBlock`, `collectReturnNodes`): Assist in finding specific nodes needed during ICFG construction.

## C. IFDS Taint Analysis (`Section C`)

*   **Purpose:** Implements the core IFDS taint analysis algorithm using the previously built ICFG.
*   **Components:**
    *   `TaintSet`: A type alias (`std::unordered_set<std::string>`) representing the set of variable names considered "tainted" at a specific program point.
    *   `AnalysisState`: Stores the results of the analysis, mapping each `NodeID` to its computed `TaintSet`.
    *   `setUnion`, `setEqual`: Helper functions for set manipulation required by the IFDS algorithm.
    *   `IFDSTaintAnalysis`: The main class performing the analysis.
        *   `runAnalysis()`: Executes the worklist-based IFDS algorithm. It initializes the state, populates the worklist, and iteratively processes nodes, applying flow functions and propagating taint information until a fixed point is reached.
        *   `flowFunction()`: Computes the taint set *after* a normal (non-call, non-return) instruction based on the taint set *before* it and the instruction's semantics (e.g., `INST_SOURCE` introduces taint, `INST_ASSIGN` propagates it).
        *   `callFlowFunction()`: Computes the initial taint set at the *entry* of a callee function based on the caller's state and which arguments passed at the call site were tainted. Maps tainted actual parameters to formal parameter names (e.g., `arg_0`).
        *   `returnFlowFunction()`: Computes the taint set in the caller *after* a function call returns, based on the taint set at the callee's `return` instruction. It handles tainting the variable assigned the call's result if the returned value was tainted.
        *   `propagate()`: Updates the `AnalysisState` for a successor node with newly computed taint information and adds the node to the worklist if its `TaintSet` changed.
        *   Helper methods (`getCallNode`, `getCallerInSet`): Assist in retrieving necessary context for inter-procedural flow functions.

## D. Build Example Programs (5 Tests) (`Section D`)

*   **Purpose:** Provides several factory functions (`buildTestProgram1` to `buildTestProgram5`) that construct different `Program` objects.
*   **Details:** Each function creates a small program designed to test specific scenarios of the taint analysis, such as simple local flow, function calls/returns with taint, tainted arguments, conditional branches, and loops.

## E. Printing Results and Main (`Section E`)

*   **Purpose:** Contains the main driver code to run the analysis on the test programs and display the results.
*   **Components:**
    *   `printAnalysisResults()`: Formats and prints the final `AnalysisState` to the console, showing the computed `TaintSet` for each `NodeID`, grouped by function and sorted for readability.
    *   `main()`: The program entry point. It iterates through the test program builders (from Section D), constructs the `Program` and `ICFG`, runs the `IFDSTaintAnalysis`, prints the results using `printAnalysisResults`, reports basic performance metrics (instructions processed, time), and performs simple checks (`[PASS]`/`[FAIL]`) against expected outcomes for each test case.
