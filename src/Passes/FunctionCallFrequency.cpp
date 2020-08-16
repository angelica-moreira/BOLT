#include "Passes/FunctionCallFrequency.h"
#include "Passes/DataflowInfoManager.h"
#include "Passes/RegAnalysis.h"
#include "llvm/Support/Options.h"
#include <cfloat>

#undef DEBUG_TYPE
#define DEBUG_TYPE "bolt-global-counts"

namespace llvm {
namespace bolt {

bool FunctionCallFrequency::isVisited(const BinaryFunction *Function) const {
  return !(ReachableFunctions.count(Function));
}

void FunctionCallFrequency::setVisited(const BinaryFunction *Function) {
  ReachableFunctions.erase(Function);
}

bool FunctionCallFrequency::isBackEdge(const FunctionEdge &CallEdge) const {
  return BackEdges.count(CallEdge);
}

bool FunctionCallFrequency::isLoopHeader(const BinaryFunction *Function) const {
  return LoopHeaders.count(Function);
}

double
FunctionCallFrequency::getLocalEdgeFrequency(FunctionEdge &CallEdge) const {
  auto It = LocalEdgeFrequency.find(CallEdge);
  if (It != LocalEdgeFrequency.end())
    return It->second;

  return 0.0;
}

double
FunctionCallFrequency::getBackEdgeFrequency(FunctionEdge &CallEdge) const {
  auto It = BackEdgeFrequency.find(CallEdge);
  if (It != BackEdgeFrequency.end())
    return It->second;

  return getLocalEdgeFrequency(CallEdge);
}

double
FunctionCallFrequency::getGlobalEdgeFrequency(FunctionEdge &CallEdge) const {
  auto It = GlobalEdgeFrequency.find(CallEdge);
  if (It != GlobalEdgeFrequency.end())
    return It->second;

  return 0.0;
}

void FunctionCallFrequency::callGraphVisitor(
    BinaryFunction *Function, DenseMap<BinaryFunction *, int> &FuncStatus) {
  // Tags the current function as being visited
  FuncStatus[Function] = 1;

  for (auto CallerID : CG->successors(CG->getNodeId(Function))) {
    BinaryFunction *SuccBF = CG->nodeIdToFunc(CallerID);
    // If the successor is being visited then it is a back edge.

    uint SuccBFStatus = 0;
    auto It = FuncStatus.find(SuccBF);
    if (It != FuncStatus.end()) {
      SuccBFStatus = It->second;
    }

    if (SuccBFStatus == 1) {
      BackEdges.insert(std::make_pair(Function, SuccBF));

      // Here a loop head is the function that is a target of this
      // back edge.
      LoopHeaders.insert(SuccBF);
    }
    // if successor is new, let's visit it
    else if (SuccBFStatus == 0)
      callGraphVisitor(SuccBF, FuncStatus);
  }

  // Tags the current function as visited
  FuncStatus[Function] = 2;
}

void FunctionCallFrequency::findLoopEdgesInfo(BinaryContext &BC,
                                              BinaryFunction *MainFunc) {
  if (!MainFunc || CG->successors(CG->getNodeId(MainFunc)).size() == 0)
    return;

  DenseMap<BinaryFunction *, int> FuncStatus;

  // Tags all the functions as not visited
  auto &BFs = BC.getBinaryFunctions();
  for (auto &BFI : BFs) {
    BinaryFunction &Function = BFI.second;
    FuncStatus[&Function] = 0;
  }

  callGraphVisitor(MainFunc, FuncStatus);

  for (auto &BFI : BFs) {
    BinaryFunction &Function = BFI.second;
    auto It = FuncStatus.find(&Function);
    if (It != FuncStatus.end()) {
      uint BFStatus = It->second;
      if (BFStatus != 0)
        continue;
      callGraphVisitor(&Function, FuncStatus);
    }
  }
}

BinaryFunction *
FunctionCallFrequency::updateLocalCallFrequencies(BinaryContext &BC) {
  DEBUG(dbgs() << "Block and Edge Frequencies calculated.\n");
  BinaryFunction *MainFunc = nullptr;

  auto &BFs = BC.getBinaryFunctions();
  for (auto &BFI : BFs) {
    BinaryFunction &Function = BFI.second;

    if (Function.getPrintName().compare("main") == 0) {
      MainFunc = &Function;
    }

    if (!Function.hasValidProfile())
      continue;

    DEBUG(dbgs() << "\n\nUpdating local call frequencies for function "
                 << Function.getPrintName() << " \n";);

    // if (Function.getPrintName().compare("main") == 0) {
    //   MainFunc = &Function;
    // }

    // Searching for function calls inside the basic blocks
    for (auto &BB : Function) {
      double BlockFreq = BEF->getLocalBlockFrequency(&BB);
      for (auto &Inst : BB) {
        if (!BC.MIB->isCall(Inst))
          continue;

        if (auto *CalleeSymbol = BC.MIB->getTargetSymbol(Inst)) {
          auto *CalleeBF = BC.getFunctionForSymbol(CalleeSymbol);
          if (!CalleeBF->hasValidProfile())
            continue;
          FunctionEdge CallEdge = std::make_pair(&Function, CalleeBF);

          double LocalFreq = 0;
          auto It = LocalEdgeFrequency.find(CallEdge);
          if (It != LocalEdgeFrequency.end()) {
            LocalFreq = It->second;
          }

          uint64_t CallEdgeFreq = LocalFreq + BlockFreq;
          CallEdgeFreq = (CallEdgeFreq == UINT64_MAX) ? 0 : CallEdgeFreq;
          LocalEdgeFrequency[CallEdge] = CallEdgeFreq;

          // Printing the local function call edge frequency for debugging
          // purposes.
          DEBUG(dbgs() << "  Local Function Call Edge Frequency[("
                       << Function.getPrintName() << ", "
                       << CalleeBF->getPrintName() << ")] = "
                       << format("%.3f", LocalEdgeFrequency[CallEdge])
                       << "\n\n";);
        }
      }
    }
  }
  return MainFunc;
}

void FunctionCallFrequency::tagReachableFunctions(BinaryFunction *Head) {
  ReachableFunctions.clear();

  if (!Head)
    return;

  SmallVector<BinaryFunction *, 16> CallStack;

  // Added the function head into the stack.
  CallStack.push_back(Head);

  // Traversing all children in depth-first fashion and
  // marking them as not visited.
  while (!CallStack.empty()) {
    BinaryFunction *CurrentBF = CallStack.pop_back_val();
    if (!(ReachableFunctions.insert(CurrentBF)).second)
      continue;

    // Only process functions that have body
    if (CurrentBF->empty())
      continue;

    // Adding the new successors into the call stack.
    for (auto CallerID : CG->successors(CG->getNodeId(CurrentBF))) {
      BinaryFunction *SuccBF = CG->nodeIdToFunc(CallerID);
      CallStack.push_back(SuccBF);
    }
  }
}

void FunctionCallFrequency::findingMainReachableFunctions(
    BinaryFunction *Head,
    DenseSet<const BinaryFunction *> &MainReachableFunctions) {
  SmallVector<BinaryFunction *, 16> CallStack;

  // Added the function head into the stack.
  CallStack.push_back(Head);

  // Traversing all children in depth-first fashion and
  // marking them as not visited.
  while (!CallStack.empty()) {
    BinaryFunction *CurrentBF = CallStack.pop_back_val();
    if (!(MainReachableFunctions.insert(CurrentBF)).second)
      continue;

    // Only process functions that have body
    if (CurrentBF->empty())
      continue;

    // Adding the new successors into the call stack.
    for (auto CallerID : CG->successors(CG->getNodeId(CurrentBF))) {
      BinaryFunction *SuccBF = CG->nodeIdToFunc(CallerID);
      CallStack.push_back(SuccBF);
    }
  }
}

void FunctionCallFrequency::propagateCallFrequency(BinaryFunction *Function,
                                                   BinaryFunction *Head,
                                                   bool Terminal) {

  DEBUG(dbgs() << "\n\n=============== Propagate Call Frequency: \n"
               << " Head: " << Head->getPrintName() << ", Current Function: "
               << Function->getPrintName() << ", Final? "
               << (Terminal ? "yes" : "no") << ")==============\n";);

  // Checks if the function has been visited.
  if (isVisited(Function))
    return;

  /// 1. Finds the function's invocation frequency

  // Checks if the edge departing from each predecessor of the current function
  // being analyzed was previously processed.
  for (auto CallerID : CG->predecessors(CG->getNodeId(Function))) {
    BinaryFunction *PredBF = CG->nodeIdToFunc(CallerID);

    FunctionEdge CallEdge = std::make_pair(PredBF, Function);
    if (!isVisited(PredBF) && !isBackEdge(CallEdge)) {
      // There is an unprocessed predecessor edge.
      return;
    }
  }

  /// Calculate all incoming edges frequencies and cyclic frequencies for
  /// loops in the call graph.

  double InvocationFreq = (Function == Head) ? 1.0 : 0.0;

  DEBUG(dbgs() << "\n\nCalculating the incoming edges frequencies\n"
               << "CURRENT INVOCATION FREQ: " << format("%.3f", InvocationFreq)
               << "\n";);

  double CyclicFrequency = 0.0;
  for (auto CallerID : CG->predecessors(CG->getNodeId(Function))) {
    BinaryFunction *PredBF = CG->nodeIdToFunc(CallerID);

    FunctionEdge CallEdge = std::make_pair(PredBF, Function);

    // Consider the CyclicFrequency only in the last call to propagate
    // frequency.
    if (Terminal && isBackEdge(CallEdge)) {

      DEBUG(dbgs() << "CURRENT EDGE FREQUENCY:\n "
                   << CallEdge.first->getPrintName() << " -> "
                   << CallEdge.second->getPrintName() << " : "
                   << format("%.3f", CyclicFrequency) << "\n";);

      CyclicFrequency += getBackEdgeFrequency(CallEdge);

      CyclicFrequency = (CyclicFrequency == UINT64_MAX) ? 0 : CyclicFrequency;

      DEBUG(dbgs() << "UPDATED EDGE FREQUENCY:\n "
                   << CallEdge.first->getPrintName() << " -> "
                   << CallEdge.second->getPrintName() << " : "
                   << format("%.3f", CyclicFrequency) << "\n";);
    } else if (!isBackEdge(CallEdge)) {

      DEBUG(dbgs() << "CURRENT INVOCATION FREQUENCY:\n "
                   << CallEdge.first->getPrintName() << " -> "
                   << CallEdge.second->getPrintName() << " : "
                   << format("%.3f", InvocationFreq) << "\n";);

      double CurrentGlobalEdgeFreq = getGlobalEdgeFrequency(CallEdge);
      CurrentGlobalEdgeFreq = (CurrentGlobalEdgeFreq == DBL_MAX)
                                ? 0
                                : CurrentGlobalEdgeFreq;

      InvocationFreq += CurrentGlobalEdgeFreq;

      DEBUG(dbgs() << "UPDATED INVOCATION FREQUENCY:\n "
                   << CallEdge.first->getPrintName() << " -> "
                   << CallEdge.second->getPrintName() << " : "
                   << format("%.3f", InvocationFreq) << "\n";);
    }
  }

  // For a loop that terminates, the cyclic frequency is less than one.
  // If a loop seems not to terminate the cyclic frequency is higher than
  // one. Since the algorithm does not work as supposed to if the frequency
  // is higher than one, we need to set it to the maximum value offset by
  // the  constant EPSILON.
  if (CyclicFrequency > (1.0 - EPSILON))
    CyclicFrequency = 1.0 - EPSILON;

  InvocationFreq = InvocationFreq / (1.0 - CyclicFrequency);

  InvocationFreq = (InvocationFreq == DBL_MAX) ? 0 : InvocationFreq;

  InvocationFrequency[Function] = InvocationFreq;

  DEBUG(dbgs() << " Invocation Frequency[" << Function->getPrintName()
               << "]:  " << format("%.3f", InvocationFreq) << "\n";);

  /// 2. Calculate global call frequencies for functions' out edges

  // Tagging the function as visited.
  setVisited(Function);

  // Only process functions that have body
  if (Function->empty())
    return;

  for (auto CallerID : CG->successors(CG->getNodeId(Function))) {
    BinaryFunction *SuccBF = CG->nodeIdToFunc(CallerID);
    FunctionEdge CallEdge = std::make_pair(Function, SuccBF);

    // Calculate the global call frequency for this edge.
    double LocalFreq = 0;
    auto It = LocalEdgeFrequency.find(CallEdge);
    if (It != LocalEdgeFrequency.end()) {
      LocalFreq = It->second;
    }

    double GFreq = LocalFreq * InvocationFreq;

    GFreq = (GFreq == DBL_MAX) ? 0 : GFreq;

    GlobalEdgeFrequency[CallEdge] = GFreq;

    DEBUG(dbgs() << "  Global Edge Frequency[(" << Function->getPrintName()
                 << "->" << SuccBF->getPrintName()
                 << ")] = " << format("%.3f", GFreq) << "\n";);

    // Update the backedge frequency so it can be used by
    // outer loops to calculate cyclic frequency of inner loops.
    if (!Terminal && SuccBF == Head)
      BackEdgeFrequency[CallEdge] = GFreq;
  }

  /// 3. Propagate calculated call frequency to the successors
  /// that are not back edges.
  for (auto CallerID : CG->successors(CG->getNodeId(Function))) {
    BinaryFunction *SuccBF = CG->nodeIdToFunc(CallerID);
    // Check if it is a back edge.
    if (!isBackEdge(std::make_pair(Function, SuccBF)))
      propagateCallFrequency(SuccBF, Head, Terminal);
  }
}

void FunctionCallFrequency::updateFrequencyValues(BinaryContext &BC) {
  StringRef FileName = "globalFrequencies.fdata";

  DEBUG(dbgs() << "BOLT-DEBUG: dumping global static infered frequencies to "
               << FileName << "\n";
        std::error_code EC;
        raw_fd_ostream Printer(FileName, EC, sys::fs::F_None); if (EC) {
          errs() << "BOLT-WARNING: " << EC.message() << ", unable to open "
                 << FileName << " for output.\n";
          return;
        });

  auto &BFs = BC.getBinaryFunctions();
  for (auto &BFI : BFs) {
    BinaryFunction &Function = BFI.second;

    if (!Function.hasValidProfile())
      continue;

    // Obtain call frequency.
    double CallFreq = 0;
    auto It = InvocationFrequency.find(&Function);
    if (It != InvocationFrequency.end()) {
      CallFreq = It->second;
    }

    Function.setExecutionCount(round(CallFreq));

    for (auto &BB : Function) {
      double BBFreq = BEF->getLocalBlockFrequency(&BB);
      uint64_t BBCount = round(CallFreq * BBFreq);

      BBCount = (BBCount == UINT64_MAX) ? 0 : BBCount;

      BB.setExecutionCount(BBCount);

      auto LastInst = BB.getLastNonPseudoInstr();
      for (auto &Inst : BB) {
        if (!BC.MIB->isCall(Inst) && !BC.MIB->isBranch(Inst) &&
            !BC.MIB->isReturn(Inst) && LastInst != (&Inst))
          continue;

        auto Offset = BC.MIB->tryGetAnnotationAs<uint64_t>(Inst, "Offset");
        if (!Offset)
          continue;

        if (BC.MIB->isCall(Inst)) {
          auto *CalleeSymbol = BC.MIB->getTargetSymbol(Inst);
          if (!CalleeSymbol)
            continue;

          StringRef CallAnnotation = "Count";
          if (BC.MIB->getConditionalTailCall(Inst)) {
            CallAnnotation = "CTCTakenCount";
          }

          BEF->updateCallFrequency(BC, Inst, CallAnnotation, CallFreq, BBCount);
        }
      }
      for (BinaryBasicBlock *SuccBB : BB.successors()) {
        double BlockEdgeFreq =
            CallFreq * BEF->getLocalEdgeFrequency(&BB, SuccBB);
        uint64_t TakenFreqEdge = round(BlockEdgeFreq);

        TakenFreqEdge = (TakenFreqEdge == UINT64_MAX) ? 0 : TakenFreqEdge;

        // Updates the branch edge frequency.
        BB.setSuccessorBranchInfo(*SuccBB, TakenFreqEdge, 0);
      }
    }
    DEBUG(std::error_code EC;
          raw_fd_ostream FilePrinter(FileName, EC, sys::fs::F_Append); if (EC) {
            dbgs() << "BOLT-ERROR: " << EC.message() << ", unable to open"
                   << " " << FileName << " for output.\n";
            return;
          } dumpProfileData(Function, FilePrinter););
  }
}

void FunctionCallFrequency::dumpProfileData(BinaryFunction &Function,
                                            raw_ostream &Printer) {
  BinaryContext &BC = Function.getBinaryContext();

  std::string FromFunName = Function.getPrintName();
  for (auto &BB : Function) {
    auto LastInst = BB.getLastNonPseudoInstr();
    for (auto &Inst : BB) {
      if (!BC.MIB->isCall(Inst))
        continue;

      auto Offset = BC.MIB->tryGetAnnotationAs<uint64_t>(Inst, "Offset");

      if (!Offset)
        continue;

      uint64_t TakenFreqEdge = 0;
      uint64_t NotTakenFreqEdge = 0;
      auto FromBb = Offset.get();
      std::string ToFunName;
      uint32_t ToBb;

      auto *CalleeSymbol = BC.MIB->getTargetSymbol(Inst);
      if (!CalleeSymbol)
        continue;

      ToFunName = CalleeSymbol->getName();
      ToBb = 0;

      StringRef CallAnnotation = "Count";
      if (BC.MIB->getConditionalTailCall(Inst)) {
        CallAnnotation = "CTCTakenCount";
      }

      BEF->getCallFrequency(BC, Inst, CallAnnotation, TakenFreqEdge);

      if (TakenFreqEdge > 0)
        Printer << "1 " << FromFunName << " " << Twine::utohexstr(FromBb)
                << " 1 " << ToFunName << " " << Twine::utohexstr(ToBb) << " "
                << NotTakenFreqEdge << " " << TakenFreqEdge << "\n";
    }

    if (!LastInst)
      continue;

    auto Offset = BC.MIB->tryGetAnnotationAs<uint64_t>(*LastInst, "Offset");

    if (!Offset)
      continue;

    uint64_t TakenFreqEdge = 0;
    uint64_t NotTakenFreqEdge = 0;
    auto FromBb = Offset.get();
    std::string ToFunName;
    uint32_t ToBb;
    for (BinaryBasicBlock *SuccBB : BB.successors()) {
      TakenFreqEdge = BB.getBranchInfo(*SuccBB).Count;

      TakenFreqEdge = (TakenFreqEdge == UINT64_MAX) ? 0 : TakenFreqEdge;

      BinaryFunction *ToFun = SuccBB->getFunction();
      ToFunName = ToFun->getPrintName();
      ToBb = SuccBB->getInputOffset();

      if (TakenFreqEdge > 0)
        Printer << "1 " << FromFunName << " " << Twine::utohexstr(FromBb)
                << " 1 " << ToFunName << " " << Twine::utohexstr(ToBb) << " "
                << NotTakenFreqEdge << " " << TakenFreqEdge << "\n";
    }
  }
}

void FunctionCallFrequency::clear() {
  LocalEdgeFrequency.clear();
  GlobalEdgeFrequency.clear();
  BackEdgeFrequency.clear();
  BackEdges.clear();
  LoopHeaders.clear();
  ReachableFunctions.clear();
  InvocationFrequency.clear();
  BEF->clear();
}

void FunctionCallFrequency::printDot(std::string DotFileName, FunPtr Fun) {
  FILE *File = fopen(DotFileName.c_str(), "wt");
  if (!File)
    return;
  fprintf(File, "digraph g {\n");
  for (auto *Head : TopologicalCGOrder) {
    fprintf(File, "f%llu [label=\"%s\\nSize=%lu\"];\n", Head->getAddress(),
            Head->getPrintName().c_str(), Head->size());
  }

  for (auto *Head : TopologicalCGOrder) {
    for (auto CallerID : CG->successors(CG->getNodeId(Head))) {
      BinaryFunction *SuccBF = CG->nodeIdToFunc(CallerID);
      FunctionEdge CallEdge = std::make_pair(Head, SuccBF);
      fprintf(File,
              "f%llu -> f%llu [label=\"Frequency=%lf\"];"
              "\n",
              Head->getAddress(), SuccBF->getAddress(), (this->*Fun)(CallEdge));
    }
  }
  fprintf(File, "}\n");
  fclose(File);
}

void FunctionCallFrequency::runOnFunctions(BinaryContext &BC) {
  BEF->runOnFunctions(BC);

  outs() << "BOLT-INFO: statirng function and function call frequency pass\n";
  outs() << "BOLT-INFO: computing global static infered frequencies\n";

  CG = std::make_unique<BinaryFunctionCallGraph>(buildCallGraph(BC));
  // Search for function loop heads in reverse depth-first order.
  TopologicalCGOrder = CG->buildTraversalOrder();

  BinaryFunction *MainFunc = updateLocalCallFrequencies(BC);

  if (!MainFunc || !MainFunc->hasValidProfile()) {
    errs() << "BOLT-WARNING: it is impossible to propagate frequencies from the "
              "main function since it has invalid profile.\n";
    
    DenseSet<const BinaryFunction *> MainReachableFunctions;
    findingMainReachableFunctions(MainFunc, MainReachableFunctions);

    for (auto *node : TopologicalCGOrder) {
      // If function is not reachable from main, invalidate its profile
      // information.
      if (!MainReachableFunctions.count(node) &&
          CG->predecessors(CG->getNodeId(node)).size() == 0) {

        node->setExecutionCount(BinaryFunction::COUNT_NO_PROFILE);
      }
    }


    return;
  }

  findLoopEdgesInfo(BC, MainFunc);

  // Search for function loop heads in reverse depth-first order.
  DEBUG(FunPtr LocalFun = &FunctionCallFrequency::getLocalEdgeFrequency;
        printDot("call-graph-local-frequencies.dot", LocalFun););

  for (auto *node : TopologicalCGOrder) {
    // Propagates frequencies only for functions that have valid profile.
    if (!node->hasValidProfile())
      continue;
    // If function is a loop head, propagate frequencies from it.
    if (isLoopHeader(node)) {
      // Tag all reachable nodes as not visited.
      tagReachableFunctions(node);

      // Propagate call frequency starting from this loop head.
      propagateCallFrequency(node, node, false);
    }
  }

  // Tags all functions reachable from the main function as not visited.
  tagReachableFunctions(MainFunc);

  // Propagate frequency starting from the main function.
  propagateCallFrequency(MainFunc, MainFunc, true);

  DenseSet<const BinaryFunction *> MainReachableFunctions;
  findingMainReachableFunctions(MainFunc, MainReachableFunctions);

  for (auto *node : TopologicalCGOrder) {
    // If function is not reachable from main, invalidate its profile
    // information.
    if (!MainReachableFunctions.count(node) &&
        CG->predecessors(CG->getNodeId(node)).size() == 0) {

      node->setExecutionCount(BinaryFunction::COUNT_NO_PROFILE);
    }
  }

  // With function frequency calculated, propagate it to block and edge
  // frequencies to achieve global block and edge frequency.
  updateFrequencyValues(BC);

  DEBUG(FunPtr GlobalFun = &FunctionCallFrequency::getGlobalEdgeFrequency;        
        printDot("call-graph-global-frequencies.dot", GlobalFun););

  clear();

  outs() << "BOLT-INFO: the BB counts, function call counts and "
         << "global edge counts where updated.\n";
}

} // namespace bolt
} // namespace llvm
