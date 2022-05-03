#pragma once

/*
Approach:
 - Rewrite RC-Analyser so that it works with the new AST
 - Replace RC-Tree with AST in Semantic Analysis
 - Should be compilable

 - Remove Modtree and annotate AST instead




Goals with new Analyser:
 - Use AST with Semantic Information
 - Remove unnecessary Stages/Data-Representation from Process (RC-Code, Modtree)
 - Simpler Dependency Handling
 - Simpler/More powerful Analysis

Problems with current Approach:
 - using Statements
    This is just a problem with Symbol-Resolution, not a problem of workloads
 - Polymorphic Functions/Templates (Analysis + Instanciation)
    Just requires annotating the AST multiple times, altough determining
    the order of Argument Evaluation is still tricky
 - includes
 - Function_Cluster_Compile
 - Struct_Reachable_Resolve
 - Bake Expressions
 - Macro Symbols
    Symbol Resolution
 - Conditional Compilation

My previous approaches were:
 * Stop and Go with Statement Granularity:
    This meant that there was a lot of downtime, since Expressions could potentially
    have lots of Dependencies, resulting in a lot of Switching.
    Also Global Symbols in a local Context (Comptime definitions inside a function, e.g. inner functions)
    were almost impossible to do
 * Analyse all Dependencies first:
    By doing a prepass almost all Symbols can be discovered (Except Cond. Compilation), 
    and big Workloads can be analysed in one Go without stopping. The problems here is that
    analysing all dependencies before knowing Context information can
    be difficult or annoying to deal with (Struct Reachable/Function Runnable, Bake-Expressions)
    
Further Problems:
 - Using Statement in Global Context influences Symbol Resolution
 - Using in Local Context requires expression-information
    x := Game~get_top_player()
    using x~*
    a += health // x.health
    using Datastructures~Hashtable

Potential Solution: Stop and Go with Fiber Granularity + Previous Discovery Stage
    When no workloads can be run, we go and detect circular dependencies. Esy
    Struct Reachable is then also not needed anymore, since this can be handled with stop and go.
    Function Cluster Compile? 
        A function can only be executed when all function it calls can be executed
        I think I have the same problem as with the stop and go.
    Struct Reachable Resolve?
    Struct Size is also Cluster -> No, since Size must not have Circular Dependency
    Bake?

Why and how to annotate AST?
    Otherwise I need to store the same information again (Operation Type, Expression Type, General Block-Structure)
    but this time only with Type/Symbol/Cast-Information. With the AST, I don't have to worry about 
    getting the structure wrong. Also, since things with Semantic Errors don't get compiled, I don't
    have to worry about generating Code for a non-compiled Structure

Problem: Symbol Discovery/Symbol Tables
*/

namespace AST {
    struct Module;
}

namespace Semantic_Analysis
{
    void initialize();
    void destroy();
    void analyse_ast(AST::Module* root);
}
