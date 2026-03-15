#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <queue>
#include <chrono>
#include <algorithm> // Why need this?: for std::sort
#include <cassert>

/*******************************************************
 * A. Toy IR and Program Representation
 ******************************************************/
enum InstType {
    INST_ASSIGN,
    INST_SOURCE,
    INST_CALL,
    INST_RET,
    INST_PASS,
    INST_BR,   // conditional branch
    INST_GOTO  // unconditional jump
};

// For branch/goto, we store the "target block name" in 'calleeFunc'.
//  - INST_BR means "if condition is nonzero => jump to calleeFunc block"
//    else fall through to next instruction in same block
//  - INST_GOTO always jumps to calleeFunc block, skipping fall-through
struct Instruction {
    InstType type;
    std::string lhs;               // e.g., x in "x = y"
    std::vector<std::string> rhs;  // e.g., y in "x = y" or condition var in branch
    std::string calleeFunc;        // for call=callee name, for branch/goto=target block name

    Instruction() : type(INST_PASS) {}
    Instruction(InstType t, const std::string &l, const std::vector<std::string> &r, 
                const std::string &f = "")
        : type(t), lhs(l), rhs(r), calleeFunc(f) {}
};

struct BasicBlock {
    std::string name;
    std::vector<Instruction> instructions;
    BasicBlock(const std::string &n) : name(n) {}
};

struct Function {
    std::string name;
    std::vector<BasicBlock> blocks;
    // Map block name -> index for quick lookup
    std::unordered_map<std::string, int> blockNameToIndex;

    Function() = default; 
    Function(const std::string &n) : name(n) {}

    void addBlock(const BasicBlock &b) {
        blockNameToIndex[b.name] = (int)blocks.size();
        blocks.push_back(b);
    }
};

struct Program {
    std::unordered_map<std::string, Function> functions;
    void addFunction(const Function &f) {
        functions[f.name] = f;
    }
};

/*******************************************************
 * B. Inter-procedural Control Flow Graph (ICFG)
 ******************************************************/

// NodeID = (functionName, blockIdx, instIdx)
struct NodeID {
    std::string funcName;
    int blockIdx;
    int instIdx;

    NodeID() : blockIdx(-1), instIdx(-1) {}
    NodeID(const std::string &f, int b, int i) : funcName(f), blockIdx(b), instIdx(i) {}

    bool operator==(const NodeID &o) const {
        return funcName == o.funcName && blockIdx == o.blockIdx && instIdx == o.instIdx;
    }
};

struct NodeIDHash {
    std::size_t operator()(const NodeID &n) const {
        auto h1 = std::hash<std::string>()(n.funcName);
        auto h2 = std::hash<int>()(n.blockIdx);
        auto h3 = std::hash<int>()(n.instIdx);
        return (h1 ^ (h2 * 31)) ^ (h3 * 17);
    }
};

class ICFG {
public:
    // Normal edges: from one NodeID to another
    std::unordered_map<NodeID, std::vector<NodeID>, NodeIDHash> edges;
    // For calls
    std::unordered_map<NodeID, std::vector<NodeID>, NodeIDHash> callEdges;
    // For returns
    std::unordered_map<NodeID, std::vector<NodeID>, NodeIDHash> returnEdges;

