//===--- Passes/FunctionCallFrequency.h - Block and Edge Frequencies ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describe the interface to the pass that calculates function
// invocation and global function call frequencies for a function as described
// in Wu and Larus paper [1].
//
// References:
//
// [1] Youfeng Wu and James R. Larus. 1994. Static branch frequency and
// program profile analysis. In Proceedings of the 27th annual international
// symposium on Microarchitecture (MICRO 27). Association for Computing
// Machinery, New York, NY, USA, 1–11. DOI:https://doi.org/10.1145/192724.192725
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_BOLT_PASSES_FUNCTIONCALLFREQUENCY_H_
#define LLVM_TOOLS_LLVM_BOLT_PASSES_FUNCTIONCALLFREQUENCY_H_

#include "BinaryContext.h"
#include "BinaryFunction.h"
#include "BinaryFunctionCallGraph.h"
#include "Passes/BinaryPasses.h"
#include "Passes/BlockEdgeFrequency.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/raw_ostream.h"
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace llvm {
namespace bolt {

class FunctionCallFrequency : public BinaryFunctionPass {
private:
  static constexpr double EPSILON = 0.001;
  std::unique_ptr<BlockEdgeFrequency> BEF;
  std::unique_ptr<BinaryFunctionCallGraph> CG;
  std::deque<BinaryFunction *> TopologicalCGOrder;

  /// An edge (x, y) indicates that control may flow from function x to
  /// function y (x calls y) and this pair of functions will be used
  /// to index maps and retrieve content of sets.
  typedef std::pair<const BinaryFunction *, const BinaryFunction *>
      FunctionEdge;

  typedef double (FunctionCallFrequency::*FunPtr)(FunctionEdge &CallEdge) const;

  void dumpProfileData(BinaryFunction &Function, raw_ostream &Printer);
  void updateFrequencyValues(BinaryContext &BC);

  void findingMainReachableFunctions(
      BinaryFunction *Head,
      DenseSet<const BinaryFunction *> &MainReachableFunctions);

  /// Holds local function frequencies calculated based only on
  /// intraprocedural calls.
  DenseMap<FunctionEdge, double> LocalEdgeFrequency;

  /// Holds global block frequencies calculated based only on
  /// one call of the main function.
  DenseMap<FunctionEdge, double> GlobalEdgeFrequency;

  typedef std::pair<BinaryBasicBlock *, const MCSymbol *> Edge;
  DenseMap<Edge, double> CallFrequency;

  /// Holds frequencies propagated to the back edges.
  DenseMap<FunctionEdge, double> BackEdgeFrequency;

  /// Holds the loop backedges of a given call graph.
  DenseSet<FunctionEdge> BackEdges;

  /// Holds the loop headers of a given call graph.
  DenseSet<const BinaryFunction *> LoopHeaders;

  /// Holds all functions reachables from head.
  DenseSet<const BinaryFunction *> ReachableFunctions;

  /// Holds functions' invocation frequencies.
  DenseMap<const BinaryFunction *, double> InvocationFrequency;

  /// isVisited - Checks if the function is marked as visited by checking
  /// if it is not in the reachable set.
  bool isVisited(const BinaryFunction *Function) const;

  /// setVisited - Marks the function as visited removing it
  /// from the reachable set.
  void setVisited(const BinaryFunction *Function);

  /// isBackEdge - Checks if the edge is a loop back edge in the call graph.
  bool isBackEdge(const FunctionEdge &CallEdge) const;

  /// isLoopHeader - Checks if the function is a loop header in the call graph.
  bool isLoopHeader(const BinaryFunction *Function) const;

  /// getBackEdgeFrequency - Get updated backedge frequency, if not
  /// found it uses the local edge frequency value.
  double getBackEdgeFrequency(FunctionEdge &CallEdge) const;

  /// getLocalEdgeFrequency - Get local flow edge frequency.
  double getLocalEdgeFrequency(FunctionEdge &CallEdge) const;

  /// getGlobalEdgeFrequency - Get updated global flow edge frequency, if not
  /// found it uses the local edge frequency value.
  double getGlobalEdgeFrequency(FunctionEdge &CallEdge) const;

  /// callGraphVisitor - Auxiliary function that traverses the call
  /// graph in a depth-first order to discover the loop back edges and
  /// the loop headers of the call graph.
  void callGraphVisitor(BinaryFunction *Function,
                        DenseMap<BinaryFunction *, int> &FuncStatus);

  /// findLoopEdgesInfo - Finds all loop back edges and loop headers
  /// within the call graph.
  void findLoopEdgesInfo(BinaryContext &BC, BinaryFunction *MainFunc);

  /// updateLocalCallFrequencies - Calculates the frequency of function
  /// calls based on the local block frequencies, populating a map of
  /// function predecessors/successors. Returns a pointer to the main (entry)
  /// binary function.
  BinaryFunction *updateLocalCallFrequencies(BinaryContext &BC);

  /// tagReachableFunctions - Marks all functions reachable from head function
  /// as not visited.
  void tagReachableFunctions(BinaryFunction *Head);

  /// propagateCallFrequency - Calculates interprocedural (or global) function
  /// invocation frequencies by propagating local frequency of calls on
  /// other functions.
  void propagateCallFrequency(BinaryFunction *Function, BinaryFunction *Head,
                              bool Terminal);

  /// Prints the program's function call graph to a file in the dot format, the
  /// edge weights are added based on the return value of the functions used by
  /// the print dot function.
  void printDot(std::string DotFileName, FunPtr Fun);

  /// clear - Cleans up all the content from the data structs used.
  void clear();

public:
  explicit FunctionCallFrequency(const cl::opt<bool> &PrintPass)
      : BinaryFunctionPass(PrintPass) {
    BEF = llvm::make_unique<BlockEdgeFrequency>(PrintPass);
  }

  const char *getName() const override {
    return "function-call-frequency-inference";
  }

  void runOnFunctions(BinaryContext &BC) override;
};

} // namespace bolt
} // namespace llvm

#endif /* LLVM_TOOLS_LLVM_BOLT_PASSES_FUNCTIONCALLFREQUENCY_H_ */
