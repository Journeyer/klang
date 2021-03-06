//===--- Driver.cpp - -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements main entry function.
//
//===----------------------------------------------------------------------===//

#include "klang/AST/ASTNodes.h"
#include "klang/Builtin/Tutorial.h"
#include "klang/Lex/Lexer.h"
#include "klang/Parse/Parser.h"

#include "llvm/ADT/OwningPtr.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"
#include "llvm/Transforms/Scalar.h"

#include <map>
#include <string>


//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

namespace klang {

  std::map<char, int> Token::BinopPrecedence;

  llvm::Module *TheModule;
  llvm::IRBuilder<> Builder(llvm::getGlobalContext());
  std::map<std::string, llvm::AllocaInst*> NamedValues;

  llvm::FunctionPassManager *TheFPM;
  llvm::ExecutionEngine *TheExecutionEngine;
}


namespace {
  llvm::cl::opt<std::string>
    OutputFilename("o",
                   llvm::cl::desc("Specify output filename"),
                   llvm::cl::value_desc("filename"));

  llvm::cl::opt<std::string>
    InputFilename(llvm::cl::Positional,
                  llvm::cl::desc("<input file>"),
                  llvm::cl::init("-"));
}


int main(int argc, char* const argv[]) {

  llvm::cl::ParseCommandLineOptions(argc, argv);

  llvm::OwningPtr<llvm::MemoryBuffer> Buf;

  if (llvm::MemoryBuffer::getFileOrSTDIN(InputFilename, Buf))
    return 1;

  llvm::InitializeNativeTarget();
  llvm::LLVMContext &Context = llvm::getGlobalContext();

  klang::Lexer myLexer(Buf->getBuffer());
  klang::Parser myParser(myLexer);

  // Install standard binary operators.
  // 1 is lowest precedence.
  klang::Token::BinopPrecedence['='] = 2;
  klang::Token::BinopPrecedence['<'] = 10;
  klang::Token::BinopPrecedence['+'] = 20;
  klang::Token::BinopPrecedence['-'] = 20;
  klang::Token::BinopPrecedence['*'] = 40;  // highest.

  // Make the module, which holds all the code.
  klang::TheModule = new llvm::Module("my cool jit", Context);

  //-----------------------------------------------------
  // Create the JIT.  This takes ownership of the module.
  std::string ErrStr;
  klang::TheExecutionEngine =
    llvm::EngineBuilder(klang::TheModule).setErrorStr(&ErrStr).create();
  if (!klang::TheExecutionEngine) {
    llvm::errs() << "Could not create ExecutionEngine: " << ErrStr.c_str()
      << "\n";
    exit(1);
  }

  llvm::FunctionPassManager OurFPM(klang::TheModule);

  // Set up the optimizer pipeline.  Start with registering info about how the
  // target lays out data structures.
  OurFPM.add(new llvm::DataLayout(*klang::TheExecutionEngine->getDataLayout()));
  // Provide basic AliasAnalysis support for GVN.
  OurFPM.add(llvm::createBasicAliasAnalysisPass());
  // Promote allocas to registers.
  OurFPM.add(llvm::createPromoteMemoryToRegisterPass());
  // Do simple "peephole" optimizations and bit-twiddling optzns.
  OurFPM.add(llvm::createInstructionCombiningPass());
  // Reassociate expressions.
  OurFPM.add(llvm::createReassociatePass());
  // Eliminate Common SubExpressions.
  OurFPM.add(llvm::createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  OurFPM.add(llvm::createCFGSimplificationPass());

  OurFPM.doInitialization();

  // Set the global so the code gen can use this.
  klang::TheFPM = &OurFPM;
  //-----------------------------------------------------

  // Run the main "interpreter loop" now.
  myParser.Go();

  klang::TheFPM = 0;

  // Print out all of the generated code.
  //FIXME
  //IR dumping will be done via a new frontendaction emit-llvm
  //klang::TheModule->dump();

  // Calls an unused function just not to lose it in the final binary
  // Without this call klangBuiltin.a is just ignored during linking
  putchard('\n');

  return 0;
}