    // Build all edges for the entire program, including:
    //  - next-instruction edges
    //  - block-fallthrough edges
    //  - branch/goto edges
    //  - call edges
    //  - return edges
    void build(const Program &prog) {
        edges.clear();
        callEdges.clear();
        returnEdges.clear();

        for (auto &fPair : prog.functions) {
            const std::string &fname = fPair.first;
            const Function &fun = fPair.second;

            for (int b = 0; b < (int)fun.blocks.size(); b++) {
                const BasicBlock &block = fun.blocks[b];
                // Build edges for each instruction within this block
                for (int i = 0; i < (int)block.instructions.size(); i++) {
                    NodeID cur(fname, b, i);
                    const Instruction &inst = block.instructions[i];

                    // Next instruction in the same block (fallthrough) 
                    //     if we haven't hit a GOTO or RET.
                    if (i + 1 < (int)block.instructions.size()) {
                        if (inst.type != INST_GOTO && inst.type != INST_RET) {
                            NodeID nxt(fname, b, i + 1);
                            edges[cur].push_back(nxt);
                        }
                    }

                    // Check: If it's a branch, add an edge to the target block's first instruction
                    //     and also to the next instruction in the same block (already done above).
                    if (inst.type == INST_BR) {
                        auto tIt = fun.blockNameToIndex.find(inst.calleeFunc);
                        if (tIt != fun.blockNameToIndex.end()) {
                            int targetBlock = tIt->second;
                            NodeID target(fname, targetBlock, 0);
                            edges[cur].push_back(target);
                        }
                    }

                    // Check: If it's a GOTO, add an edge to the target block's first instruction
                    if (inst.type == INST_GOTO) {
                        auto tIt = fun.blockNameToIndex.find(inst.calleeFunc);
                        if (tIt != fun.blockNameToIndex.end()) {
                            int targetBlock = tIt->second;
                            NodeID target(fname, targetBlock, 0);
                            edges[cur].push_back(target);
                        }
                    }

                    // Check: If it's a CALL, remember the callee's entry node
                    if (inst.type == INST_CALL) {
                        auto fIt = prog.functions.find(inst.calleeFunc);
                        if (fIt != prog.functions.end()) {
                            // Jump to block 0, inst 0 of the callee
                            NodeID calleeEntry(inst.calleeFunc, 0, 0);
                            callEdges[cur].push_back(calleeEntry);
                        }
                    }
                }

                // Check: If the block is non-empty, check the last instruction:
                //     If it's NOT a RETURN or GOTO, we add a "block-fallthrough"
                //     to the next block (b+1) if it exists.
                if (!block.instructions.empty()) {
                    const Instruction &lastInst = block.instructions.back();
                    if (lastInst.type != INST_RET && lastInst.type != INST_GOTO) {
                        if (b + 1 < (int)fun.blocks.size()) {
                            NodeID lastNode(fname, b, (int)block.instructions.size() - 1);
                            NodeID nextBlockStart(fname, b + 1, 0);
                            edges[lastNode].push_back(nextBlockStart);
                        }
                    }
                }
            }
        }

        //  We gotta build returnEdges from callee returns to the caller's "next" instruction.
        //  That means for each call site, we link each "return instruction" in the callee
        //  back to the "instruction after call" in the caller.
        for (auto &callPair : callEdges) {
            NodeID callSite = callPair.first;
            // Next instruction in caller, or invalid if none
            NodeID returnSite = successorInSameBlock(prog, callSite);

            // For each callee entry
            for (NodeID calleeEntry : callPair.second) {
                // Find all returns in that callee
                std::vector<NodeID> calleeReturns = collectReturnNodes(prog, calleeEntry.funcName);
                // Link them back to returnSite
                for (auto &retNode : calleeReturns) {
                    returnEdges[retNode].push_back(returnSite);
                }
            }
        }
    }

private:
    // Get next instruction in same block,,, or invalid if none
    NodeID successorInSameBlock(const Program &prog, const NodeID &n) const {
        auto fIt = prog.functions.find(n.funcName);
        if (fIt == prog.functions.end()) return NodeID();
        const Function &fun = fIt->second;
        if (n.blockIdx >= (int)fun.blocks.size()) return NodeID();
        const BasicBlock &blk = fun.blocks[n.blockIdx];
        if (n.instIdx + 1 < (int)blk.instructions.size()) {
            return NodeID(n.funcName, n.blockIdx, n.instIdx + 1);
        }
        return NodeID(); // no next instr
    }

    // ?Collect all NodeIDs that are return instructions in the given function
    std::vector<NodeID> collectReturnNodes(const Program &prog, const std::string &funcName) const {
        std::vector<NodeID> results;
        auto it = prog.functions.find(funcName);
        if (it == prog.functions.end()) return results;
        const Function &fun = it->second;
        for (int b = 0; b < (int)fun.blocks.size(); b++) {
            const BasicBlock &block = fun.blocks[b];
            for (int i = 0; i < (int)block.instructions.size(); i++) {
                if (block.instructions[i].type == INST_RET) {
                    results.push_back(NodeID(funcName, b, i));
                }
            }
        }
        return results;
    }
};

/*******************************************************
 * C. IFDS Taint Analysis
 ******************************************************/

using TaintSet = std::unordered_set<std::string>;

struct AnalysisState {
    // For each node, which variables are tainted? -> check
    std::unordered_map<NodeID, TaintSet, NodeIDHash> data;
};

TaintSet setUnion(const TaintSet &a, const TaintSet &b) {
    TaintSet r = a;
    for (auto &x : b) {
        r.insert(x);
    }
    return r;
}

bool setEqual(const TaintSet &a, const TaintSet &b) {
    if (a.size() != b.size()) return false;
    for (auto &x : a) {
        if (b.find(x) == b.end()) {
            return false;
        }
    }
    return true;
}

class IFDSTaintAnalysis {
public:
    IFDSTaintAnalysis(const Program &p, const ICFG &i)
        : prog(p), icfg(i) {}

    void runAnalysis() {
        auto startTime = std::chrono::steady_clock::now();

        state.data.clear();
        instructionsProcessed = 0;
        while (!worklist.empty()) { worklist.pop(); }

        // should Initialize: for each function, the entry node has an empty taint set
        for (auto &fp : prog.functions) {
            const Function &fun = fp.second;
            if (!fun.blocks.empty()) {
                NodeID start(fp.first, 0, 0);
                state.data[start] = TaintSet();
                worklist.push(start);
            }
        }

        while (!worklist.empty()) {
            NodeID cur = worklist.front();
            worklist.pop();

            // inSet is what we already know at 'cur' (check)
            TaintSet inSet = state.data[cur];

            // get the instruction
            const Function &fun = prog.functions.at(cur.funcName);
            const BasicBlock &block = fun.blocks[cur.blockIdx];
            const Instruction &inst = block.instructions[cur.instIdx];
            instructionsProcessed++;

            // (1 Normal flow function
            TaintSet outSet = flowFunction(cur, inSet, inst);

            // 2) Normal edges
            auto itE = icfg.edges.find(cur);
            if (itE != icfg.edges.end()) {
                for (auto &succ : itE->second) {
                    propagate(cur, succ, outSet);
                }
            }

            // (3) Call edges -> callee
            auto itC = icfg.callEdges.find(cur);
            if (itC != icfg.callEdges.end()) {
                for (auto &calleeEntry : itC->second) {
                    TaintSet calleeIn = callFlowFunction(cur, calleeEntry, inSet, inst);
                    propagate(cur, calleeEntry, calleeIn);
                }
            }

            // 4 Return edges -> caller
            auto itR = icfg.returnEdges.find(cur);
            if (itR != icfg.returnEdges.end()) {
                for (auto &returnSite : itR->second) {
                    TaintSet callerOut = returnFlowFunction(cur, returnSite, inSet, inst);
                    propagate(cur, returnSite, callerOut);
                }
            }
        }

        auto endTime = std::chrono::steady_clock::now();
        analysisTimeMs = (double)std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    }

    const AnalysisState &getAnalysisState() const { return state; }
    int getInstructionsProcessed() const { return instructionsProcessed; }
    double getAnalysisTimeMs() const { return analysisTimeMs; }

private:
    const Program &prog;
    const ICFG &icfg;
    AnalysisState state;
    std::queue<NodeID> worklist;
    int instructionsProcessed = 0;
    double analysisTimeMs = 0.0;

    TaintSet flowFunction(const NodeID &, const TaintSet &inSet, const Instruction &inst) {
        TaintSet outSet = inSet;
        switch (inst.type) {
            case INST_SOURCE:
                // LHS becomes tainted
                outSet.insert(inst.lhs);
                break;
            case INST_ASSIGN:
                // if RHS is tainted, then LHS is tainted
                if (!inst.rhs.empty()) {
                    if (inSet.find(inst.rhs[0]) != inSet.end()) {
                        outSet.insert(inst.lhs);
                    }
                }
                break;
            case INST_PASS:
            case INST_BR:
            case INST_GOTO:
                // no direct effect on taint
                break;
            case INST_CALL:
                // handled by callFlowFunction/returnFlowFunction
                break;
            case INST_RET:
                // also handled by returnFlowFunction
                break;
        }
        return outSet;
    }

    TaintSet callFlowFunction(const NodeID &, const NodeID &,
                              const TaintSet &inSet, const Instruction &callInst) {
        // If any argument is tainted, callee sees "arg_i" as tainted
        TaintSet calleeIn;
        for (size_t i = 0; i < callInst.rhs.size(); i++) {
            if (inSet.find(callInst.rhs[i]) != inSet.end()) {
                calleeIn.insert("arg_" + std::to_string(i));
            }
        }
        return calleeIn;
    }

    TaintSet returnFlowFunction(const NodeID &exitNode, const NodeID &returnSite,
                                const TaintSet &exitIn, const Instruction &exitInst) {
        // The "caller out" is the caller's inSet plus possible taint from retVal
        TaintSet callerOut = getCallerInSet(returnSite);
        if (exitInst.type == INST_RET && !exitInst.rhs.empty()) {
            std::string retVar = exitInst.rhs[0];
            if (exitIn.find(retVar) != exitIn.end()) {
                // If retVar is tainted, the caller's call LHS becomes tainted
                NodeID callNode = getCallNode(returnSite);
                if (callNode.blockIdx >= 0) {
                    const Function &fn = prog.functions.at(callNode.funcName);
                    const BasicBlock &blk = fn.blocks[callNode.blockIdx];
                    const Instruction &callInst = blk.instructions[callNode.instIdx];
                    callerOut.insert(callInst.lhs);
                }
            }
        }
        return callerOut;
    }

    // find the call instruction that led to returnSite
    NodeID getCallNode(const NodeID &returnSite) {
        if (returnSite.instIdx <= 0) return NodeID();
        NodeID possibleCall(returnSite.funcName, returnSite.blockIdx, returnSite.instIdx - 1);
        const Function &fun = prog.functions.at(returnSite.funcName);
        if (possibleCall.blockIdx >= 0 && possibleCall.blockIdx < (int)fun.blocks.size()) {
            const BasicBlock &block = fun.blocks[possibleCall.blockIdx];
            if (possibleCall.instIdx >= 0 && possibleCall.instIdx < (int)block.instructions.size()) {
                if (block.instructions[possibleCall.instIdx].type == INST_CALL) {
                    return possibleCall;
                }
            }
        }
        return NodeID();
    }

    // get the inSet at the call site
    TaintSet getCallerInSet(const NodeID &returnSite) {
        NodeID callN = getCallNode(returnSite);
        if (callN.blockIdx < 0) return TaintSet();
        auto it = state.data.find(callN);
        if (it != state.data.end()) {
            return it->second;
        }
        return TaintSet();
    }

    void propagate(const NodeID &, const NodeID &to, const TaintSet &newFact) {
        auto it = state.data.find(to);
        if (it == state.data.end()) {
            state.data[to] = newFact;
            worklist.push(to);
        } else {
            TaintSet oldFact = it->second;
            TaintSet uni = setUnion(oldFact, newFact);
            if (!setEqual(oldFact, uni)) {
                state.data[to] = uni;
                worklist.push(to);
            }
        }
    }
};

/*******************************************************
 * D. Build Example Programs (5 Tests)
 ******************************************************/

// Test 1: local taint flow
//   main/b0:
//     x=SOURCE
//     y=x
//     z=PASS
//     return y
Program buildTestProgram1() {
    Function mainFunc("main");
    {
        BasicBlock b0("b0");
        b0.instructions.push_back(Instruction(INST_SOURCE, "x", {}));
        b0.instructions.push_back(Instruction(INST_ASSIGN, "y", {"x"}));
        b0.instructions.push_back(Instruction(INST_PASS, "z", {}));
        b0.instructions.push_back(Instruction(INST_RET, "", {"y"}));
        mainFunc.addBlock(b0);
    }
    Program prog;
    prog.addFunction(mainFunc);
    return prog;
}

// Test 2: call returns tainted
//   callee/b0:
//     w=SOURCE
//     return w
//   main/b0:
//     x=call callee()
//     y=PASS
//     return y
Program buildTestProgram2() {
    Function callee("callee");
    {
        BasicBlock b0("b0");
        b0.instructions.push_back(Instruction(INST_SOURCE, "w", {}));
        b0.instructions.push_back(Instruction(INST_RET, "", {"w"}));
        callee.addBlock(b0);
    }

    Function mainFunc("main");
    {
        BasicBlock b0("b0");
        b0.instructions.push_back(Instruction(INST_CALL, "x", {}, "callee"));
        b0.instructions.push_back(Instruction(INST_PASS, "y", {}));
        b0.instructions.push_back(Instruction(INST_RET, "", {"y"}));
        mainFunc.addBlock(b0);
    }

    Program prog;
    prog.addFunction(callee);
    prog.addFunction(mainFunc);
    return prog;
}

// Test 3: passing tainted arg
//   f/b0:
//     x=arg_0
//     y=x
//     return y
//   main/b0:
//     s=SOURCE
//     z=call f(s)
//     t=PASS
//     return z
Program buildTestProgram3() {
    Function f("f");
    {
        BasicBlock b0("b0");
        b0.instructions.push_back(Instruction(INST_ASSIGN, "x", {"arg_0"}));
        b0.instructions.push_back(Instruction(INST_ASSIGN, "y", {"x"}));
        b0.instructions.push_back(Instruction(INST_RET, "", {"y"}));
        f.addBlock(b0);
    }

    Function mainFunc("main");
    {
        BasicBlock b0("b0");
        b0.instructions.push_back(Instruction(INST_SOURCE, "s", {}));
        b0.instructions.push_back(Instruction(INST_CALL, "z", {"s"}, "f"));
        b0.instructions.push_back(Instruction(INST_PASS, "t", {}));
        b0.instructions.push_back(Instruction(INST_RET, "", {"z"}));
        mainFunc.addBlock(b0);
    }

    Program prog;
    prog.addFunction(f);
    prog.addFunction(mainFunc);
    return prog;
}

// Test 4: branch skipping an instruction
//   main/b0:
//     x=SOURCE
//     BR "",{x},"b1"
//     y=PASS
//     return y
//   main/b1:
//     z=x
//     return z
Program buildTestProgram4() {
    Function mainFunc("main");
    {
        BasicBlock b0("b0");
        b0.instructions.push_back(Instruction(INST_SOURCE, "x", {}));
        b0.instructions.push_back(Instruction(INST_BR, "", {"x"}, "b1"));
        b0.instructions.push_back(Instruction(INST_PASS, "y", {}));
        b0.instructions.push_back(Instruction(INST_RET, "", {"y"}));
        mainFunc.addBlock(b0);
    }
    {
        BasicBlock b1("b1");
        b1.instructions.push_back(Instruction(INST_ASSIGN, "z", {"x"}));
        b1.instructions.push_back(Instruction(INST_RET, "", {"z"}));
        mainFunc.addBlock(b1);
    }

    Program prog;
    prog.addFunction(mainFunc);
    return prog;
}

// Test 5: loop example
//   main/b0:
//     i=0
//     x=PASS
//   main/b1:
//     BR "",{i},"b2"
//     i=1
//     x=SOURCE
//     GOTO "",{},"b1"
//   main/b2:
//     return x
//
// The block-fallthrough from b0->b1 is crucial (because the last inst in b0 is not RET/GOTO).
// Then in b1 we do a loop until i != 0, eventually x=SOURCE => x is tainted, jump to b1 again,
// then take the branch to b2 => return x => x is tainted => [PASS].
Program buildTestProgram5() {
    Function mainFunc("main");
    {
        BasicBlock b0("b0");
        b0.instructions.push_back(Instruction(INST_ASSIGN, "i", {"0"}));
        b0.instructions.push_back(Instruction(INST_PASS, "x", {}));
        mainFunc.addBlock(b0);
    }
    {
        BasicBlock b1("b1");
        b1.instructions.push_back(Instruction(INST_BR, "", {"i"}, "b2"));
        b1.instructions.push_back(Instruction(INST_ASSIGN, "i", {"1"}));
        b1.instructions.push_back(Instruction(INST_SOURCE, "x", {}));
        b1.instructions.push_back(Instruction(INST_GOTO, "", {}, "b1"));
        mainFunc.addBlock(b1);
    }
    {
        BasicBlock b2("b2");
        b2.instructions.push_back(Instruction(INST_RET, "", {"x"}));
        mainFunc.addBlock(b2);
    }

    Program prog;
    prog.addFunction(mainFunc);
    return prog;
}

//make sure all test cases follows the format and the instructions are correct and also matches handwritten output (expected)
/*******************************************************
 * E. Printing Results and Main
 ******************************************************/

void printAnalysisResults(const Program &prog, const AnalysisState &state) {
    // Group NodeIDs by function, then sort by block/inst
    std::map<std::string, std::vector<std::pair<NodeID, TaintSet>>> perFunc;
    for (auto &kv : state.data) {
        perFunc[kv.first.funcName].push_back({kv.first, kv.second});
    }

    for (auto &pf : perFunc) {
        std::cout << "Function " << pf.first << ":\n";
        // Sort by (blockIdx, instIdx)
        std::sort(pf.second.begin(), pf.second.end(),
                  [](auto &a, auto &b){
                      if(a.first.blockIdx < b.first.blockIdx) return true;
                      if(a.first.blockIdx > b.first.blockIdx) return false;
                      return (a.first.instIdx < b.first.instIdx);
                  });
        for (auto &ent : pf.second) {
            auto &nid = ent.first;
            auto &ts  = ent.second;
            std::cout << "  Block " << nid.blockIdx 
                      << ", Inst " << nid.instIdx << " -> {";
            bool first = true;
            for (auto &var : ts) {
                if(!first) std::cout << ", ";
                std::cout << var;
                first = false;
            }
            std::cout << "}\n";
        }
    }
}

int main() {
    {
        std::cout << "=== Test Program 1 ===\n";
        Program p = buildTestProgram1();
        ICFG icfg; 
        icfg.build(p); 
        IFDSTaintAnalysis analysis(p, icfg);
        analysis.runAnalysis();
        printAnalysisResults(p, analysis.getAnalysisState());
        std::cout << "Processed: " << analysis.getInstructionsProcessed() 
                  << " instructions.\nTime (ms): " << analysis.getAnalysisTimeMs() << "\n";

        // Check that y is tainted at the final return in block0, inst3
        NodeID endOfMain("main", 0, 3);
        bool pass = false;
        auto it = analysis.getAnalysisState().data.find(endOfMain);
        if (it != analysis.getAnalysisState().data.end()) {
            pass = (it->second.find("y") != it->second.end());
        }
        std::cout << (pass ? "[PASS]" : "[FAIL]")
                  << " - Expect y is tainted.\n\n";
    }

    {
        std::cout << "=== Test Program 2 ===\n";
        Program p = buildTestProgram2();
        ICFG icfg; 
        icfg.build(p); 
        IFDSTaintAnalysis analysis(p, icfg);
        analysis.runAnalysis();
        printAnalysisResults(p, analysis.getAnalysisState());
        std::cout << "Processed: " << analysis.getInstructionsProcessed() 
                  << " instructions.\nTime (ms): " << analysis.getAnalysisTimeMs() << "\n";

        // Expect x tainted, y not
        NodeID endOfMain("main", 0, 2);
        bool pass = false;
        auto it = analysis.getAnalysisState().data.find(endOfMain);
        if (it != analysis.getAnalysisState().data.end()) {
            bool xTainted = (it->second.find("x") != it->second.end());
            bool yTainted = (it->second.find("y") != it->second.end());
            pass = (xTainted && !yTainted);
        }
        std::cout << (pass ? "[PASS]" : "[FAIL]")
                  << " - Expect x tainted, y not tainted.\n\n";
    }

    {
        std::cout << "=== Test Program 3 ===\n";
        Program p = buildTestProgram3();
        ICFG icfg; 
        icfg.build(p); 
        IFDSTaintAnalysis analysis(p, icfg);
        analysis.runAnalysis();
        printAnalysisResults(p, analysis.getAnalysisState());
        std::cout << "Processed: " << analysis.getInstructionsProcessed() 
                  << " instructions.\nTime (ms): " << analysis.getAnalysisTimeMs() << "\n";

        // Expect s, z tainted
        NodeID endOfMain("main", 0, 3);
        bool pass = false;
        auto it = analysis.getAnalysisState().data.find(endOfMain);
        if (it != analysis.getAnalysisState().data.end()) {
            bool sTainted = (it->second.find("s") != it->second.end());
            bool zTainted = (it->second.find("z") != it->second.end());
            pass = (sTainted && zTainted);
        }
        std::cout << (pass ? "[PASS]" : "[FAIL]")
                  << " - Expect s and z tainted.\n\n";
    }

    {
        std::cout << "=== Test Program 4 (Branch) ===\n";
        Program p = buildTestProgram4();
        ICFG icfg; 
        icfg.build(p); 
        IFDSTaintAnalysis analysis(p, icfg);
        analysis.runAnalysis();
        printAnalysisResults(p, analysis.getAnalysisState());
        std::cout << "Processed: " << analysis.getInstructionsProcessed() 
                  << " instructions.\nTime (ms): " << analysis.getAnalysisTimeMs() << "\n";

        // The final return in block b1 is inst1, returning z = x. x is a SOURCE => z is tainted.
        NodeID retB1("main", 1, 1);
        bool pass = false;
        auto it = analysis.getAnalysisState().data.find(retB1);
        if (it != analysis.getAnalysisState().data.end()) {
            bool zTainted = (it->second.find("z") != it->second.end());
            pass = zTainted;
        }
        std::cout << (pass ? "[PASS]" : "[FAIL]")
                  << " - Expect z is tainted if branch to b1 is taken.\n\n";
    }

    {
        std::cout << "=== Test Program 5 (Loop) ===\n";
        Program p = buildTestProgram5();
        ICFG icfg; 
        icfg.build(p); 
        IFDSTaintAnalysis analysis(p, icfg);
        analysis.runAnalysis();
        printAnalysisResults(p, analysis.getAnalysisState());
        std::cout << "Processed: " << analysis.getInstructionsProcessed()
                  << " instructions.\nTime (ms): " << analysis.getAnalysisTimeMs() << "\n";

        // final return is in block b2, inst0 => return x
        NodeID retB2("main", 2, 0);
        bool pass = false;
        auto it = analysis.getAnalysisState().data.find(retB2);
        if (it != analysis.getAnalysisState().data.end()) {
            bool xTainted = (it->second.find("x") != it->second.end());
            pass = xTainted;
        }
        std::cout << (pass ? "[PASS]" : "[FAIL]")
                  << " - Expect x is tainted after the loop.\n\n";
    }

    return 0;
}